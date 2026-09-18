// Copyright Epic Games, Inc. All Rights Reserved.

#include "Reclaim/RTSWreckageSubsystem.h"

#include "Configuration/RTSMilestone2Configuration.h"
#include "Engine/World.h"
#include "Reclaim/RTSWreckage.h"
#include "Structures/RTSStructure.h"
#include "Units/RTSCombatUnit.h"

ARTSWreckage* URTSWreckageSubsystem::CreateFromUnit(const ARTSCombatUnit& Unit)
{
	// Restore this contractor-owned implementation.
	return {};
}

ARTSWreckage* URTSWreckageSubsystem::CreateFromStructure(const ARTSStructure& Structure)
{
	// Restore this contractor-owned implementation.
	return {};
}

ARTSWreckage* URTSWreckageSubsystem::Find(const int32 StableWreckageId) const
{
	return WreckageById.FindRef(StableWreckageId).Get();
}

TArray<ARTSWreckage*> URTSWreckageSubsystem::QueryAvailable() const
{
	// Restore this contractor-owned implementation.
	return {};
}

TArray<FRTSWreckageSnapshot> URTSWreckageSubsystem::QueryAvailableSnapshots() const
{
	// Restore this contractor-owned implementation.
	return {};
}

void URTSWreckageSubsystem::UnregisterWreckage(ARTSWreckage& Wreckage)
{
	// Restore this contractor-owned implementation.
}

ARTSWreckage* URTSWreckageSubsystem::SpawnWreckage(const FVector& WorldLocation)
{
	// Restore this contractor-owned implementation.
	return {};
}
