// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Configuration/RTSMilestone2Configuration.h"
#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "RTSProductionTypes.generated.h"

UENUM()
enum class ERTSProductionRefusal : uint8
{
	None,
	FactoryUnavailable,
	WrongTeam,
	NotFactory,
	InvalidUnitType,
	QueueFull,
	EconomyRejected,
	MatchResolved
};

UENUM()
enum class ERTSProductionState : uint8
{
	UnderConstruction,
	Idle,
	Producing,
	PausedPower,
	BlockedExit
};

USTRUCT()
struct RTS_API FRTSProductionQueueEntrySnapshot
{
	GENERATED_BODY()

	int32 QueueEntryId = INDEX_NONE;
	ERTSUnitType UnitType = ERTSUnitType::InfantrySquad;
	float Progress = 0.0f;
	float ProductionSeconds = 0.0f;
	int32 MaterialCost = 0;
	int32 SupplyCost = 0;
};

USTRUCT()
struct RTS_API FRTSProductionSnapshot
{
	GENERATED_BODY()

	int32 StableFactoryId = INDEX_NONE;
	FGenericTeamId TeamId = FGenericTeamId::NoTeam;
	ERTSProductionState State = ERTSProductionState::UnderConstruction;
	ERTSProductionRefusal LastRefusal = ERTSProductionRefusal::None;
	TArray<FRTSProductionQueueEntrySnapshot> Queue;
};

USTRUCT()
struct RTS_API FRTSProductionResult
{
	GENERATED_BODY()

	bool bAccepted = false;
	ERTSProductionRefusal Refusal = ERTSProductionRefusal::None;
	int32 QueueEntryId = INDEX_NONE;
	int64 EconomyTransactionId = 0;
};
