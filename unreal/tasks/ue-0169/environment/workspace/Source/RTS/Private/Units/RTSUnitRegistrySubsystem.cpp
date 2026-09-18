// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/RTSUnitRegistrySubsystem.h"

#include "Combat/RTSHealthComponent.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Units/RTSCombatUnit.h"

bool URTSUnitRegistrySubsystem::RegisterUnit(ARTSCombatUnit& Unit)
{
	// Restore this contractor-owned implementation.
	return {};
}

void URTSUnitRegistrySubsystem::UnregisterUnit(ARTSCombatUnit& Unit)
{
	// Restore this contractor-owned implementation.
}

TOptional<FRTSUnitSnapshot> URTSUnitRegistrySubsystem::Find(const int32 StableUnitId) const
{
	// Restore this contractor-owned implementation.
	return {};
}

ARTSCombatUnit* URTSUnitRegistrySubsystem::FindActor(const int32 StableUnitId) const
{
	// Restore this contractor-owned implementation.
	return {};
}

TArray<FRTSUnitSnapshot> URTSUnitRegistrySubsystem::Query(
	const FGenericTeamId TeamId,
	const TOptional<ERTSUnitType> UnitType) const
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSUnitRegistryChanged& URTSUnitRegistrySubsystem::OnUnitChanged()
{
	return UnitChanged;
}

FRTSUnitSnapshot URTSUnitRegistrySubsystem::MakeSnapshot(const ARTSCombatUnit& Unit) const
{
	// Restore this contractor-owned implementation.
	return {};
}
