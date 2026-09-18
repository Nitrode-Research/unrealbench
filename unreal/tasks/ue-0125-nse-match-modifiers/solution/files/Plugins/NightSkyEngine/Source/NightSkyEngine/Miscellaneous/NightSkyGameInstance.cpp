// Fill out your copyright notice in the Description page of Project Settings.


#include "NightSkyGameInstance.h"

#include "NightSkyEditorSettings.h"
#include "NightSkySettingsInfo.h"
#include "ReplayInfo.h"
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
if (!CurrentReplay ||
    !ValidateModifierContent(CurrentReplay->BattleData.Modifiers, ModifierSetupError))
{
    IsReplay = false;
    return;
}
BattleData = CurrentReplay->BattleData;
}

void UNightSkyGameInstance::PlayReplayToGameState(int32 FrameNumber, int32& OutP1Input, int32& OutP2Input) const
{
	if (FrameNumber >= CurrentReplay->LengthInFrames)
	{
		UGameplayStatics::OpenLevel(this, FName(TEXT("MainMenu_PL")));
		return;
	}
	OutP1Input = CurrentReplay->InputsP1[FrameNumber];
	OutP2Input = CurrentReplay->InputsP2[FrameNumber];
}

void UNightSkyGameInstance::RecordReplay()
{
	CurrentReplay = Cast<UReplaySaveInfo>(UGameplayStatics::CreateSaveGameObject(UReplaySaveInfo::StaticClass()));
	CurrentReplay->BattleData = BattleData;
	CurrentReplay->Version = BattleVersion;
	CurrentReplay->bIsTraining = IsTraining;
}

void UNightSkyGameInstance::UpdateReplay(int32 InputsP1, int32 InputsP2) const
{
	if (!CurrentReplay) return;
	CurrentReplay->LengthInFrames++;
	CurrentReplay->InputsP1.Add(InputsP1);
	CurrentReplay->InputsP2.Add(InputsP2);
}

void UNightSkyGameInstance::RollbackReplay(int32 FramesToRollback) const
{
	if (IsReplay)
	{
		return; // Playback reads an immutable saved input tape.
	}
	for (int i = 0; i < FramesToRollback; i++)
	{
		if (!CurrentReplay || CurrentReplay->LengthInFrames <= 0 || CurrentReplay->InputsP1.IsEmpty() ||
			CurrentReplay->InputsP2.IsEmpty())
		{
			return;
		}
		CurrentReplay->LengthInFrames--;
		CurrentReplay->InputsP1.Pop();
		CurrentReplay->InputsP2.Pop();
	}
}

void UNightSkyGameInstance::EndRecordReplay() const
{
	if (IsReplay) return;
	FString ReplayName = "REPLAY";
	for (int i = 0; i < MaxReplays; i++)
	{
		ReplayName = "REPLAY";
		ReplayName.AppendInt(i);
		if (!UGameplayStatics::DoesSaveGameExist(ReplayName, 0))
		{
			break;
		}
	}
	UGameplayStatics::SaveGameToSlot(CurrentReplay, ReplayName, 0);
}

void UNightSkyGameInstance::PlayReplayFromBP(FString ReplayName)
{
	FighterRunner = Multiplayer;
	IsReplay = true;
	CurrentReplay = Cast<UReplaySaveInfo>(UGameplayStatics::LoadGameFromSlot(ReplayName, 0));
if (!CurrentReplay)
{
    ModifierSetupError = TEXT("Replay unavailable");
    IsReplay = false;
    return;
}
	IsTraining = CurrentReplay->bIsTraining;
	LoadReplay();
}

