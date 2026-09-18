// Copyright Epic Games, Inc. All Rights Reserved.

#include "Orders/RTSUnitOrderComponent.h"

#include "AIController.h"
#include "Combat/RTSCombatTypes.h"
#include "Combat/RTSHealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Match/RTSMatchSubsystem.h"
#include "NavigationSystem.h"
#include "Reclaim/RTSReclaimComponent.h"
#include "Reclaim/RTSWreckage.h"
#include "Structures/RTSStructure.h"
#include "TimerManager.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"

namespace
{
ERTSOrderFailure ReclaimFailureToOrderFailure(const ERTSReclaimRefusal Refusal)
{
	switch (Refusal)
	{
	case ERTSReclaimRefusal::NotInfantry: return ERTSOrderFailure::NotReclaimer;
	case ERTSReclaimRefusal::AlreadyLeased: return ERTSOrderFailure::ReclaimContended;
	case ERTSReclaimRefusal::MatchResolved: return ERTSOrderFailure::MatchResolved;
	case ERTSReclaimRefusal::WreckageUnavailable: return ERTSOrderFailure::WreckageUnavailable;
	case ERTSReclaimRefusal::OutOfRange: return ERTSOrderFailure::ReclaimInterrupted;
	case ERTSReclaimRefusal::InvalidWreckage:
	case ERTSReclaimRefusal::WrongWorld: return ERTSOrderFailure::InvalidWreckage;
	case ERTSReclaimRefusal::InvalidReclaimer:
	case ERTSReclaimRefusal::ReclaimerUnavailable: return ERTSOrderFailure::UnitUnavailable;
	case ERTSReclaimRefusal::EconomyRejected: return ERTSOrderFailure::ReclaimInterrupted;
	case ERTSReclaimRefusal::None:
	default: return ERTSOrderFailure::None;
	}
}
}

URTSUnitOrderComponent::URTSUnitOrderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = 0.05f;
}

void URTSUnitOrderComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner());
	if (Unit == nullptr || !Unit->IsAlive())
	{
		return;
	}

	if (CurrentRequest.Kind == ERTSOrderKind::Attack)
	{
		UpdateAttack(DeltaTime);
	}
	else if (CurrentRequest.Kind == ERTSOrderKind::Reclaim)
	{
		UpdateReclaim(DeltaTime);
	}
	else if (CurrentRequest.Kind == ERTSOrderKind::None && Snapshot.Phase == ERTSOrderPhase::Idle)
	{
		TryAcquireIdleTarget();
	}
}

void URTSUnitOrderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopActiveNavigation();
	Super::EndPlay(EndPlayReason);
}

void URTSUnitOrderComponent::Issue(const FRTSOrderRequest& Request)
{
	StopActiveNavigation();
	if (ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner()))
	{
		if (URTSReclaimComponent* Reclaim = Unit->GetReclaimComponent())
		{
			Reclaim->Interrupt();
		}
	}
	CurrentRequest = Request;
	RetryCount = 0;
	Snapshot.Kind = Request.Kind;
	Snapshot.GroupCommandId = Request.GroupCommandId;
	Snapshot.Destination = Request.Destination;
	Snapshot.Target = Request.Target;
	Snapshot.TargetStructure = Request.TargetStructure;
	Snapshot.WreckageTarget = Request.WreckageTarget;
	Snapshot.bAutomatic = Request.bAutomatic;
	Snapshot.LastAttackTimeSeconds = -1.0;
	Snapshot.LastFailure = ERTSOrderFailure::None;

	if (Request.Kind == ERTSOrderKind::Move)
	{
		Snapshot.Phase = ERTSOrderPhase::Moving;
		OrderChanged.Broadcast();
		StartMove();
		return;
	}

	if (Request.Kind == ERTSOrderKind::Attack && IsAttackTargetAvailable())
	{
		const ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner());
		if (Unit != nullptr && IsAttackTargetInRange(*Unit))
		{
			EnterAttacking();
		}
		else
		{
			StartChase();
		}
		return;
	}

	if (Request.Kind == ERTSOrderKind::Reclaim && IsValid(Request.WreckageTarget))
	{
		ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner());
		URTSReclaimComponent* Reclaim = Unit != nullptr ? Unit->GetReclaimComponent() : nullptr;
		if (Reclaim == nullptr)
		{
			CurrentRequest = {};
			Snapshot.Phase = ERTSOrderPhase::Idle;
			Snapshot.LastFailure = ERTSOrderFailure::NotReclaimer;
			OrderChanged.Broadcast();
			return;
		}
		const FRTSReclaimResult BeginResult = Reclaim->Begin(*Request.WreckageTarget);
		if (!BeginResult.bAccepted)
		{
			CurrentRequest = {};
			Snapshot.Phase = ERTSOrderPhase::Idle;
			Snapshot.LastFailure = ReclaimFailureToOrderFailure(BeginResult.Refusal);
			OrderChanged.Broadcast();
			return;
		}
		if (FVector::DistSquared2D(Unit->GetActorLocation(), Request.WreckageTarget->GetActorLocation())
			<= FMath::Square(FRTSMilestone2Configuration::Load().Reclaim.Range))
		{
			EnterReclaiming();
		}
		else
		{
			StartReclaimApproach();
		}
		return;
	}

	Snapshot.Phase = ERTSOrderPhase::Idle;
	Snapshot.LastFailure = ERTSOrderFailure::InvalidTarget;
	OrderChanged.Broadcast();
}

