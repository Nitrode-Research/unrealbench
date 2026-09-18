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
	FRTSCommandResult Result;
	Result.GroupCommandId = NextGroupCommandId++;
	Result.RequestedDestination = RequestedDestination;
	if (IsMatchResolved(GetWorld()))
	{
		Result.Failure = ERTSOrderFailure::MatchResolved;
		for (ARTSCombatUnit* Unit : Units)
		{
			Result.Outcomes.Add(MakeRejectedOutcome(Unit, Result.Failure));
		}
		return Result;
	}

	TArray<ARTSCombatUnit*> EligibleUnits;
	for (ARTSCombatUnit* Unit : Units)
	{
		if (!IsValid(Unit))
		{
			Result.Outcomes.Add(MakeRejectedOutcome(Unit, ERTSOrderFailure::InvalidUnit));
		}
		else if (!Unit->IsAlive())
		{
			Result.Outcomes.Add(MakeRejectedOutcome(Unit, ERTSOrderFailure::UnitUnavailable));
		}
		else if (Unit->GetWorld() != GetWorld())
		{
			Result.Outcomes.Add(MakeRejectedOutcome(Unit, ERTSOrderFailure::WrongWorld));
		}
		else if (!IsInsideCommandVehicleStartingZone(*Unit, RequestedDestination))
		{
			Unit->GetOrderComponent()->Cancel();
			Result.Outcomes.Add(MakeRejectedOutcome(Unit, ERTSOrderFailure::OutsideStartingZone));
		}
		else
		{
			EligibleUnits.AddUnique(Unit);
		}
	}
	EligibleUnits.Sort([](const ARTSCombatUnit& Left, const ARTSCombatUnit& Right)
	{
		return Left.GetStableUnitId() < Right.GetStableUnitId();
	});

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	ANavigationData* NavigationData = NavigationSystem != nullptr
		? NavigationSystem->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)
		: nullptr;
	if (NavigationSystem == nullptr || NavigationData == nullptr)
	{
		Result.Failure = ERTSOrderFailure::NavigationUnavailable;
		for (ARTSCombatUnit* Unit : EligibleUnits)
		{
			Unit->GetOrderComponent()->Cancel();
			Result.Outcomes.Add(MakeRejectedOutcome(Unit, Result.Failure));
		}
		return Result;
	}

	FNavLocation ProjectedCenter;
	if (!NavigationSystem->ProjectPointToNavigation(
		RequestedDestination,
		ProjectedCenter,
		FVector(500.0f, 500.0f, 500.0f),
		NavigationData))
	{
		Result.Failure = ERTSOrderFailure::DestinationNotNavigable;
		for (ARTSCombatUnit* Unit : EligibleUnits)
		{
			Unit->GetOrderComponent()->Cancel();
			Result.Outcomes.Add(MakeRejectedOutcome(Unit, Result.Failure));
		}
		return Result;
	}
	Result.ProjectedDestination = ProjectedCenter.Location;
	LastMoveDestination = ProjectedCenter.Location;

	if (EligibleUnits.IsEmpty())
	{
		return Result;
	}
	const FRTSMovementTuning MovementTuning = FRTSMovementTuning::Load();

	FVector GroupCentroid = FVector::ZeroVector;
	float LargestAgentRadius = 0.0f;
	for (const ARTSCombatUnit* Unit : EligibleUnits)
	{
		GroupCentroid += Unit->GetActorLocation();
		LargestAgentRadius = FMath::Max(LargestAgentRadius, Unit->GetCapsuleComponent()->GetScaledCapsuleRadius());
	}
	GroupCentroid /= EligibleUnits.Num();

	FVector TravelDirection = ProjectedCenter.Location - GroupCentroid;
	TravelDirection.Z = 0.0f;
	if (!TravelDirection.Normalize())
	{
		TravelDirection = FVector::ForwardVector;
	}
	const FVector FormationRight(-TravelDirection.Y, TravelDirection.X, 0.0f);
	const float SlotSpacing = LargestAgentRadius * 2.0f + MovementTuning.FormationGap;
	const FVector ProjectionExtent(SlotSpacing * 0.45f, SlotSpacing * 0.45f, 500.0f);
	EligibleUnits.Sort([GroupCentroid, TravelDirection](const ARTSCombatUnit& Left, const ARTSCombatUnit& Right)
	{
		const float LeftProgress = FVector::DotProduct(Left.GetActorLocation() - GroupCentroid, TravelDirection);
		const float RightProgress = FVector::DotProduct(Right.GetActorLocation() - GroupCentroid, TravelDirection);
		if (!FMath::IsNearlyEqual(LeftProgress, RightProgress, 1.0f))
		{
			return LeftProgress > RightProgress;
		}
		return Left.GetStableUnitId() < Right.GetStableUnitId();
	});

	TArray<FVector> CandidateSlots;
	for (int32 ExpansionRing = 0;
		ExpansionRing <= MovementTuning.MaximumFormationExpansionRings
			&& CandidateSlots.Num() < EligibleUnits.Num();
		++ExpansionRing)
	{
		const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(EligibleUnits.Num()))) + ExpansionRing * 2;
		const int32 Rows = FMath::CeilToInt(static_cast<float>(EligibleUnits.Num()) / Columns) + ExpansionRing * 2;
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			for (int32 Column = 0; Column < Columns; ++Column)
			{
				const float LateralOffset = (Column - (Columns - 1) * 0.5f) * SlotSpacing;
				const float LongitudinalOffset = (Row - (Rows - 1) * 0.5f) * SlotSpacing;
				const FVector Candidate = ProjectedCenter.Location
					+ FormationRight * LateralOffset
					+ TravelDirection * LongitudinalOffset;
				FNavLocation ProjectedSlot;
				if (!NavigationSystem->ProjectPointToNavigation(
					Candidate,
					ProjectedSlot,
					ProjectionExtent,
					NavigationData))
				{
					continue;
				}

				const bool bOverlapsExistingSlot = CandidateSlots.ContainsByPredicate(
					[&ProjectedSlot, SlotSpacing](const FVector& ExistingSlot)
					{
						return FVector::DistSquared2D(ExistingSlot, ProjectedSlot.Location)
							< FMath::Square(SlotSpacing * 0.9f);
					});
				if (!bOverlapsExistingSlot)
				{
					CandidateSlots.Add(ProjectedSlot.Location);
				}
			}
		}
	}

	for (ARTSCombatUnit* Unit : EligibleUnits)
	{
		int32 AssignedSlotIndex = INDEX_NONE;
		float FurthestProgress = -TNumericLimits<float>::Max();
		double NearestDistanceSquared = TNumericLimits<double>::Max();
		for (int32 SlotIndex = 0; SlotIndex < CandidateSlots.Num(); ++SlotIndex)
		{
			if (!HasCompletePath(*NavigationSystem, *NavigationData, *Unit, CandidateSlots[SlotIndex]))
			{
				continue;
			}

			const float SlotProgress = FVector::DotProduct(
				CandidateSlots[SlotIndex] - ProjectedCenter.Location,
				TravelDirection);
			const double DistanceSquared = FVector::DistSquared2D(Unit->GetActorLocation(), CandidateSlots[SlotIndex]);
			const bool bIsFurtherFormationRow = SlotProgress > FurthestProgress + SlotSpacing * 0.25f;
			const bool bIsSameFormationRow = FMath::IsNearlyEqual(
				SlotProgress,
				FurthestProgress,
				SlotSpacing * 0.25f);
			if (bIsFurtherFormationRow || (bIsSameFormationRow && DistanceSquared < NearestDistanceSquared))
			{
				FurthestProgress = SlotProgress;
				NearestDistanceSquared = DistanceSquared;
				AssignedSlotIndex = SlotIndex;
			}
		}

		if (AssignedSlotIndex == INDEX_NONE)
		{
			Unit->GetOrderComponent()->Cancel();
			Result.Outcomes.Add(MakeRejectedOutcome(Unit, ERTSOrderFailure::NoReachableSlot));
			continue;
		}

		const FVector AssignedDestination = CandidateSlots[AssignedSlotIndex];
		if (!IsInsideCommandVehicleStartingZone(*Unit, AssignedDestination))
		{
			Unit->GetOrderComponent()->Cancel();
			Result.Outcomes.Add(MakeRejectedOutcome(Unit, ERTSOrderFailure::OutsideStartingZone));
			continue;
		}
		CandidateSlots.RemoveAt(AssignedSlotIndex);
		Unit->GetOrderComponent()->Issue(FRTSOrderRequest::MakeMove(
			Result.GroupCommandId,
			AssignedDestination,
			MovementTuning.ArrivalTolerance));

		FRTSUnitCommandOutcome Outcome;
		Outcome.Unit = Unit;
		Outcome.AssignedDestination = AssignedDestination;
		Outcome.Failure = Unit->GetOrderComponent()->GetSnapshot().LastFailure;
		Outcome.bAccepted = Outcome.Failure == ERTSOrderFailure::None;
		Result.Outcomes.Add(MoveTemp(Outcome));
	}

	if (Result.GetAcceptedCount() == 0 && Result.Failure == ERTSOrderFailure::None)
	{
		Result.Failure = ERTSOrderFailure::NoReachableSlot;
	}
	if (Result.GetAcceptedCount() > 0)
	{
		CommandAccepted.Broadcast(Result);
	}
	return Result;
}