void UNightSkyGameInstance::FindReplays()
{
	ReplayList.Empty();
	for (int i = 0; i < MaxReplays; i++)
	{
		FString ReplayName = "REPLAY";
		ReplayName.AppendInt(i);
		if (!UGameplayStatics::DoesSaveGameExist(ReplayName, 0))
		{
			continue;
		}
		ReplayList.Add(Cast<UReplaySaveInfo>(UGameplayStatics::LoadGameFromSlot(ReplayName, 0)));
		ReplayList.Last()->ReplayIndex = i;
		if (ReplayList.Last()->Version != BattleVersion)
		{
			ReplayList.Pop();
			UGameplayStatics::DeleteGameInSlot(ReplayName, 0);
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
bool UNightSkyGameInstance::ValidateModifierContent(const FModifierConfiguration& Configuration,
                                                    FString& Reason) const
{
    if (!Configuration.Validate(Reason))
        return false;
    FModifierConfiguration Installed;
    Installed.Definitions = AvailableModifiers;
    if (!Installed.Validate(Reason))
        return false;
    for (const auto& Required : Configuration.Definitions)
    {
        const auto* Available = AvailableModifiers.FindByPredicate(
            [&](const auto& D)
            { return D.Identifier.Equals(Required.Identifier, ESearchCase::CaseSensitive); });
        if (!Available)
        {
            Reason = Required.Identifier + TEXT(": unavailable definition");
            return false;
        }
        FModifierConfiguration A, B;
        A.Definitions.Add(*Available);
        B.Definitions.Add(Required);
        if (!A.Canonical().Equals(B.Canonical(), ESearchCase::CaseSensitive))
        {
            Reason = Required.Identifier + TEXT(": incompatible revision or content");
            return false;
        }
    }
    Reason.Reset();
    return true;
}
bool UNightSkyGameInstance::AcceptModifierPeer(const FModifierConfiguration& Remote)
{
    ModifierPeerAccepted = false;
    if (!ValidateModifierContent(BattleData.Modifiers, ModifierSetupError))
        return false;
    if (!ValidateModifierContent(Remote, ModifierSetupError))
        return false;
    if (!Remote.Canonical().Equals(BattleData.Modifiers.Canonical(), ESearchCase::CaseSensitive))
    {
        TArray<FString> Ids;
        for (const auto& D : BattleData.Modifiers.Definitions) Ids.AddUnique(D.Identifier);
        for (const auto& D : Remote.Definitions) Ids.AddUnique(D.Identifier);
        Ids.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
        ModifierSetupError = TEXT("Match modifiers: peer configuration mismatch");
        for (const FString& Id : Ids)
        {
            auto RuleConfig = [&Id](const FModifierConfiguration& Source)
            {
                FModifierConfiguration Result;
                for (const auto& D : Source.Definitions)
                    if (D.Identifier.Equals(Id, ESearchCase::CaseSensitive)) Result.Definitions.Add(D);
                for (const auto& I : Source.Schedule)
                    if (I.Identifier.Equals(Id, ESearchCase::CaseSensitive)) Result.Schedule.Add(I);
                return Result.Canonical();
            };
            if (!RuleConfig(Remote).Equals(RuleConfig(BattleData.Modifiers), ESearchCase::CaseSensitive))
            {
                ModifierSetupError = Id + TEXT(": peer selection or activation schedule mismatch");
                break;
            }
        }
        return false;
    }
    ModifierSetupError.Reset();
    ModifierPeerConfiguration = Remote.Canonical();
    ModifierPeerAccepted = true;
    return true;
}

bool UNightSkyGameInstance::SaveRecordedReplay(const FString& Slot)
{
    return CurrentReplay && !Slot.IsEmpty() &&
           UGameplayStatics::SaveGameToSlot(CurrentReplay, Slot, 0);
}
bool UNightSkyGameInstance::HasReplayFrame(int32 Frame) const
{
    return CurrentReplay && Frame >= 0 && Frame < CurrentReplay->LengthInFrames &&
           CurrentReplay->InputsP1.IsValidIndex(Frame) &&
           CurrentReplay->InputsP2.IsValidIndex(Frame);
}
