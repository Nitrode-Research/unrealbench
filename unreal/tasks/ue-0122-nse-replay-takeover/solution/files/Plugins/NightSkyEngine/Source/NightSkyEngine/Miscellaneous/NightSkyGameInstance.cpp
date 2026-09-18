// Fill out your copyright notice in the Description page of Project Settings.


#include "NightSkyGameInstance.h"

#include "NightSkyEditorSettings.h"
#include "NightSkySettingsInfo.h"
#include "ReplayInfo.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Actors/ParticleManager.h"
#include "NightSkyEngine/Battle/Misc/Bitflags.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "NightSkyEngine/Data/PrimaryCharaData.h"
#include "NightSkyEngine/Battle/FighterRunners/FighterReplayRunner.h"
#include "Kismet/GameplayStatics.h"
#include "NightSkyEngine/Data/PrimaryStageData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NightSkyGameInstance)

void UNightSkyGameInstance::Init()
{
	Super::Init();

#if WITH_EDITOR
	if (auto& InBattleData = UNightSkyEditorSettings::GetConst()->BattleData; InBattleData.bIsValid) 
		BattleData = InBattleData;
#endif
	
	BattleData.bIsValid = true;	
	BattleData.Random = FRandomManager(FDateTime::Now().ToUnixTimestamp());

	SettingsInfo = Cast<UNightSkySettingsInfo>(UGameplayStatics::LoadGameFromSlot("SYSTEM", 0));
	if (!SettingsInfo)
	{
		SettingsInfo = Cast<UNightSkySettingsInfo>(UGameplayStatics::CreateSaveGameObject(UNightSkySettingsInfo::StaticClass()));
		UGameplayStatics::SaveGameToSlot(SettingsInfo, "SYSTEM", 0);
	}

	const FString AntiAliasingCommand = "r.AntiAliasingMethod " + FString::FromInt(SettingsInfo->AntiAliasingMethod);
	const FString GlobalIlluminationCommand = "r.DynamicGlobalIlluminationMethod " + FString::FromInt(SettingsInfo->GlobalIlluminationMethod);

	GetWorld()->Exec(GetWorld(), *AntiAliasingCommand);
	GetWorld()->Exec(GetWorld(), *GlobalIlluminationCommand);
}

void UNightSkyGameInstance::TravelToVSInfo() const
{
	this->GetWorld()->ServerTravel("VSInfo_PL", true);
}

void UNightSkyGameInstance::TravelToBattleMap() const
{
	this->GetWorld()->ServerTravel(BattleData.Stage->StageURL, true);
}

void UNightSkyGameInstance::LoadReplay()
{
	BattleData = CurrentReplay->BattleData;
}

void UNightSkyGameInstance::PlayReplayToGameState(int32 FrameNumber, int32& OutP1Input, int32& OutP2Input) const
{
    if (!CurrentReplay || FrameNumber < 0 || FrameNumber >= CurrentReplay->LengthInFrames)
    {
        OutP1Input = 0;
        OutP2Input = 0;
        return;
    }
    OutP1Input = CurrentReplay->InputsP1[FrameNumber];
	OutP2Input = CurrentReplay->InputsP2[FrameNumber];
}

void UNightSkyGameInstance::RecordReplay()
{
    IsReplay = false;
    ReplayPosition = 0;
    ReplayOwner = -1;
    BranchReplay = nullptr;
    bReplayPaused = false;
    bReplayComplete = false;
    SavedBranchSlot.Empty();
    ReplayStatus.Empty();
	CurrentReplay = Cast<UReplaySaveInfo>(UGameplayStatics::CreateSaveGameObject(UReplaySaveInfo::StaticClass()));
	CurrentReplay->BattleData = BattleData;
	CurrentReplay->Version = BattleVersion;
	CurrentReplay->bIsTraining = IsTraining;
}

