// Copyright Epic Games, Inc. All Rights Reserved.

#include "Production/RTSProductionComponent.h"

#include "Economy/RTSEconomySubsystem.h"
#include "Engine/World.h"
#include "Match/RTSMatchSubsystem.h"
#include "Orders/RTSCommandSubsystem.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "TimerManager.h"
#include "Units/RTSCombatUnit.h"

namespace
{
constexpr float ProcessingIntervalSeconds = 0.1f;
}

URTSProductionComponent::URTSProductionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URTSProductionComponent::Initialize(ARTSStructure& InFactory)
{
	// Restore this contractor-owned implementation.
}

void URTSProductionComponent::NotifyConstructionCompleted()
{
	// Restore this contractor-owned implementation.
}

FRTSProductionResult URTSProductionComponent::TryEnqueue(const ERTSUnitType UnitType)
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSProductionSnapshot URTSProductionComponent::GetSnapshot() const
{
	// Restore this contractor-owned implementation.
	return {};
}

void URTSProductionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Restore this contractor-owned implementation.
	Super::EndPlay(EndPlayReason);
}

void URTSProductionComponent::StartProcessing()
{
	// Restore this contractor-owned implementation.
}

void URTSProductionComponent::AdvanceQueue()
{
	// Restore this contractor-owned implementation.
}

bool URTSProductionComponent::TryCompleteFrontEntry()
{
	// Restore this contractor-owned implementation.
	return {};
}

void URTSProductionComponent::ReleaseOutstandingReservations()
{
	// Restore this contractor-owned implementation.
}