FRTSCommandResult URTSCommandSubsystem::IssueAttack(
	TConstArrayView<ARTSCombatUnit*> Units,
	ARTSCombatUnit& RequestedTarget)
{
	FRTSCommandResult Result;
	Result.GroupCommandId = NextGroupCommandId++;
	Result.RequestedTarget = &RequestedTarget;
	if (IsMatchResolved(GetWorld()))
	{
		Result.Failure = ERTSOrderFailure::MatchResolved;
		for (ARTSCombatUnit* Unit : Units)
		{
			Result.Outcomes.Add(MakeRejectedOutcome(Unit, Result.Failure));
		}
		return Result;
	}
	if (!IsValid(&RequestedTarget) || RequestedTarget.GetWorld() != GetWorld())
	{
		Result.Failure = ERTSOrderFailure::InvalidTarget;
	}
	else if (!RequestedTarget.IsAlive())
	{
		Result.Failure = ERTSOrderFailure::TargetUnavailable;
	}

	TArray<ARTSCombatUnit*> EligibleUnits;
	for (ARTSCombatUnit* Unit : Units)
	{
		ERTSOrderFailure UnitFailure = Result.Failure;
		if (!IsValid(Unit))
		{
			UnitFailure = ERTSOrderFailure::InvalidUnit;
		}
		else if (!Unit->IsAlive())
		{
			UnitFailure = ERTSOrderFailure::UnitUnavailable;
		}
		else if (Unit->GetWorld() != GetWorld())
		{
			UnitFailure = ERTSOrderFailure::WrongWorld;
		}
		else if (UnitFailure == ERTSOrderFailure::None && !AreHostile(*Unit, RequestedTarget))
		{
			UnitFailure = ERTSOrderFailure::FriendlyTarget;
		}

		if (UnitFailure != ERTSOrderFailure::None)
		{
			if (IsValid(Unit))
			{
				Unit->GetOrderComponent()->Cancel();
			}
			Result.Outcomes.Add(MakeRejectedOutcome(Unit, UnitFailure));
		}
		else if (!EligibleUnits.Contains(Unit))
		{
			EligibleUnits.Add(Unit);
		}
	}
	EligibleUnits.Sort([](const ARTSCombatUnit& Left, const ARTSCombatUnit& Right)
	{
		return Left.GetStableUnitId() < Right.GetStableUnitId();
	});

	for (ARTSCombatUnit* Unit : EligibleUnits)
	{
		Unit->GetOrderComponent()->Issue(FRTSOrderRequest::MakeAttack(
			Result.GroupCommandId,
			RequestedTarget));

		FRTSUnitCommandOutcome Outcome;
		Outcome.Unit = Unit;
		Outcome.AssignedTarget = &RequestedTarget;
		Outcome.Failure = Unit->GetOrderComponent()->GetSnapshot().LastFailure;
		Outcome.bAccepted = Outcome.Failure == ERTSOrderFailure::None;
		Result.Outcomes.Add(MoveTemp(Outcome));
	}

	if (Result.GetAcceptedCount() == 0 && Result.Failure == ERTSOrderFailure::None)
	{
		Result.Failure = Result.Outcomes.IsEmpty()
			? ERTSOrderFailure::InvalidUnit
			: Result.Outcomes[0].Failure;
	}
	if (Result.GetAcceptedCount() > 0)
	{
		CommandAccepted.Broadcast(Result);
	}
	return Result;
}

