// Copyright Epic Games, Inc. All Rights Reserved.

#include "Reclaim/RTSReclaimComponent.h"

#include "Configuration/RTSMilestone2Configuration.h"
#include "Match/RTSMatchSubsystem.h"
#include "Reclaim/RTSWreckage.h"
#include "Units/RTSCombatUnit.h"

URTSReclaimComponent::URTSReclaimComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URTSReclaimComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Restore this contractor-owned implementation.
	Super::EndPlay(EndPlayReason);
}

FRTSReclaimResult URTSReclaimComponent::Begin(ARTSWreckage& Wreckage)
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSReclaimResult URTSReclaimComponent::Advance(const float DeltaTime)
{
	// Restore this contractor-owned implementation.
	return {};
}

void URTSReclaimComponent::MarkApproaching()
{
	// Restore this contractor-owned implementation.
}

void URTSReclaimComponent::Interrupt(const ERTSReclaimRefusal Refusal)
{
	// Restore this contractor-owned implementation.
}

FRTSReclaimSnapshot URTSReclaimComponent::GetSnapshot() const
{
	return Snapshot;
}

FRTSReclaimChanged& URTSReclaimComponent::OnReclaimChanged()
{
	return ReclaimChanged;
}
