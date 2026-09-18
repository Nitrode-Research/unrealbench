// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Reclaim/RTSReclaimTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSWreckageSubsystem.generated.h"

class ARTSCombatUnit;
class ARTSStructure;
class ARTSWreckage;

/** Creates one stable wreck per reported death and indexes only live wreck actors. */
UCLASS()
class RTS_API URTSWreckageSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	ARTSWreckage* CreateFromUnit(const ARTSCombatUnit& Unit);
	ARTSWreckage* CreateFromStructure(const ARTSStructure& Structure);
	ARTSWreckage* Find(int32 StableWreckageId) const;
	TArray<ARTSWreckage*> QueryAvailable() const;
	TArray<FRTSWreckageSnapshot> QueryAvailableSnapshots() const;
	void UnregisterWreckage(ARTSWreckage& Wreckage);

private:
	ARTSWreckage* SpawnWreckage(const FVector& WorldLocation);

	TMap<int32, TWeakObjectPtr<ARTSWreckage>> WreckageById;
	int32 NextStableWreckageId = 1;
};
