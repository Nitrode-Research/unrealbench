// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Configuration/RTSMilestone2Configuration.h"
#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "RTSReclaimTypes.generated.h"

class ARTSCombatUnit;
class ARTSWreckage;

UENUM()
enum class ERTSWreckageSourceKind : uint8
{
	Unit,
	Structure
};

UENUM()
enum class ERTSReclaimState : uint8
{
	Idle,
	Approaching,
	Reclaiming,
	Completed,
	Interrupted
};

UENUM()
enum class ERTSReclaimRefusal : uint8
{
	None,
	InvalidReclaimer,
	ReclaimerUnavailable,
	NotInfantry,
	InvalidWreckage,
	WreckageUnavailable,
	WrongWorld,
	AlreadyLeased,
	OutOfRange,
	EconomyRejected,
	MatchResolved
};

USTRUCT()
struct RTS_API FRTSWreckageSnapshot
{
	GENERATED_BODY()

	int32 StableWreckageId = INDEX_NONE;
	ERTSWreckageSourceKind SourceKind = ERTSWreckageSourceKind::Unit;
	ERTSUnitType SourceUnitType = ERTSUnitType::InfantrySquad;
	ERTSStructureType SourceStructureType = ERTSStructureType::Headquarters;
	FGenericTeamId OriginalTeamId = FGenericTeamId::NoTeam;
	FVector WorldLocation = FVector::ZeroVector;
	int32 ReclaimMaterialValue = 0;
	bool bConsumed = false;
	int32 LeaseHolderStableUnitId = INDEX_NONE;
};

USTRUCT()
struct RTS_API FRTSReclaimSnapshot
{
	GENERATED_BODY()

	ERTSReclaimState State = ERTSReclaimState::Idle;
	float Progress = 0.0f;
	ERTSReclaimRefusal LastRefusal = ERTSReclaimRefusal::None;

	UPROPERTY()
	TObjectPtr<ARTSWreckage> Target;
};

USTRUCT()
struct RTS_API FRTSReclaimResult
{
	GENERATED_BODY()

	bool bAccepted = false;
	bool bCompleted = false;
	ERTSReclaimRefusal Refusal = ERTSReclaimRefusal::None;
};