void URTSUnitOrderComponent::Cancel()
{
	StopActiveNavigation();
	if (ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner()))
	{
		if (URTSReclaimComponent* Reclaim = Unit->GetReclaimComponent())
		{
			Reclaim->Interrupt();
		}
	}
	CurrentRequest = {};
	Snapshot.Kind = ERTSOrderKind::None;
	Snapshot.Phase = ERTSOrderPhase::Idle;
	Snapshot.GroupCommandId = 0;
	Snapshot.Destination = FVector::ZeroVector;
	Snapshot.Target = nullptr;
	Snapshot.TargetStructure = nullptr;
	Snapshot.WreckageTarget = nullptr;
	Snapshot.bAutomatic = false;
	Snapshot.LastAttackTimeSeconds = -1.0;
	Snapshot.LastFailure = ERTSOrderFailure::None;
	RetryCount = 0;
	OrderChanged.Broadcast();
}

FRTSOrderSnapshot URTSUnitOrderComponent::GetSnapshot() const
{
	return Snapshot;
}

FRTSOrderChanged& URTSUnitOrderComponent::OnOrderChanged()
{
	return OrderChanged;
}

FRTSUnitWeaponFired& URTSUnitOrderComponent::OnWeaponFired()
{
	return WeaponFired;
}

void URTSUnitOrderComponent::HandleMoveCompleted(
	const FAIRequestID RequestId,
	const FPathFollowingResult& Result)
{
	if (RequestId != ActiveMoveRequestId
		|| (Snapshot.Phase != ERTSOrderPhase::Moving
			&& Snapshot.Phase != ERTSOrderPhase::Chasing
			&& Snapshot.Phase != ERTSOrderPhase::Approaching))
	{
		return;
	}

	ActiveMoveRequestId = FAIRequestID::InvalidRequest;
	if (Snapshot.Phase == ERTSOrderPhase::Chasing)
	{
		UpdateAttack(0.0f);
		return;
	}
	if (Snapshot.Phase == ERTSOrderPhase::Approaching)
	{
		UpdateReclaim(0.0f);
		return;
	}

	if (Result.IsSuccess())
	{
		CompleteMove();
		return;
	}

	if (RetryCount == 0 && GetWorld() != nullptr)
	{
		++RetryCount;
		GetWorld()->GetTimerManager().SetTimer(
			RetryTimer,
			this,
			&URTSUnitOrderComponent::RetryMove,
			FRTSMovementTuning::Load().RetryDelaySeconds,
			false);
		return;
	}

	FailMove(ERTSOrderFailure::PathFollowingFailed);
}

void URTSUnitOrderComponent::StopActiveNavigation()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RetryTimer);
	}

	const FAIRequestID RequestToStop = ActiveMoveRequestId;
	ActiveMoveRequestId = FAIRequestID::InvalidRequest;
	if (RequestToStop.IsValid())
	{
		const APawn* OwnerPawn = Cast<APawn>(GetOwner());
		if (AAIController* Controller = OwnerPawn != nullptr
			? Cast<AAIController>(OwnerPawn->GetController())
			: nullptr)
		{
			Controller->StopMovement();
		}
	}
}

