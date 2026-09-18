// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Configuration/RTSMilestone3Configuration.h"
#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "Orders/RTSOrderTypes.h"
#include "Production/RTSProductionTypes.h"
#include "Structures/RTSStructureTypes.h"
#include "RTSAIStrategyTypes.generated.h"

UENUM()
enum class ERTSAIStrategyPhase : uint8
{
	Stopped,
	Observing,
	Committed
};

UENUM()
enum class ERTSAIHeadquartersPhase : uint8
{
	None,
	ChoosingSite,
	Traveling,
	ReadyToDeploy,
	Deploying,
	Retrying,
	Deployed,
	RetryLimitReached
};

UENUM()
enum class ERTSAIHeadquartersCandidateRefusal : uint8
{
	None,
	Retired,
	DeploymentRejected,
	TravelRouteUnavailable,
	ApproachRouteUnavailable
};

UENUM()
enum class ERTSAIAction : uint8
{
	None,
	DeployCommandStructure,
	RestoreMaterialIncome,
	AddPowerCapacity,
	AddSupplyCapacity,
	EstablishProduction,
	EnqueueRosterUnit,
	DefendBase,
	PlaceDefensiveTurret,
	MusterAttackGroup,
	ReclaimWreckage
};

UENUM()
enum class ERTSAITacticalPhase : uint8
{
	None,
	Mustering,
	Attacking,
	Defending,
	Reclaiming
};

UENUM()
enum class ERTSAIRefusal : uint8
{
	None,
	UnsupportedTeam,
	AlreadyStarted,
	InvalidProfile,
	MatchResolved,
	CommandVehicleUnavailable,
	StartingZoneUnavailable,
	NoLegalHeadquartersCandidate,
	MoveRejected,
	MovementFailed,
	DeploymentRejected,
	RetryLimitReached,
	HeadquartersUnavailable,
	ConstructionInProgress,
	NoLegalStructureCandidate,
	StructurePlacementRejected,
	FactoryUnavailable,
	ProductionRejected,
	DefenseForceUnavailable,
	DefenseCommandRejected,
	TurretPlacementRejected,
	AttackGroupUnavailable,
	AttackTargetUnavailable,
	AttackCommandRejected,
	ReclaimUnavailable,
	ReclaimCommandRejected
};

UENUM()
enum class ERTSAIStructureCandidateRefusal : uint8
{
	None,
	MinimumTurretSpacing
};

USTRUCT()
struct RTS_API FRTSStructureCandidateReceipt
{
	GENERATED_BODY()

	int32 CandidateIndex = INDEX_NONE;
	FVector GroundLocation = FVector::ZeroVector;
	bool bSelectable = false;
	ERTSPlacementRefusal Refusal = ERTSPlacementRefusal::InvalidTeam;
	ERTSAIStructureCandidateRefusal PolicyRefusal = ERTSAIStructureCandidateRefusal::None;
	int32 StableDepositId = INDEX_NONE;
	float DistanceScore = 0.0f;
};

USTRUCT()
struct RTS_API FRTSStructureCandidateSearchResult
{
	GENERATED_BODY()

	bool bFound = false;
	FRTSStructureCandidateReceipt Selected;
	TArray<FRTSStructureCandidateReceipt> Candidates;
};

USTRUCT()
struct RTS_API FRTSAIStructureCommitReceipt
{
	GENERATED_BODY()

	ERTSAIAction Action = ERTSAIAction::None;
	ERTSStructureType StructureType = ERTSStructureType::Headquarters;
	int32 StableStructureId = INDEX_NONE;
	int64 MaterialTransactionId = 0;
};

USTRUCT()
struct RTS_API FRTSAIProductionCommitReceipt
{
	GENERATED_BODY()

	ERTSUnitType UnitType = ERTSUnitType::InfantrySquad;
	int32 StableFactoryId = INDEX_NONE;
	int32 QueueEntryId = INDEX_NONE;
	int64 MaterialReservationTransactionId = 0;
};

USTRUCT()
struct RTS_API FRTSAICommandCommitReceipt
{
	GENERATED_BODY()

	ERTSAIAction Action = ERTSAIAction::None;
	ERTSAITacticalPhase Phase = ERTSAITacticalPhase::None;
	int64 TacticalCommitmentId = 0;
	int64 GroupCommandId = 0;
	TArray<int32> MemberStableUnitIds;
	int32 ObjectiveStableId = INDEX_NONE;
	bool bObjectiveIsStructure = false;
};

USTRUCT()
struct RTS_API FRTSAIReclaimCommitReceipt
{
	GENERATED_BODY()

