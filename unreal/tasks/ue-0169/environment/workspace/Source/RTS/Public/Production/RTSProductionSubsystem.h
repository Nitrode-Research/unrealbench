// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Production/RTSProductionTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSProductionSubsystem.generated.h"

/** Shared player, AI, and test transaction boundary for actor-owned Factory queues. */
UCLASS()
class RTS_API URTSProductionSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	FRTSProductionResult TryEnqueue(
		FGenericTeamId RequestingTeam,
		int32 StableFactoryId,
		ERTSUnitType UnitType);
	TOptional<FRTSProductionSnapshot> Find(int32 StableFactoryId) const;
};
