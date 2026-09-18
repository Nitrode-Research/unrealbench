// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "RTSEconomyTypes.generated.h"

UENUM()
enum class ERTSEconomyRefusal : uint8
{
	None,
	InvalidTeam,
	InvalidTransaction,
	AlreadyApplied,
	InsufficientMaterials,
	InsufficientSupply,
	WouldCreateNegativeTotal
};

UENUM()
enum class ERTSEconomyTransactionKind : uint8
{
	Commit,
	SupplyConversion,
	SupplyRelease,
	Rollback
};

USTRUCT()
struct RTS_API FRTSEconomySnapshot
{
	GENERATED_BODY()

	FGenericTeamId TeamId = FGenericTeamId::NoTeam;
	int32 Materials = 0;
	int32 PowerGeneration = 0;
	int32 PowerDemand = 0;
	int32 SupplyUsed = 0;
	int32 SupplyReserved = 0;
	int32 SupplyCapacity = 0;

	bool IsInBrownout() const { return PowerDemand > PowerGeneration; }
};

USTRUCT()
struct RTS_API FRTSEconomyDelta
{
	GENERATED_BODY()

	int32 Materials = 0;
	int32 PowerGeneration = 0;
	int32 PowerDemand = 0;
	int32 SupplyUsed = 0;
	int32 SupplyReserved = 0;
	int32 SupplyCapacity = 0;
};

USTRUCT()
struct RTS_API FRTSEconomyTransactionResult
{
	GENERATED_BODY()

	bool bAccepted = false;
	ERTSEconomyRefusal Refusal = ERTSEconomyRefusal::None;
	int64 TransactionId = 0;
	FRTSEconomySnapshot Snapshot;
};

USTRUCT()
struct RTS_API FRTSEconomyTransactionReceipt
{
	GENERATED_BODY()

	ERTSEconomyTransactionKind Kind = ERTSEconomyTransactionKind::Commit;
	int64 TransactionId = 0;
	int64 RelatedTransactionId = 0;
	FGenericTeamId TeamId = FGenericTeamId::NoTeam;
	FRTSEconomyDelta Delta;
	FRTSEconomySnapshot Snapshot;
};
