// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/RTSAIStrategySubsystem.h"

#include "Configuration/RTSMilestone2Configuration.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Match/RTSMatchSubsystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Orders/RTSCommandSubsystem.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Production/RTSProductionSubsystem.h"
#include "Reclaim/RTSWreckage.h"
#include "Reclaim/RTSWreckageSubsystem.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSUnitRegistrySubsystem.h"
#include "Units/RTSTeams.h"

namespace
{
struct FRTSRouteMeasurement
{
	bool bComplete = false;
	float Length = 0.0f;
};

FRTSRouteMeasurement MeasureCompleteRoute(
	UWorld& World,
	const FVector& Start,
	const FVector& End)
{
	FRTSRouteMeasurement Measurement;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
	ANavigationData* NavigationData = Navigation != nullptr
		? Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)
		: nullptr;
	if (Navigation == nullptr || NavigationData == nullptr)
	{
		return Measurement;
	}
	FNavLocation ProjectedStart;
	FNavLocation ProjectedEnd;
	const FVector ProjectionExtent(500.0f, 500.0f, 500.0f);
	if (!Navigation->ProjectPointToNavigation(Start, ProjectedStart, ProjectionExtent, NavigationData)
		|| !Navigation->ProjectPointToNavigation(End, ProjectedEnd, ProjectionExtent, NavigationData))
	{
		return Measurement;
	}
	FPathFindingQuery Query(nullptr, *NavigationData, ProjectedStart.Location, ProjectedEnd.Location);
	Query.SetAllowPartialPaths(false);
	const FPathFindingResult PathResult = Navigation->FindPathSync(Query);
	if (!PathResult.IsSuccessful() || PathResult.IsPartial() || !PathResult.Path.IsValid())
	{
		return Measurement;
	}
	Measurement.bComplete = true;
	Measurement.Length = PathResult.Path->GetLength();
	return Measurement;
}

bool HasCompleteRouteWithinRange(
	UWorld& World,
	const FVector& Start,
	const FVector& Target,
	const float AcceptanceRange)
{
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
	ANavigationData* NavigationData = Navigation != nullptr
		? Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)
		: nullptr;
	if (Navigation == nullptr || NavigationData == nullptr)
	{
		return false;
	}
	FNavLocation ProjectedStart;
	FNavLocation ProjectedTarget;
	if (!Navigation->ProjectPointToNavigation(
			Start,
			ProjectedStart,
			FVector(500.0f, 500.0f, 500.0f),
			NavigationData)
		|| !Navigation->ProjectPointToNavigation(
			Target,
			ProjectedTarget,
			FVector(AcceptanceRange, AcceptanceRange, 500.0f),
			NavigationData)
		|| FVector::DistSquared2D(ProjectedTarget.Location, Target) > FMath::Square(AcceptanceRange))
	{
		return false;
	}
	FPathFindingQuery Query(nullptr, *NavigationData, ProjectedStart.Location, ProjectedTarget.Location);
	Query.SetAllowPartialPaths(false);
	const FPathFindingResult PathResult = Navigation->FindPathSync(Query);
	return PathResult.IsSuccessful() && !PathResult.IsPartial();
}
}

void URTSAIStrategySubsystem::Deinitialize()
{
	// Restore this contractor-owned implementation.
	Super::Deinitialize();
}

FRTSAIStartResult URTSAIStrategySubsystem::StartFaction(
	const FGenericTeamId TeamId,
	const FRTSAIProfile& Profile)
{
	// Restore this contractor-owned implementation.
	return {};
}

void URTSAIStrategySubsystem::StopFaction(const FGenericTeamId TeamId)
{
	// Restore this contractor-owned implementation.
}

FRTSAISnapshot URTSAIStrategySubsystem::GetSnapshot(const FGenericTeamId TeamId) const
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSHQCandidateSearchResult URTSAIStrategySubsystem::EvaluateHeadquartersCandidates(
	const ARTSCombatUnit& CommandVehicle,
	const FRTSAIProfile& Profile,
	const TConstArrayView<int32> RetiredCandidateIndices) const
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSStructureCandidateSearchResult URTSAIStrategySubsystem::EvaluateStructureCandidates(
	const FGenericTeamId TeamId,
	const ERTSStructureType StructureType,
	const FRTSAIProfile& Profile) const
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSStructureCandidateSearchResult URTSAIStrategySubsystem::EvaluateTurretCandidates(
	const FGenericTeamId TeamId,
	const FRTSAIProfile& Profile) const
{
	// Restore this contractor-owned implementation.
	return {};
}

void URTSAIStrategySubsystem::EvaluateFaction(const uint8 TeamValue)
{
	// Restore this contractor-owned implementation.
}

bool URTSAIStrategySubsystem::ChooseAndIssueDeploymentMove(
	FRuntimeState& State,
	ARTSCombatUnit& CommandVehicle)
{
	// Restore this contractor-owned implementation.
	return {};
}

void URTSAIStrategySubsystem::UpdateDeploymentTravel(
	FRuntimeState& State,
	ARTSCombatUnit& CommandVehicle)
{
	// Restore this contractor-owned implementation.
}

void URTSAIStrategySubsystem::RetireCandidateAndRetry(
	FRuntimeState& State,
	ARTSCombatUnit& CommandVehicle)
{
	// Restore this contractor-owned implementation.
}

void URTSAIStrategySubsystem::MarkHeadquartersDeployed(FRuntimeState& State)
{
	// Restore this contractor-owned implementation.
}

bool URTSAIStrategySubsystem::EvaluateEconomyAndProduction(FRuntimeState& State)
{
	// Restore this contractor-owned implementation.
	return {};
}

bool URTSAIStrategySubsystem::EnsureStructure(
	FRuntimeState& State,
	const ERTSStructureType StructureType,
	const int32 DesiredCount,
	const ERTSAIAction Action)
{
	// Restore this contractor-owned implementation.
	return {};
}

bool URTSAIStrategySubsystem::EnqueueMissingRosterUnit(FRuntimeState& State)
{
	// Restore this contractor-owned implementation.
	return {};
}

bool URTSAIStrategySubsystem::EvaluateDefense(FRuntimeState& State)
{
	// Restore this contractor-owned implementation.
	return {};
}

bool URTSAIStrategySubsystem::EnsureDefensiveTurrets(FRuntimeState& State)
{
	// Restore this contractor-owned implementation.
	return {};
}

bool URTSAIStrategySubsystem::TryAssignSafeReclaim(FRuntimeState& State)
{
	// Restore this contractor-owned implementation.
	return {};
}

void URTSAIStrategySubsystem::MaintainAttackCommitment(FRuntimeState& State)
{
	// Restore this contractor-owned implementation.
}

bool URTSAIStrategySubsystem::UpdateOrLaunchAttackGroup(FRuntimeState& State)
{
	// Restore this contractor-owned implementation.
	return {};
}

void URTSAIStrategySubsystem::ReleaseCommitment(
	FRuntimeState& State,
	FTacticalCommitment& Commitment)
{
	// Restore this contractor-owned implementation.
}

void URTSAIStrategySubsystem::PublishCommitment(
	FRTSAISnapshot& Snapshot,
	const FTacticalCommitment& Commitment) const
{
	// Restore this contractor-owned implementation.
}

bool URTSAIStrategySubsystem::IsSupportedTeam(const FGenericTeamId TeamId)
{
	return TeamId == RTSTeams::Player || TeamId == RTSTeams::Enemy;
}
