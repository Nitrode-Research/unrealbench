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
	// Restore the published gameplay contract.
	Super::BeginPlay();
}

void AScifiSimEscapeGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Restore the published gameplay contract.
	Super::EndPlay(EndPlayReason);
}

bool AScifiSimEscapeGameMode::HasEscaped() const
{
	// Restore the published gameplay contract.
	return {};
}

void AScifiSimEscapeGameMode::HandleFacilityStateChanged()
{
	// Restore the published gameplay contract.
}

void AScifiSimEscapeGameMode::SetPlayersFrozen(const bool bFrozen)
{
	// Restore the published gameplay contract.
}