	int64 TacticalCommitmentId = 0;
	int64 GroupCommandId = 0;
	int32 ReclaimerStableUnitId = INDEX_NONE;
	int32 StableWreckageId = INDEX_NONE;
};

USTRUCT()
struct RTS_API FRTSHQCandidateReceipt
{
	GENERATED_BODY()

	int32 CandidateIndex = INDEX_NONE;
	FVector GroundLocation = FVector::ZeroVector;
	bool bSelectable = false;
	ERTSAIHeadquartersCandidateRefusal Refusal = ERTSAIHeadquartersCandidateRefusal::None;
	ERTSDeploymentRefusal DeploymentRefusal = ERTSDeploymentRefusal::None;
	int32 NearbyMaterialCount = 0;
	int32 UsableBuildProbeCount = 0;
	float MaterialProximityScore = 0.0f;
	float BuildSpaceScore = 0.0f;
	float ApproachStandoffScore = 0.0f;
	float RouteAccessScore = 0.0f;
	float RouteLength = 0.0f;
	float TotalScore = 0.0f;
};

USTRUCT()
struct RTS_API FRTSHQCandidateSearchResult
{
	GENERATED_BODY()

	bool bFound = false;
	FRTSHQCandidateReceipt Selected;
	TArray<FRTSHQCandidateReceipt> Candidates;
};

USTRUCT()
struct RTS_API FRTSAISnapshot
{
	GENERATED_BODY()

	FGenericTeamId TeamId = FGenericTeamId::NoTeam;
	ERTSAIStrategyPhase Phase = ERTSAIStrategyPhase::Stopped;
	ERTSAIHeadquartersPhase HeadquartersPhase = ERTSAIHeadquartersPhase::None;
	ERTSAIAction CurrentAction = ERTSAIAction::None;
	ERTSAIRefusal LastRefusal = ERTSAIRefusal::None;
	ERTSOrderFailure LastOrderFailure = ERTSOrderFailure::None;
	ERTSDeploymentRefusal LastDeploymentRefusal = ERTSDeploymentRefusal::None;
	ERTSPlacementRefusal LastPlacementRefusal = ERTSPlacementRefusal::None;
	ERTSProductionRefusal LastProductionRefusal = ERTSProductionRefusal::None;
	ERTSProductionState ObservedProductionState = ERTSProductionState::Idle;
	int64 CommitmentId = 0;
	int64 GroupCommandId = 0;
	int32 ObjectiveStableId = INDEX_NONE;
	int32 CandidateIndex = INDEX_NONE;
	FVector ObjectiveLocation = FVector::ZeroVector;
	float Score = 0.0f;
	int32 DecisionSequence = 0;
	int32 DeploymentAttemptCount = 0;
	int32 AcceptedDeploymentCount = 0;
	int32 CandidateRetryCount = 0;
	int32 StructurePlacementAttemptCount = 0;
	int32 AcceptedStructurePlacementCount = 0;
	int32 ProductionEnqueueAttemptCount = 0;
	int32 AcceptedProductionEnqueueCount = 0;
	ERTSStructureType ObjectiveStructureType = ERTSStructureType::Headquarters;
	ERTSUnitType ObjectiveUnitType = ERTSUnitType::InfantrySquad;
	ERTSAITacticalPhase TacticalPhase = ERTSAITacticalPhase::None;
	int64 TacticalCommitmentId = 0;
	bool bObjectiveIsStructure = false;
	FVector MusterLocation = FVector::ZeroVector;
	TArray<int32> TacticalMemberStableUnitIds;
	int32 DefenseResponseCount = 0;
	int32 AttackLaunchCount = 0;
	int32 MusterTimeoutCount = 0;
	int32 AttackReleaseCount = 0;
	int32 ReclaimOrderCount = 0;
	double NextReconsiderationTimeSeconds = 0.0;
	FRTSHQCandidateReceipt Candidate;
	FRTSStructureCandidateReceipt StructureCandidate;
	TArray<FRTSAIStructureCommitReceipt> StructureCommits;
	TArray<FRTSAIProductionCommitReceipt> ProductionCommits;
	TArray<FRTSAICommandCommitReceipt> CommandCommits;
	TArray<FRTSAIReclaimCommitReceipt> ReclaimCommits;
	bool bRunning = false;
};

USTRUCT()
struct RTS_API FRTSAIStartResult
{
	GENERATED_BODY()

	bool bAccepted = false;
	ERTSAIRefusal Refusal = ERTSAIRefusal::None;
	FRTSAISnapshot Snapshot;
};