void UNightSkyGameInstance::UpdateReplay(int32 InputsP1, int32 InputsP2) const
{
	if (!CurrentReplay || IsReplay) return;
	CurrentReplay->LengthInFrames++;
	CurrentReplay->InputsP1.Add(InputsP1);
	CurrentReplay->InputsP2.Add(InputsP2);
}

void UNightSkyGameInstance::RollbackReplay(int32 FramesToRollback) const
{
	for (int i = 0; i < FramesToRollback && CurrentReplay && CurrentReplay->LengthInFrames > 0; i++)
	{
		if (!CurrentReplay || IsReplay) return;
		CurrentReplay->LengthInFrames--;
		CurrentReplay->InputsP1.Pop();
		CurrentReplay->InputsP2.Pop();
	}
}

void UNightSkyGameInstance::EndRecordReplay() const
{
    if (IsReplay)
    {
        if (ReplayOwner >= 0)
        {
            const_cast<UNightSkyGameInstance*>(this)->FinishReplayBranch();
        }
        return;
    }
    if (!CurrentReplay) return;
    for (int32 Index = 0; Index < MAX_int32; ++Index)
    {
        const FString Slot = ReplaySlotPrefix + FString::FromInt(Index);
        if (!UGameplayStatics::DoesSaveGameExist(Slot, 0))
        {
            UGameplayStatics::SaveGameToSlot(CurrentReplay, Slot, 0);
            return;
        }
    }
}

void UNightSkyGameInstance::PlayReplayFromBP(FString ReplayName)
{
    BeginReplaySession(Cast<UReplaySaveInfo>(UGameplayStatics::LoadGameFromSlot(ReplayName, 0)));
}

void UNightSkyGameInstance::FindReplays()
{
    ReplayList.Empty();
    for (const FString& Slot : GetSavedReplaySlots())
    {
        auto* Replay = Cast<UReplaySaveInfo>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
        if (Replay && Replay->Version == BattleVersion)
        {
            const FString Suffix = Slot.RightChop(ReplaySlotPrefix.Len());
            if (Suffix.IsNumeric()) Replay->ReplayIndex = FCString::Atoi(*Suffix);
            ReplayList.Add(Replay);
        }
    }
    BP_OnFindReplaysComplete(ReplayList);
}

void UNightSkyGameInstance::DeleteReplay(const FString& ReplayName)
{
	if (UGameplayStatics::DoesSaveGameExist(ReplayName, 0))
	{
		UGameplayStatics::DeleteGameInSlot(ReplayName, 0);
	}
	FindReplays();
}

bool UNightSkyGameInstance::BeginReplaySession(UReplaySaveInfo* Source)
{
    if (!Source || Source->Version != BattleVersion || Source->LengthInFrames < 0 ||
        Source->InputsP1.Num() != Source->LengthInFrames || Source->InputsP2.Num() != Source->LengthInFrames)
    {
        ReplayStatus = "Rejected";
        return false;
    }
    // Validate asset availability before committing any session state.
    if (!IsValid(Source->BattleData.Stage) || Source->BattleData.PlayerListP1.IsEmpty() ||
        Source->BattleData.PlayerListP2.IsEmpty())
    {
        ReplayStatus = "Rejected";
        return false;
    }
    for (const auto& Team : {Source->BattleData.PlayerListP1, Source->BattleData.PlayerListP2})
    {
        for (const auto& Character : Team)
        {
            const auto* Asset = Character.LoadSynchronous();
            if (!Asset || !Asset->PlayerClass)
            {
                ReplayStatus = "Rejected";
                return false;
            }
        }
    }
    CurrentReplay = Source;
    BattleData = Source->BattleData;
    IsTraining = Source->bIsTraining;
    IsReplay = true;
    FighterRunner = Multiplayer;
    ReplayPosition = 0;
    ReplayOwner = -1;
    BranchReplay = nullptr;
    bReplayPaused = false;
    bReplayComplete = false;
    SavedBranchSlot.Empty();
    ReplayStatus = "Playback";
    IsCPUBattle = false;
    if (GetWorld())
    {
        if (TActorIterator<ANightSkyGameState> BattleIterator(GetWorld()); BattleIterator)
        {
            // An existing local runner would bypass replay input ownership and EOF.
            if (BattleIterator->FighterRunner &&
                !BattleIterator->FighterRunner->IsA<AFighterReplayRunner>())
            {
                BattleIterator->FighterRunner->Destroy();
                BattleIterator->FighterRunner = nullptr;
            }
            for (auto* Player : BattleIterator->Players)
            {
                if (Player) Player->bIsCpu = false;
            }
            BattleIterator->bPauseGame = false;
            if (BattleIterator->ParticleManager)
            {
                BattleIterator->ParticleManager->ClearCommonParticles();
            }
            BattleIterator->InitializeReplayBattle();
            BattleIterator->LocalFrame = 0;
        }
    }
    return true;
}