void URTSUnitOrderComponent::StartMove()
{
	const ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner());
	AAIController* Controller = Unit != nullptr ? Cast<AAIController>(Unit->GetController()) : nullptr;
	if (Unit == nullptr || !Unit->IsAlive() || Controller == nullptr)
	{
		FailMove(ERTSOrderFailure::MoveRequestRejected);
		return;
	}

	FAIMoveRequest MoveRequest(CurrentRequest.Destination);
	MoveRequest.SetAcceptanceRadius(CurrentRequest.AcceptanceRadius)
		.SetUsePathfinding(true)
		.SetAllowPartialPath(false)
		.SetRequireNavigableEndLocation(true)
		.SetProjectGoalLocation(false)
		.SetCanStrafe(true)
		.SetReachTestIncludesAgentRadius(false)
		.SetReachTestIncludesGoalRadius(false);

	const FPathFollowingRequestResult MoveResult = Controller->MoveTo(MoveRequest);
	if (MoveResult.Code == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		CompleteMove();
	}
	else if (MoveResult.Code == EPathFollowingRequestResult::RequestSuccessful)
	{
		ActiveMoveRequestId = MoveResult.MoveId;
	}
	else if (RetryCount == 0 && GetWorld() != nullptr)
	{
		++RetryCount;
		GetWorld()->GetTimerManager().SetTimer(
			RetryTimer,
			this,
			&URTSUnitOrderComponent::RetryMove,
			FRTSMovementTuning::Load().RetryDelaySeconds,
			false);
	}
	else
	{
		FailMove(ERTSOrderFailure::MoveRequestRejected);
	}
}

void URTSUnitOrderComponent::RetryMove()
{
	if (Snapshot.Phase == ERTSOrderPhase::Moving)
	{
		StartMove();
	}
}

void URTSUnitOrderComponent::CompleteMove()
{
	CurrentRequest = {};
	Snapshot.Phase = ERTSOrderPhase::Idle;
	Snapshot.LastFailure = ERTSOrderFailure::None;
	OrderChanged.Broadcast();
}

void URTSUnitOrderComponent::FailMove(const ERTSOrderFailure Failure)
{
	ActiveMoveRequestId = FAIRequestID::InvalidRequest;
	Snapshot.Phase = ERTSOrderPhase::Idle;
	Snapshot.LastFailure = Failure;
	OrderChanged.Broadcast();
}

void URTSUnitOrderComponent::UpdateAttack(const float DeltaTime)
{
	ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner());
	if (Unit == nullptr
		|| !IsAttackTargetAvailable()
		|| Unit->GetGenericTeamId() == FGenericTeamId::NoTeam
		|| GetAttackTargetTeam() == FGenericTeamId::NoTeam
		|| Unit->GetGenericTeamId() == GetAttackTargetTeam())
	{
		ClearAttackTarget();
		return;
	}

	const FRTSCombatTuning CombatTuning = FRTSCombatTuning::Load();
	FVector ToTarget = GetAttackTargetLocation() - Unit->GetActorLocation();
	ToTarget.Z = 0.0f;
	if (!IsAttackTargetInRange(*Unit))
	{
		if (Snapshot.Phase != ERTSOrderPhase::Chasing || !ActiveMoveRequestId.IsValid())
		{
			StartChase();
		}
		return;
	}

	EnterAttacking();
	if (!ToTarget.IsNearlyZero())
	{
		const FRotator DesiredRotation = ToTarget.Rotation();
		Unit->SetActorRotation(FMath::RInterpConstantTo(
			Unit->GetActorRotation(),
			DesiredRotation,
			DeltaTime,
			CombatTuning.FacingDegreesPerSecond));
	}

	UWorld* World = GetWorld();
	if (World == nullptr || World->GetTimeSeconds() + UE_DOUBLE_SMALL_NUMBER < NextAllowedAttackTimeSeconds)
	{
		return;
	}

	const bool bTargetsUnit = IsValid(CurrentRequest.Target);
	const int32 TargetStableId = bTargetsUnit
		? CurrentRequest.Target->GetStableUnitId()
		: IsValid(CurrentRequest.TargetStructure)
			? CurrentRequest.TargetStructure->GetStableStructureId()
			: INDEX_NONE;
	const FVector TargetWorldLocation = GetAttackTargetLocation();
	const FRTSDamageResult DamageResult = ApplyDamageToAttackTarget(Unit->GetAttackDamage());
	if (DamageResult.bAccepted)
	{
		FRTSWeaponEvent WeaponEvent;
		WeaponEvent.SourceKind = ERTSWeaponSourceKind::Unit;
		WeaponEvent.SourceTeamId = Unit->GetGenericTeamId();
		WeaponEvent.StableSourceId = Unit->GetStableUnitId();
		WeaponEvent.SourceShotSequence = NextWeaponEventSequence++;
		WeaponEvent.SourceWorldLocation = Unit->GetActorLocation();
		WeaponEvent.TargetWorldLocation = TargetWorldLocation;
		WeaponEvent.AppliedDamage = DamageResult.AppliedDamage;
		WeaponEvent.bDestroyedTarget = DamageResult.bKilled;
		WeaponEvent.TargetKind = bTargetsUnit
			? ERTSWeaponTargetKind::Unit
			: ERTSWeaponTargetKind::Structure;
		WeaponEvent.StableTargetId = TargetStableId;
		WeaponFired.Broadcast(WeaponEvent);
		Snapshot.LastAttackTimeSeconds = World->GetTimeSeconds();
		NextAllowedAttackTimeSeconds = World->GetTimeSeconds() + Unit->GetWeaponCooldownSeconds();
		OrderChanged.Broadcast();
	}
}

