// Fill out your copyright notice in the Description page of Project Settings.


#include "NightSkyGameInstance.h"

#include "NightSkyEditorSettings.h"
#include "NightSkySettingsInfo.h"
#include "ReplayInfo.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "HAL/PlatformTime.h"
#include "NightSkyEngine/Network/SpectatorRoom.h"
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

void UNightSkyGameInstance::CancelRoomTravelGrace() const
{
	if (RoomTravelTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RoomTravelTicker);
		RoomTravelTicker.Reset();
	}
}

void UNightSkyGameInstance::Shutdown()
{
	CancelRoomTravelGrace();
	Super::Shutdown();
}

void UNightSkyGameInstance::TrackRoomTravelGrace(UWorld* World) const
{
	CancelRoomTravelGrace();
	const auto* Room = GetSubsystem<USpectatorRoom>();
	if (!World || World != GetWorld() || World->GetGameInstance() != this || World->GetNetMode() != NM_ListenServer ||
		!Room || !Room->IsEnabled() || !World->GetNetDriver() || World->GetNetDriver()->GetWorld() != World ||
		World->NextURL.IsEmpty() ||
		World->IsInSeamlessTravel() || World->NextSwitchCountdown <= 0.f ||
		!FMath::IsFinite(World->NextSwitchCountdown))
		return;

	// Retain the configured remaining notification grace, but do not stretch it
	// with the battle's fixed simulation delta when rendering runs below 60 Hz.
	const double Deadline = FPlatformTime::Seconds() + World->NextSwitchCountdown;
	const FString PendingURL = World->NextURL;
	const auto PendingTravelType = World->NextTravelType;
	const TWeakObjectPtr<UWorld> PendingWorld(World);
	const TWeakObjectPtr<UNetDriver> PendingDriver(World->GetNetDriver());
	const TWeakObjectPtr<const UNightSkyGameInstance> PendingGame(this);
	RoomTravelTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[PendingGame, PendingWorld, PendingDriver, PendingURL, PendingTravelType, Deadline](float)
		{
			const auto* Game = PendingGame.Get();
			if (!Game)
				return false;
			auto* CurrentWorld = PendingWorld.Get();
			const auto* CurrentRoom = Game->GetSubsystem<USpectatorRoom>();
			if (!CurrentWorld || CurrentWorld != Game->GetWorld() || CurrentWorld->GetGameInstance() != Game ||
				CurrentWorld->bIsTearingDown ||
				!PendingDriver.IsValid() || CurrentWorld->GetNetDriver() != PendingDriver.Get() ||
				PendingDriver->GetWorld() != CurrentWorld ||
				CurrentWorld->GetNetMode() != NM_ListenServer || !CurrentRoom || !CurrentRoom->IsEnabled() ||
				CurrentWorld->NextURL != PendingURL || CurrentWorld->NextTravelType != PendingTravelType ||
				CurrentWorld->IsInSeamlessTravel() || CurrentWorld->NextSwitchCountdown <= 0.f ||
				!FMath::IsFinite(CurrentWorld->NextSwitchCountdown))
			{
				Game->RoomTravelTicker.Reset();
				return false;
			}
			if (FPlatformTime::Seconds() < Deadline)
				return true;
			CurrentWorld->NextSwitchCountdown = 0.f;
			Game->RoomTravelTicker.Reset();
			return false;
		}), 0.f);
}

void UNightSkyGameInstance::TravelToVSInfo() const
{
	auto* World = GetWorld();
	FURL URL(nullptr, TEXT("VSInfo_PL"), TRAVEL_Absolute);
	if (World->GetNetMode() == NM_ListenServer)
		URL.AddOption(TEXT("listen"));
	const bool CanStartRequest = World->NextURL.IsEmpty() && !World->IsInSeamlessTravel();
	if (World->ServerTravel(URL.ToString(), true) && CanStartRequest)
		TrackRoomTravelGrace(World);
}

void UNightSkyGameInstance::TravelToBattleMap() const
{
	auto* World = GetWorld();
	FURL URL(nullptr, *BattleData.Stage->StageURL, TRAVEL_Absolute);
	if (World->GetNetMode() == NM_ListenServer)
		URL.AddOption(TEXT("listen"));
	const bool CanStartRequest = World->NextURL.IsEmpty() && !World->IsInSeamlessTravel();
	if (World->ServerTravel(URL.ToString(), true) && CanStartRequest)
		TrackRoomTravelGrace(World);
}

void UNightSkyGameInstance::LoadReplay()
{
	BattleData = CurrentReplay->BattleData;
}

bool UNightSkyGameInstance::HasReplayInput(int32 FrameNumber) const
{
	return CurrentReplay && FrameNumber >= 0 && FrameNumber < CurrentReplay->LengthInFrames &&
		CurrentReplay->InputsP1.IsValidIndex(FrameNumber) && CurrentReplay->InputsP2.IsValidIndex(FrameNumber);
}

void UNightSkyGameInstance::PlayReplayToGameState(int32 FrameNumber, int32& OutP1Input, int32& OutP2Input) const
{
	if (!HasReplayInput(FrameNumber))
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
	for (int i = 0; i < FramesToRollback; i++)
	{
		if (!CurrentReplay) return;
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