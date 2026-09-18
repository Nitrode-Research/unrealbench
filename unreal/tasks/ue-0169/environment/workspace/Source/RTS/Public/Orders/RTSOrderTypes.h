// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RTSOrderTypes.generated.h"

class ARTSCombatUnit;
class ARTSStructure;
class ARTSWreckage;

/** Repository-readable Milestone 1 movement tuning loaded from DefaultGame.ini. */
USTRUCT()
struct RTS_API FRTSMovementTuning
{
	GENERATED_BODY()

	float UnitSpeed = 650.0f;
	float TurnRateDegreesPerSecond = 540.0f;
	float ArrivalTolerance = 100.0f;
	float FormationGap = 60.0f;
	float RetryDelaySeconds = 0.25f;
	int32 MaximumFormationExpansionRings = 3;

	static FRTSMovementTuning Load();
};

UENUM()
enum class ERTSOrderKind : uint8
{
	None,
	Move,
	Attack,
	Reclaim
};

UENUM()
enum class ERTSOrderPhase : uint8
{
	Idle,
	Moving,
	Chasing,
	Attacking,
	Approaching,
	Reclaiming
};

UENUM()
enum class ERTSOrderFailure : uint8
{
	None,
	InvalidUnit,
	UnitUnavailable,
	WrongWorld,
	NavigationUnavailable,
	DestinationNotNavigable,
	OutsideStartingZone,
	NoReachableSlot,
	MoveRequestRejected,
	PathFollowingFailed,
	InvalidTarget,
	TargetUnavailable,
	FriendlyTarget,
	InvalidWreckage,
	WreckageUnavailable,
	NotReclaimer,
	ReclaimContended,
	ReclaimInterrupted,
	MatchResolved
};

USTRUCT()
struct RTS_API FRTSOrderRequest
{
	GENERATED_BODY()

	ERTSOrderKind Kind = ERTSOrderKind::None;
	int64 GroupCommandId = 0;
	FVector Destination = FVector::ZeroVector;
	float AcceptanceRadius = 100.0f;

	UPROPERTY()
	TObjectPtr<ARTSCombatUnit> Target;

	UPROPERTY()
	TObjectPtr<ARTSStructure> TargetStructure;

	UPROPERTY()
	TObjectPtr<ARTSWreckage> WreckageTarget;

	bool bAutomatic = false;

	static FRTSOrderRequest MakeMove(
		int64 NewGroupCommandId,
		const FVector& NewDestination,
		float NewAcceptanceRadius);
	static FRTSOrderRequest MakeAttack(
		int64 NewGroupCommandId,
		ARTSCombatUnit& NewTarget,
		bool bNewAutomatic = false);
	static FRTSOrderRequest MakeAttack(
		int64 NewGroupCommandId,
		ARTSStructure& NewTarget);
	static FRTSOrderRequest MakeReclaim(
		int64 NewGroupCommandId,
		ARTSWreckage& NewTarget);
};

USTRUCT()
struct RTS_API FRTSOrderSnapshot
{
	GENERATED_BODY()

	ERTSOrderKind Kind = ERTSOrderKind::None;
	ERTSOrderPhase Phase = ERTSOrderPhase::Idle;
	int64 GroupCommandId = 0;
	FVector Destination = FVector::ZeroVector;

	UPROPERTY()
	TObjectPtr<ARTSCombatUnit> Target;

	UPROPERTY()
	TObjectPtr<ARTSStructure> TargetStructure;

	UPROPERTY()
	TObjectPtr<ARTSWreckage> WreckageTarget;

	bool bAutomatic = false;
	double LastAttackTimeSeconds = -1.0;
	ERTSOrderFailure LastFailure = ERTSOrderFailure::None;
};

USTRUCT()
struct RTS_API FRTSUnitCommandOutcome
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<ARTSCombatUnit> Unit;

	bool bAccepted = false;
	FVector AssignedDestination = FVector::ZeroVector;

	UPROPERTY()
	TObjectPtr<ARTSCombatUnit> AssignedTarget;

	UPROPERTY()
	TObjectPtr<ARTSStructure> AssignedStructureTarget;

	UPROPERTY()
	TObjectPtr<ARTSWreckage> AssignedWreckageTarget;

	ERTSOrderFailure Failure = ERTSOrderFailure::None;
};

USTRUCT()
struct RTS_API FRTSCommandResult
{
	GENERATED_BODY()

	int64 GroupCommandId = 0;
	FVector RequestedDestination = FVector::ZeroVector;
	FVector ProjectedDestination = FVector::ZeroVector;

	UPROPERTY()
	TObjectPtr<ARTSCombatUnit> RequestedTarget;

	UPROPERTY()
	TObjectPtr<ARTSStructure> RequestedStructureTarget;

	UPROPERTY()
	TObjectPtr<ARTSWreckage> RequestedWreckageTarget;

	ERTSOrderFailure Failure = ERTSOrderFailure::None;

	UPROPERTY()
	TArray<FRTSUnitCommandOutcome> Outcomes;

	int32 GetAcceptedCount() const;
};