bool UNightSkyGameInstance::TakeOverReplay(int32 Side)
{
    if (!IsReplay || !CurrentReplay || !IsTraining || !GetWorld() || GetWorld()->GetNetMode() != NM_Standalone ||
        ReplayOwner != -1 || Side < 0 || Side > 1 || bReplayComplete)
    {
        ReplayStatus = "Rejected";
        return false;
    }
    // Copy initialization and the actual source prefix before accepting ownership.
    BranchReplay = DuplicateObject<UReplaySaveInfo>(CurrentReplay, this);
    BranchReplay->InputsP1.SetNum(ReplayPosition);
    BranchReplay->InputsP2.SetNum(ReplayPosition);
    BranchReplay->LengthInFrames = ReplayPosition;
    BranchReplay->TakeoverFrame = ReplayPosition;
    BranchReplay->TakeoverSide = Side;
    BranchReplay->ReplayIndex = -1;
    BranchReplay->Timestamp = FDateTime::Now();
    ReplayOwner = Side;
    ReplayStatus = "Branch";
    return true;
}

bool UNightSkyGameInstance::SeekReplay(double Position)
{
    if (!IsReplay || !CurrentReplay || ReplayOwner != -1 || !FMath::IsFinite(Position) || Position < 0 ||
        Position > CurrentReplay->LengthInFrames || FMath::FloorToDouble(Position) != Position)
    {
        ReplayStatus = "Rejected";
        return false;
    }
    ANightSkyGameState* BattleState = nullptr;
    if (GetWorld())
    {
        if (TActorIterator<ANightSkyGameState> BattleIterator(GetWorld()); BattleIterator)
        {
            BattleState = *BattleIterator;
        }
    }
    if (!BattleState)
    {
        ReplayStatus = "Rejected";
        return false;
    }
    const int32 TargetFrame = static_cast<int32>(Position);
    if (BattleState->ParticleManager)
    {
        if (TargetFrame < ReplayPosition)
        {
            // Discard transient presentation from the old timeline. Replayed
            // historical one-shots remain suppressed by the ordinary gates.
            BattleState->ParticleManager->ClearCommonParticles();
        }
        else if (TargetFrame > ReplayPosition && BattleState->ParticleManager->CommonParticleRenderer)
        {
            // Age only effects that existed before the seek. New effects from
            // skipped frames must never be submitted to this renderer. This
            // native common-effect clock is independent of owner hitstop.
            BattleState->ParticleManager->CommonParticleRenderer->Advance(
                OneFrame * static_cast<float>(TargetFrame - ReplayPosition));
        }
    }
    BattleState->bReplayCatchUp = true;
    if (TargetFrame < ReplayPosition)
    {
        BattleData = CurrentReplay->BattleData;
        BattleState->MatchInit();
        BattleState->LocalFrame = 0;
        ReplayPosition = 0;
    }
    while (ReplayPosition < TargetFrame)
    {
        int32 PlayerOneInput = 0, PlayerTwoInput = 0;
        PlayReplayToGameState(ReplayPosition, PlayerOneInput, PlayerTwoInput);
        BattleState->UpdateGameState(PlayerOneInput, PlayerTwoInput, false);
        ++ReplayPosition;
    }
    BattleState->bReplayCatchUp = false;
    BattleState->PresentReplayDestination();
    ReplayStatus = "Playback";
    return true;
}

