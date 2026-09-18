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
	// Restore this contractor-owned implementation.
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void URTSUnitOrderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Restore this contractor-owned implementation.
	Super::EndPlay(EndPlayReason);
}

void URTSUnitOrderComponent::Issue(const FRTSOrderRequest& Request)
{
	// Restore this contractor-owned implementation.
}

void URTSUnitOrderComponent::Cancel()
{
	// Restore this contractor-owned implementation.
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
	// Restore this contractor-owned implementation.
}

void URTSUnitOrderComponent::StopActiveNavigation()
{
	// Restore this contractor-owned implementation.
}

void URTSUnitOrderComponent::StartMove()
{
	// Restore this contractor-owned implementation.
}

void URTSUnitOrderComponent::RetryMove()
{
	// Restore this contractor-owned implementation.
}

void URTSUnitOrderComponent::CompleteMove()
{
	// Restore this contractor-owned implementation.
}

void URTSUnitOrderComponent::FailMove(const ERTSOrderFailure Failure)
{
	// Restore this contractor-owned implementation.
}

void URTSUnitOrderComponent::UpdateAttack(const float DeltaTime)
{
	// Restore this contractor-owned implementation.
}

void URTSUnitOrderComponent::StartChase()
{
	// Restore this contractor-owned implementation.
}

void URTSUnitOrderComponent::EnterAttacking()
{
	// Restore this contractor-owned implementation.
}

void URTSUnitOrderComponent::ClearAttackTarget()
{
	// Restore this contractor-owned implementation.
}

bool URTSUnitOrderComponent::IsAttackTargetAvailable() const
{
	return (IsValid(CurrentRequest.Target) && CurrentRequest.Target->IsAlive())
		|| (IsValid(CurrentRequest.TargetStructure) && CurrentRequest.TargetStructure->IsAlive());
}

bool URTSUnitOrderComponent::IsAttackTargetInRange(const ARTSCombatUnit& Unit) const
{
	// Restore this contractor-owned implementation.
	return {};
}

FVector URTSUnitOrderComponent::GetAttackTargetLocation() const
{
	// Restore this contractor-owned implementation.
	return {};
}

FGenericTeamId URTSUnitOrderComponent::GetAttackTargetTeam() const
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSDamageResult URTSUnitOrderComponent::ApplyDamageToAttackTarget(const float Damage) const
{
	// Restore this contractor-owned implementation.
	return {};
}

void URTSUnitOrderComponent::UpdateReclaim(const float DeltaTime)
{
	// Restore this contractor-owned implementation.
}

void URTSUnitOrderComponent::StartReclaimApproach()
{
	// Restore this contractor-owned implementation.
}

void URTSUnitOrderComponent::EnterReclaiming()
{
	// Restore this contractor-owned implementation.
}

void URTSUnitOrderComponent::ClearReclaimTarget(const ERTSOrderFailure Failure)
{
	// Restore this contractor-owned implementation.
}

void URTSUnitOrderComponent::TryAcquireIdleTarget()
{
	// Restore this contractor-owned implementation.
}