void URTSUnitOrderComponent::StartChase()
{
	ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner());
	AActor* Target = IsValid(CurrentRequest.Target)
		? static_cast<AActor*>(CurrentRequest.Target.Get())
		: static_cast<AActor*>(CurrentRequest.TargetStructure.Get());
	AAIController* Controller = Unit != nullptr ? Cast<AAIController>(Unit->GetController()) : nullptr;
	if (Unit == nullptr || Target == nullptr || Controller == nullptr)
	{
		ClearAttackTarget();
		return;
	}

	Snapshot.Phase = ERTSOrderPhase::Chasing;
	OrderChanged.Broadcast();
	FAIMoveRequest MoveRequest;
	if (IsValid(CurrentRequest.TargetStructure))
	{
		// A structure's origin is inside its navigation-blocking footprint. Approach the
		// closest navigable perimeter point so a legal structure attack can actually begin.
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		FNavLocation ProjectedApproach;
		const FVector ClosestFootprintPoint = CurrentRequest.TargetStructure
			->GetComponentsBoundingBox()
			.GetClosestPointTo(Unit->GetActorLocation());
		const float SearchRadius = FMath::Max(Unit->GetAttackRange(), 100.0f);
		if (Navigation == nullptr
			|| !Navigation->ProjectPointToNavigation(
				ClosestFootprintPoint,
				ProjectedApproach,
				FVector(SearchRadius, SearchRadius, 500.0f))
			|| FVector::DistSquared2D(
				ProjectedApproach.Location,
				CurrentRequest.TargetStructure
					->GetComponentsBoundingBox()
					.GetClosestPointTo(ProjectedApproach.Location))
				> FMath::Square(Unit->GetAttackRange()))
		{
			Snapshot.Phase = ERTSOrderPhase::Idle;
			Snapshot.LastFailure = ERTSOrderFailure::MoveRequestRejected;
			OrderChanged.Broadcast();
			return;
		}
		MoveRequest.SetGoalLocation(ProjectedApproach.Location);
		MoveRequest.SetAcceptanceRadius(FRTSMovementTuning::Load().ArrivalTolerance);
	}
	else
	{
		MoveRequest.SetGoalActor(Target);
		MoveRequest.SetAcceptanceRadius(Unit->GetAttackRange() * 0.85f);
	}
	MoveRequest
		.SetUsePathfinding(true)
		.SetAllowPartialPath(false)
		.SetRequireNavigableEndLocation(true)
		.SetCanStrafe(true)
		.SetReachTestIncludesAgentRadius(false)
		.SetReachTestIncludesGoalRadius(false);

	const FPathFollowingRequestResult MoveResult = Controller->MoveTo(MoveRequest);
	if (MoveResult.Code == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		EnterAttacking();
	}
	else if (MoveResult.Code == EPathFollowingRequestResult::RequestSuccessful)
	{
		ActiveMoveRequestId = MoveResult.MoveId;
	}
	else
	{
		Snapshot.Phase = ERTSOrderPhase::Idle;
		Snapshot.LastFailure = ERTSOrderFailure::MoveRequestRejected;
		OrderChanged.Broadcast();
	}
}

void URTSUnitOrderComponent::EnterAttacking()
{
	const bool bPhaseChanged = Snapshot.Phase != ERTSOrderPhase::Attacking;
	if (bPhaseChanged)
	{
		StopActiveNavigation();
		Snapshot.Phase = ERTSOrderPhase::Attacking;
		OrderChanged.Broadcast();
	}
}

