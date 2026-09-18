// Copyright Epic Games, Inc. All Rights Reserved.

#include "Production/RTSProductionSubsystem.h"

#include "Production/RTSProductionComponent.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"

FRTSProductionResult URTSProductionSubsystem::TryEnqueue(
	const FGenericTeamId RequestingTeam,
	const int32 StableFactoryId,
	const ERTSUnitType UnitType)
{
	// Restore this contractor-owned implementation.
	return {};
}

TOptional<FRTSProductionSnapshot> URTSProductionSubsystem::Find(const int32 StableFactoryId) const
{
	// Restore this contractor-owned implementation.
	return {};
}