bool UNightSkyGameInstance::FinishReplayBranch()
{
    if (bReplayComplete)
    {
        ReplayStatus = "Complete";
        return true;
    }
    if (!BranchReplay || ReplayOwner < 0)
    {
        ReplayStatus = "Rejected";
        return false;
    }
    for (int32 Index = 0; Index < MAX_int32; ++Index)
    {
        const FString Slot = ReplaySlotPrefix + FString::FromInt(Index);
        if (UGameplayStatics::DoesSaveGameExist(Slot, 0))
        {
            continue;
        }
        if (!UGameplayStatics::SaveGameToSlot(BranchReplay, Slot, 0))
        {
            ReplayStatus = "SaveFailed";
            return false;
        }
        SavedBranchSlot = Slot;
        bReplayComplete = true;
        ReplayStatus = "Complete";
        return true;
    }
    ReplayStatus = "SaveFailed";
    return false;
}

bool UNightSkyGameInstance::AdvanceReplay(ANightSkyGameState* BattleState)
{
    if (!BattleState || !IsReplay || !CurrentReplay || bReplayPaused || BattleState->bPauseGame ||
        bReplayComplete || (ReplayOwner < 0 && ReplayPosition >= CurrentReplay->LengthInFrames))
    {
        return false;
    }
    int32 PlayerOneInput = 0, PlayerTwoInput = 0;
    PlayReplayToGameState(ReplayPosition, PlayerOneInput, PlayerTwoInput);
    if (ReplayOwner >= 0)
    {
        if (ReplayOwner == 0)
        {
            PlayerOneInput = BattleState->GetLocalInputs(0);
        }
        else
        {
            PlayerTwoInput = BattleState->GetLocalInputs(1);
        }
        PlayerOneInput &= ~(INP_Rematch | INP_ResetTraining);
        PlayerTwoInput &= ~(INP_Rematch | INP_ResetTraining);
        // Record before simulation: a match-ending frame must be included in its save.
        BranchReplay->InputsP1.Add(PlayerOneInput);
        BranchReplay->InputsP2.Add(PlayerTwoInput);
        ++BranchReplay->LengthInFrames;
    }
    ++ReplayPosition;
    ReplayStatus = ReplayOwner >= 0 ? "Branch" : "Playback";
    BattleState->UpdateGameState(PlayerOneInput, PlayerTwoInput, false);
    return true;
}

TArray<FString> UNightSkyGameInstance::GetSavedReplaySlots() const
{
    TArray<FString> Slots;
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("*.sav")), true, false);
    for (const FString& File : Files)
    {
        const FString Slot = FPaths::GetBaseFilename(File);
        if (Slot.StartsWith(ReplaySlotPrefix) && UGameplayStatics::DoesSaveGameExist(Slot, 0))
        {
            Slots.Add(Slot);
        }
    }
    Slots.Sort();
    return Slots;
}

bool UNightSkyGameInstance::TakeOverReplayAt(double Position, int32 Side)
{
    if (!IsReplay || !CurrentReplay || !IsTraining || !GetWorld() || GetWorld()->GetNetMode() != NM_Standalone ||
        ReplayOwner != -1 || bReplayComplete || Side < 0 || Side > 1 ||
        !FMath::IsFinite(Position) || Position < 0 || Position > CurrentReplay->LengthInFrames ||
        FMath::FloorToDouble(Position) != Position)
    {
        ReplayStatus = "Rejected";
        return false;
    }
    if (!SeekReplay(Position))
    {
        return false;
    }
    return TakeOverReplay(Side);
}