FRTSCommandResult URTSCommandSubsystem::IssueAttack(
	TConstArrayView<ARTSCombatUnit*> Units,
	ARTSStructure& RequestedTarget)
{
	FRTSCommandResult Result;
	Result.GroupCommandId = NextGroupCommandId++;
	Result.RequestedStructureTarget = &RequestedTarget;
	if (IsMatchResolved(GetWorld()))
	{
		Result.Failure = ERTSOrderFailure::MatchResolved;
	}
	else if (!IsValid(&RequestedTarget) || RequestedTarget.GetWorld() != GetWorld())
	{
		Result.Failure = ERTSOrderFailure::InvalidTarget;
	}
	else if (!RequestedTarget.IsAlive())
	{
		Result.Failure = ERTSOrderFailure::TargetUnavailable;
	}

	TArray<ARTSCombatUnit*> EligibleUnits;
	for (ARTSCombatUnit* Unit : Units)
	{
		ERTSOrderFailure UnitFailure = Result.Failure;
		if (!IsValid(Unit))
		{
			UnitFailure = ERTSOrderFailure::InvalidUnit;
		}
		else if (!Unit->IsAlive())
		{
			UnitFailure = ERTSOrderFailure::UnitUnavailable;
		}
		else if (Unit->GetWorld() != GetWorld())
		{
			UnitFailure = ERTSOrderFailure::WrongWorld;
		}
		else if (UnitFailure == ERTSOrderFailure::None && !AreHostile(*Unit, RequestedTarget))
		{
			UnitFailure = ERTSOrderFailure::FriendlyTarget;
		}

		if (UnitFailure != ERTSOrderFailure::None)
		{
			Result.Outcomes.Add(MakeRejectedOutcome(Unit, UnitFailure));
		}
		else
		{
			EligibleUnits.AddUnique(Unit);
		}
	}
	EligibleUnits.Sort([](const ARTSCombatUnit& Left, const ARTSCombatUnit& Right)
	{
		return Left.GetStableUnitId() < Right.GetStableUnitId();
	});

	for (ARTSCombatUnit* Unit : EligibleUnits)
	{
		Unit->GetOrderComponent()->Issue(FRTSOrderRequest::MakeAttack(Result.GroupCommandId, RequestedTarget));
		FRTSUnitCommandOutcome Outcome;
		Outcome.Unit = Unit;
		Outcome.AssignedStructureTarget = &RequestedTarget;
		Outcome.Failure = Unit->GetOrderComponent()->GetSnapshot().LastFailure;
		Outcome.bAccepted = Outcome.Failure == ERTSOrderFailure::None;
		Result.Outcomes.Add(MoveTemp(Outcome));
	}
	if (Result.GetAcceptedCount() == 0 && Result.Failure == ERTSOrderFailure::None)
	{
		Result.Failure = Result.Outcomes.IsEmpty()
			? ERTSOrderFailure::InvalidUnit
			: Result.Outcomes[0].Failure;
	}
	if (Result.GetAcceptedCount() > 0)
	{
		CommandAccepted.Broadcast(Result);
	}
	return Result;
}

