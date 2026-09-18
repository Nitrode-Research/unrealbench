// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AI/RTSAIStrategyTypes.h"
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TimerManager.h"
#include "RTSAIStrategySubsystem.generated.h"

/** Owns deterministic faction intentions and coordinates them through shared gameplay transactions. */
UCLASS()
class RTS_API URTSAIStrategySubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	FRTSAIStartResult StartFaction(FGenericTeamId TeamId, const FRTSAIProfile& Profile);
	void StopFaction(FGenericTeamId TeamId);
	FRTSAISnapshot GetSnapshot(FGenericTeamId TeamId) const;
	FRTSHQCandidateSearchResult EvaluateHeadquartersCandidates(
		const class ARTSCombatUnit& CommandVehicle,
		const FRTSAIProfile& Profile,
		TConstArrayView<int32> RetiredCandidateIndices = {}) const;
	FRTSStructureCandidateSearchResult EvaluateStructureCandidates(
		FGenericTeamId TeamId,
		ERTSStructureType StructureType,
		const FRTSAIProfile& Profile) const;
	FRTSStructureCandidateSearchResult EvaluateTurretCandidates(
		FGenericTeamId TeamId,
		const FRTSAIProfile& Profile) const;

private:
	struct FTacticalCommitment
	{
		ERTSAIAction Action = ERTSAIAction::None;
		ERTSAITacticalPhase Phase = ERTSAITacticalPhase::None;
		int64 CommitmentId = 0;
		int64 GroupCommandId = 0;
		TArray<int32> MemberStableUnitIds;
		int32 ObjectiveStableId = INDEX_NONE;
		bool bObjectiveIsStructure = false;
		FVector MusterLocation = FVector::ZeroVector;
		double StartedAtSeconds = 0.0;
	};

	struct FRuntimeState
	{
		FRTSAISnapshot Snapshot;
		FRTSAIProfile Profile;
		TMap<int32, double> RetiredCandidateExpiryByIndex;
		TMap<uint8, int32> StructurePlacementRetriesByType;
		double CommitmentStartTimeSeconds = 0.0;
		double NextStructureAttemptTimeSeconds = 0.0;
		FTacticalCommitment DefenseCommitment;
		FTacticalCommitment AttackCommitment;
		int64 ReclaimCommitmentId = 0;
		int64 ReclaimGroupCommandId = 0;
		int32 ReclaimerStableUnitId = INDEX_NONE;
		int32 ReclaimWreckageId = INDEX_NONE;
		FTimerHandle DecisionTimer;
	};

	void EvaluateFaction(uint8 TeamValue);
	bool ChooseAndIssueDeploymentMove(FRuntimeState& State, class ARTSCombatUnit& CommandVehicle);
	void UpdateDeploymentTravel(FRuntimeState& State, class ARTSCombatUnit& CommandVehicle);
	void RetireCandidateAndRetry(FRuntimeState& State, class ARTSCombatUnit& CommandVehicle);
	void MarkHeadquartersDeployed(FRuntimeState& State);
	bool EvaluateEconomyAndProduction(FRuntimeState& State);
	bool EnsureStructure(
		FRuntimeState& State,
		ERTSStructureType StructureType,
		int32 DesiredCount,
		ERTSAIAction Action);
	bool EnqueueMissingRosterUnit(FRuntimeState& State);
	bool EvaluateDefense(FRuntimeState& State);
	bool EnsureDefensiveTurrets(FRuntimeState& State);
	bool TryAssignSafeReclaim(FRuntimeState& State);
	void MaintainAttackCommitment(FRuntimeState& State);
	bool UpdateOrLaunchAttackGroup(FRuntimeState& State);
	void ReleaseCommitment(FRuntimeState& State, FTacticalCommitment& Commitment);
	void PublishCommitment(FRTSAISnapshot& Snapshot, const FTacticalCommitment& Commitment) const;
	static bool IsSupportedTeam(FGenericTeamId TeamId);

	TMap<uint8, FRuntimeState> RuntimeStatesByTeam;
	int64 NextCommitmentId = 1;
};
