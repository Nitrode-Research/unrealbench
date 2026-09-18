// Copyright Epic Games, Inc. All Rights Reserved.

#include "Orders/RTSCommandSubsystem.h"

#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "EngineUtils.h"
#include "Match/RTSMatchSubsystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Reclaim/RTSReclaimComponent.h"
#include "Reclaim/RTSWreckage.h"
#include "Structures/RTSStructure.h"
#include "Units/RTSCombatUnit.h"
#include "World/RTSStartingZone.h"

namespace
{
FRTSUnitCommandOutcome MakeRejectedOutcome(
	ARTSCombatUnit* Unit,
	const ERTSOrderFailure Failure)
{
	FRTSUnitCommandOutcome Outcome;
	Outcome.Unit = Unit;
	Outcome.Failure = Failure;
	return Outcome;
}

bool HasCompletePath(
	UNavigationSystemV1& NavigationSystem,
	const ANavigationData& NavigationData,
	const ARTSCombatUnit& Unit,
	const FVector& Destination)
{
	FPathFindingQuery Query(
		Unit.GetController(),
		NavigationData,
		Unit.GetActorLocation(),
		Destination);
	Query.SetAllowPartialPaths(false);
	const FPathFindingResult PathResult = NavigationSystem.FindPathSync(MoveTemp(Query));
	return PathResult.IsSuccessful() && !PathResult.IsPartial();
}

bool AreHostile(const ARTSCombatUnit& Source, const ARTSCombatUnit& Target)
{
	return Source.GetGenericTeamId() != FGenericTeamId::NoTeam
		&& Target.GetGenericTeamId() != FGenericTeamId::NoTeam
		&& Source.GetGenericTeamId() != Target.GetGenericTeamId();
}

bool AreHostile(const ARTSCombatUnit& Source, const ARTSStructure& Target)
{
	return Source.GetGenericTeamId() != FGenericTeamId::NoTeam
		&& Target.GetGenericTeamId() != FGenericTeamId::NoTeam
		&& Source.GetGenericTeamId() != Target.GetGenericTeamId();
}

bool IsMatchResolved(const UWorld* World)
{
	const URTSMatchSubsystem* Match = World != nullptr
		? World->GetSubsystem<URTSMatchSubsystem>()
		: nullptr;
	return Match != nullptr && Match->IsResolved();
}

bool IsInsideCommandVehicleStartingZone(const ARTSCombatUnit& Unit, const FVector& Destination)
{
	if (!Unit.IsMilestone2Unit() || Unit.GetUnitType() != ERTSUnitType::CommandVehicle)
	{
		return true;
	}
	for (TActorIterator<ARTSStartingZone> ZoneIterator(Unit.GetWorld()); ZoneIterator; ++ZoneIterator)
	{
		if (ZoneIterator->GetGenericTeamId() != Unit.GetGenericTeamId())
		{
			continue;
		}
		return FVector::Dist2D(Destination, ZoneIterator->GetActorLocation())
			+ Unit.GetCapsuleComponent()->GetScaledCapsuleRadius()
			<= ZoneIterator->GetRadius();
	}
	return false;
}
}

FRTSCommandResult URTSCommandSubsystem::IssueMove(
	TConstArrayView<ARTSCombatUnit*> Units,
	const FVector& RequestedDestination)
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSCommandResult URTSCommandSubsystem::IssueAttack(
	TConstArrayView<ARTSCombatUnit*> Units,
	ARTSCombatUnit& RequestedTarget)
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSCommandResult URTSCommandSubsystem::IssueAttack(
	TConstArrayView<ARTSCombatUnit*> Units,
	ARTSStructure& RequestedTarget)
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSCommandResult URTSCommandSubsystem::IssueReclaim(
	TConstArrayView<ARTSCombatUnit*> Units,
	ARTSWreckage& RequestedTarget)
{
	// Restore this contractor-owned implementation.
	return {};
}

TOptional<FVector> URTSCommandSubsystem::GetLastMoveDestination() const
{
	return LastMoveDestination;
}

FRTSCommandAccepted& URTSCommandSubsystem::OnCommandAccepted()
{
	return CommandAccepted;
}