FRTSCommandResult URTSCommandSubsystem::IssueReclaim(
	TConstArrayView<ARTSCombatUnit*> Units,
	ARTSWreckage& RequestedTarget)
{
	FRTSCommandResult Result;
	Result.GroupCommandId = NextGroupCommandId++;
	Result.RequestedWreckageTarget = &RequestedTarget;
	if (IsMatchResolved(GetWorld()))
	{
		Result.Failure = ERTSOrderFailure::MatchResolved;
	}
	else if (!IsValid(&RequestedTarget) || RequestedTarget.GetWorld() != GetWorld())
	{
		Result.Failure = ERTSOrderFailure::InvalidWreckage;
	}
	else if (!RequestedTarget.IsAvailable())
	{
		Result.Failure = ERTSOrderFailure::WreckageUnavailable;
	}

	TArray<ARTSCombatUnit*> EligibleUnits;
	for (ARTSCombatUnit* Unit : Units)
	{
		ERTSOrderFailure UnitFailure = Result.Failure;
		if (!IsValid(Unit))
		{
			UnitFailure = ERTSOrderFailure::InvalidUnit;
		}
		else if (!Unit->IsAlive())
		{
			UnitFailure = ERTSOrderFailure::UnitUnavailable;
		}
		else if (Unit->GetWorld() != GetWorld())
		{
			UnitFailure = ERTSOrderFailure::WrongWorld;
		}
		else if (Unit->GetReclaimComponent() == nullptr)
		{
			UnitFailure = ERTSOrderFailure::NotReclaimer;
		}
		if (UnitFailure != ERTSOrderFailure::None)
		{
			Result.Outcomes.Add(MakeRejectedOutcome(Unit, UnitFailure));
		}
		else
		{
			EligibleUnits.AddUnique(Unit);
		}
	}
	EligibleUnits.Sort([](const ARTSCombatUnit& Left, const ARTSCombatUnit& Right)
	{
		return Left.GetStableUnitId() < Right.GetStableUnitId();
	});

	for (ARTSCombatUnit* Unit : EligibleUnits)
	{
		Unit->GetOrderComponent()->Issue(FRTSOrderRequest::MakeReclaim(Result.GroupCommandId, RequestedTarget));
		FRTSUnitCommandOutcome Outcome;
		Outcome.Unit = Unit;
		Outcome.AssignedWreckageTarget = &RequestedTarget;
		Outcome.Failure = Unit->GetOrderComponent()->GetSnapshot().LastFailure;
		Outcome.bAccepted = Outcome.Failure == ERTSOrderFailure::None;
		Result.Outcomes.Add(MoveTemp(Outcome));
	}
	if (Result.GetAcceptedCount() == 0 && Result.Failure == ERTSOrderFailure::None)
	{
		Result.Failure = Result.Outcomes.IsEmpty()
			? ERTSOrderFailure::InvalidUnit
			: Result.Outcomes[0].Failure;
	}
	if (Result.GetAcceptedCount() > 0)
	{
		CommandAccepted.Broadcast(Result);
	}
	return Result;
}

TOptional<FVector> URTSCommandSubsystem::GetLastMoveDestination() const
{
	return LastMoveDestination;
}

FRTSCommandAccepted& URTSCommandSubsystem::OnCommandAccepted()
{
	return CommandAccepted;
}