void URTSUnitOrderComponent::ClearAttackTarget()
{
	StopActiveNavigation();
	CurrentRequest = {};
	Snapshot.Phase = ERTSOrderPhase::Idle;
	Snapshot.Target = nullptr;
	Snapshot.TargetStructure = nullptr;
	Snapshot.bAutomatic = false;
	Snapshot.LastFailure = ERTSOrderFailure::None;
	OrderChanged.Broadcast();
}

bool URTSUnitOrderComponent::IsAttackTargetAvailable() const
{
	return (IsValid(CurrentRequest.Target) && CurrentRequest.Target->IsAlive())
		|| (IsValid(CurrentRequest.TargetStructure) && CurrentRequest.TargetStructure->IsAlive());
}

bool URTSUnitOrderComponent::IsAttackTargetInRange(const ARTSCombatUnit& Unit) const
{
	if (IsValid(CurrentRequest.TargetStructure))
	{
		const FVector ClosestPoint = CurrentRequest.TargetStructure
			->GetComponentsBoundingBox()
			.GetClosestPointTo(Unit.GetActorLocation());
		return FVector::DistSquared2D(Unit.GetActorLocation(), ClosestPoint)
			<= FMath::Square(Unit.GetAttackRange());
	}
	return FVector::DistSquared2D(Unit.GetActorLocation(), GetAttackTargetLocation())
		<= FMath::Square(Unit.GetAttackRange());
}

FVector URTSUnitOrderComponent::GetAttackTargetLocation() const
{
	if (IsValid(CurrentRequest.Target))
	{
		return CurrentRequest.Target->GetActorLocation();
	}
	return IsValid(CurrentRequest.TargetStructure)
		? CurrentRequest.TargetStructure->GetActorLocation()
		: FVector::ZeroVector;
}

FGenericTeamId URTSUnitOrderComponent::GetAttackTargetTeam() const
{
	if (IsValid(CurrentRequest.Target))
	{
		return CurrentRequest.Target->GetGenericTeamId();
	}
	return IsValid(CurrentRequest.TargetStructure)
		? CurrentRequest.TargetStructure->GetGenericTeamId()
		: FGenericTeamId::NoTeam;
}

FRTSDamageResult URTSUnitOrderComponent::ApplyDamageToAttackTarget(const float Damage) const
{
	if (IsValid(CurrentRequest.Target))
	{
		return CurrentRequest.Target->GetHealthComponent()->ApplyDamage(Damage);
	}
	return IsValid(CurrentRequest.TargetStructure)
		? CurrentRequest.TargetStructure->ApplyDamage(Damage)
		: FRTSDamageResult();
}

void URTSUnitOrderComponent::UpdateReclaim(const float DeltaTime)
{
	ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner());
	URTSReclaimComponent* Reclaim = Unit != nullptr ? Unit->GetReclaimComponent() : nullptr;
	ARTSWreckage* Wreckage = CurrentRequest.WreckageTarget;
	if (Unit == nullptr || Reclaim == nullptr || !IsValid(Wreckage) || !Wreckage->IsAvailable())
	{
		ClearReclaimTarget(ERTSOrderFailure::WreckageUnavailable);
		return;
	}

	const float ReclaimRange = FRTSMilestone2Configuration::Load().Reclaim.Range;
	if (FVector::DistSquared2D(Unit->GetActorLocation(), Wreckage->GetActorLocation())
		> FMath::Square(ReclaimRange))
	{
		if (Snapshot.Phase == ERTSOrderPhase::Reclaiming)
		{
			ClearReclaimTarget(ERTSOrderFailure::ReclaimInterrupted);
		}
		else if (Snapshot.Phase != ERTSOrderPhase::Approaching || !ActiveMoveRequestId.IsValid())
		{
			StartReclaimApproach();
		}
		return;
	}

	EnterReclaiming();
	const FRTSReclaimResult AdvanceResult = Reclaim->Advance(DeltaTime);
	if (AdvanceResult.bCompleted)
	{
		CurrentRequest = {};
		Snapshot.Phase = ERTSOrderPhase::Idle;
		Snapshot.WreckageTarget = nullptr;
		Snapshot.LastFailure = ERTSOrderFailure::None;
		OrderChanged.Broadcast();
	}
	else if (!AdvanceResult.bAccepted)
	{
		ClearReclaimTarget(ReclaimFailureToOrderFailure(AdvanceResult.Refusal));
	}
}

