// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Units/RTSUnitTypes.h"
#include "RTSUnitRegistrySubsystem.generated.h"

class ARTSCombatUnit;

DECLARE_MULTICAST_DELEGATE_OneParam(FRTSUnitRegistryChanged, const FRTSUnitSnapshot&);

/** Stable, non-owning unit discovery for AI, results, UI, and automation. */
UCLASS()
class RTS_API URTSUnitRegistrySubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	bool RegisterUnit(ARTSCombatUnit& Unit);
	void UnregisterUnit(ARTSCombatUnit& Unit);
	TOptional<FRTSUnitSnapshot> Find(int32 StableUnitId) const;
	ARTSCombatUnit* FindActor(int32 StableUnitId) const;
	TArray<FRTSUnitSnapshot> Query(
		FGenericTeamId TeamId,
		TOptional<ERTSUnitType> UnitType = {}) const;
	FRTSUnitRegistryChanged& OnUnitChanged();

private:
	FRTSUnitSnapshot MakeSnapshot(const ARTSCombatUnit& Unit) const;

	TMap<int32, TWeakObjectPtr<ARTSCombatUnit>> UnitsById;
	FRTSUnitRegistryChanged UnitChanged;
};
