// Copyright Epic Games, Inc. All Rights Reserved.

#include "ScifiSimEscapeGameMode.h"

#include "Engine/World.h"
#include "Facility/FacilityStateSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "ScifiSimEscape.h"

AScifiSimEscapeGameMode::AScifiSimEscapeGameMode()
{
	// stub
}

void AScifiSimEscapeGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (UFacilityStateSubsystem* Facility = UWorld::GetSubsystem<UFacilityStateSubsystem>(GetWorld()))
	{
		Facility->OnStateChanged.AddDynamic(this, &AScifiSimEscapeGameMode::HandleFacilityStateChanged);
	}
}

void AScifiSimEscapeGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UFacilityStateSubsystem* Facility = UWorld::GetSubsystem<UFacilityStateSubsystem>(GetWorld()))
	{
		Facility->OnStateChanged.RemoveDynamic(this, &AScifiSimEscapeGameMode::HandleFacilityStateChanged);
	}

	Super::EndPlay(EndPlayReason);
}

bool AScifiSimEscapeGameMode::HasEscaped() const
{
	const UFacilityStateSubsystem* Facility = UWorld::GetSubsystem<UFacilityStateSubsystem>(GetWorld());
	return Facility != nullptr && Facility->HasEscaped();
}

void AScifiSimEscapeGameMode::HandleFacilityStateChanged()
{
	const UFacilityStateSubsystem* Facility = UWorld::GetSubsystem<UFacilityStateSubsystem>(GetWorld());
	const bool bEscaped = Facility != nullptr && Facility->HasEscaped();
	if (bEscaped == bLastReportedEscaped)
	{
		return;
	}

	// Recorded before the hooks run, so anything they change compares against the new value.
	bLastReportedEscaped = bEscaped;

	SetPlayersFrozen(bEscaped);

	if (bEscaped)
	{
		const EFacilityExit Exit = Facility->GetEscapeExit();
		UE_LOG(LogScifiSimEscape, Log, TEXT("Attempt won through the %s. Player input is off."),
			*UEnum::GetDisplayValueAsText(Exit).ToString());

		OnEscaped(Exit);
		ReceiveEscaped(Exit);
	}
	else
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("New attempt. Player input is back on."));
	}
}

void AScifiSimEscapeGameMode::SetPlayersFrozen(const bool bFrozen)
{
	// Move and look are ignored rather than input disabled outright, so a win widget still gets its clicks.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PlayerController = It->Get())
		{
			PlayerController->SetIgnoreMoveInput(bFrozen);
			PlayerController->SetIgnoreLookInput(bFrozen);
		}
	}
}