void URTSUnitOrderComponent::StartReclaimApproach()
{
	ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner());
	ARTSWreckage* Wreckage = CurrentRequest.WreckageTarget;
	AAIController* Controller = Unit != nullptr ? Cast<AAIController>(Unit->GetController()) : nullptr;
	if (Unit == nullptr || !IsValid(Wreckage) || Controller == nullptr)
	{
		ClearReclaimTarget(ERTSOrderFailure::MoveRequestRejected);
		return;
	}
	if (URTSReclaimComponent* Reclaim = Unit->GetReclaimComponent())
	{
		Reclaim->MarkApproaching();
	}
	Snapshot.Phase = ERTSOrderPhase::Approaching;
	OrderChanged.Broadcast();
	FAIMoveRequest MoveRequest(Wreckage);
	MoveRequest.SetAcceptanceRadius(FRTSMilestone2Configuration::Load().Reclaim.Range * 0.85f)
		.SetUsePathfinding(true)
		.SetAllowPartialPath(false)
		.SetRequireNavigableEndLocation(true)
		.SetCanStrafe(true)
		.SetReachTestIncludesAgentRadius(false)
		.SetReachTestIncludesGoalRadius(false);
	const FPathFollowingRequestResult MoveResult = Controller->MoveTo(MoveRequest);
	if (MoveResult.Code == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		EnterReclaiming();
	}
	else if (MoveResult.Code == EPathFollowingRequestResult::RequestSuccessful)
	{
		ActiveMoveRequestId = MoveResult.MoveId;
	}
	else
	{
		ClearReclaimTarget(ERTSOrderFailure::MoveRequestRejected);
	}
}

void URTSUnitOrderComponent::EnterReclaiming()
{
	if (Snapshot.Phase != ERTSOrderPhase::Reclaiming)
	{
		StopActiveNavigation();
		Snapshot.Phase = ERTSOrderPhase::Reclaiming;
		OrderChanged.Broadcast();
	}
}

void URTSUnitOrderComponent::ClearReclaimTarget(const ERTSOrderFailure Failure)
{
	StopActiveNavigation();
	if (ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner()))
	{
		if (URTSReclaimComponent* Reclaim = Unit->GetReclaimComponent())
		{
			Reclaim->Interrupt(Failure == ERTSOrderFailure::ReclaimInterrupted
				? ERTSReclaimRefusal::OutOfRange
				: ERTSReclaimRefusal::WreckageUnavailable);
		}
	}
	CurrentRequest = {};
	Snapshot.Phase = ERTSOrderPhase::Idle;
	Snapshot.WreckageTarget = nullptr;
	Snapshot.LastFailure = Failure;
	OrderChanged.Broadcast();
}

void URTSUnitOrderComponent::TryAcquireIdleTarget()
{
	ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner());
	UWorld* World = GetWorld();
	if (Unit == nullptr || World == nullptr || Unit->GetGenericTeamId() == FGenericTeamId::NoTeam)
	{
		return;
	}

	const double MaximumDistanceSquared = FMath::Square(
		static_cast<double>(Unit->GetAcquisitionRange()));
	ARTSCombatUnit* BestTarget = nullptr;
	double BestDistanceSquared = MaximumDistanceSquared;
	for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
	{
		ARTSCombatUnit* Candidate = *UnitIterator;
		if (Candidate == Unit
			|| !Candidate->IsAlive()
			|| Candidate->GetGenericTeamId() == FGenericTeamId::NoTeam
			|| Candidate->GetGenericTeamId() == Unit->GetGenericTeamId())
		{
			continue;
		}

		const double DistanceSquared = FVector::DistSquared2D(
			Unit->GetActorLocation(),
			Candidate->GetActorLocation());
		const bool bCloser = DistanceSquared < BestDistanceSquared - 1.0;
		const bool bStableTie = FMath::IsNearlyEqual(DistanceSquared, BestDistanceSquared, 1.0)
			&& (BestTarget == nullptr || Candidate->GetStableUnitId() < BestTarget->GetStableUnitId());
		if (bCloser || bStableTie)
		{
			BestTarget = Candidate;
			BestDistanceSquared = DistanceSquared;
		}
	}

	if (BestTarget != nullptr)
	{
		Issue(FRTSOrderRequest::MakeAttack(0, *BestTarget, true));
	}
}
