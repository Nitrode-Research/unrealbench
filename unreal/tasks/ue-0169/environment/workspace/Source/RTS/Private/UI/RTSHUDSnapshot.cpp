// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/RTSHUDSnapshot.h"

#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSTurretCombatComponent.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Game/RTSPlayerController.h"
#include "Match/RTSMatchSubsystem.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Production/RTSProductionComponent.h"
#include "Reclaim/RTSReclaimComponent.h"
#include "Reclaim/RTSWreckage.h"
#include "Selection/RTSSelectionSubsystem.h"
#include "Structures/RTSDeploymentViewSubsystem.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "UI/RTSPresentationText.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"

namespace
{
int32 HealthPercent(const FRTSHealthSnapshot& Health)
{
	return Health.MaximumHealth > 0.0f
		? FMath::RoundToInt(Health.CurrentHealth / Health.MaximumHealth * 100.0f)
		: 0;
}

const TCHAR* OrderPhaseText(const ERTSOrderPhase Phase)
{
	switch (Phase)
	{
	case ERTSOrderPhase::Moving: return TEXT("MOVING");
	case ERTSOrderPhase::Chasing: return TEXT("CHASING");
	case ERTSOrderPhase::Attacking: return TEXT("ATTACKING");
	case ERTSOrderPhase::Approaching: return TEXT("APPROACHING WRECK");
	case ERTSOrderPhase::Reclaiming: return TEXT("RECLAIMING");
	case ERTSOrderPhase::Idle:
	default: return TEXT("IDLE");
	}
}

const TCHAR* ProductionStateText(const ERTSProductionState State)
{
	switch (State)
	{
	case ERTSProductionState::UnderConstruction: return TEXT("UNDER CONSTRUCTION");
	case ERTSProductionState::Producing: return TEXT("PRODUCING");
	case ERTSProductionState::PausedPower: return TEXT("PAUSED - LOW POWER");
	case ERTSProductionState::BlockedExit: return TEXT("BLOCKED EXIT");
	case ERTSProductionState::Idle:
	default: return TEXT("IDLE");
	}
}

const TCHAR* ProductionRefusalText(const ERTSProductionRefusal Refusal)
{
	switch (Refusal)
	{
	case ERTSProductionRefusal::FactoryUnavailable: return TEXT("FACTORY UNAVAILABLE");
	case ERTSProductionRefusal::WrongTeam: return TEXT("WRONG FACTORY OWNER");
	case ERTSProductionRefusal::NotFactory: return TEXT("SELECTION IS NOT A FACTORY");
	case ERTSProductionRefusal::InvalidUnitType: return TEXT("INVALID UNIT TYPE");
	case ERTSProductionRefusal::QueueFull: return TEXT("FACTORY QUEUE FULL");
	case ERTSProductionRefusal::EconomyRejected: return TEXT("INSUFFICIENT MATERIALS OR SUPPLY");
	case ERTSProductionRefusal::MatchResolved: return TEXT("MATCH COMPLETE");
	case ERTSProductionRefusal::None:
	default: return TEXT("");
	}
}

const TCHAR* PlacementRefusalText(const ERTSPlacementRefusal Refusal)
{
	switch (Refusal)
	{
	case ERTSPlacementRefusal::InvalidTeam: return TEXT("INVALID TEAM");
	case ERTSPlacementRefusal::HeadquartersRequired: return TEXT("DEPLOY HQ FIRST");
	case ERTSPlacementRefusal::HeadquartersCannotBePlaced: return TEXT("USE COMMAND VEHICLE TO DEPLOY HQ");
	case ERTSPlacementRefusal::OutsideBuildArea: return TEXT("OUTSIDE BUILD AREA");
	case ERTSPlacementRefusal::InvalidGround: return TEXT("INVALID GROUND");
	case ERTSPlacementRefusal::FootprintBlocked: return TEXT("FOOTPRINT BLOCKED");
	case ERTSPlacementRefusal::DepositRequired: return TEXT("EXTRACTOR REQUIRES DEPOSIT");
	case ERTSPlacementRefusal::DepositClaimed: return TEXT("DEPOSIT ALREADY CLAIMED");
	case ERTSPlacementRefusal::FactoryExitBlocked: return TEXT("FACTORY EXIT BLOCKED");
	case ERTSPlacementRefusal::EconomyRejected: return TEXT("INSUFFICIENT MATERIALS");
	case ERTSPlacementRefusal::SpawnFailed: return TEXT("PLACEMENT FAILED");
	case ERTSPlacementRefusal::MatchResolved: return TEXT("MATCH COMPLETE");
	case ERTSPlacementRefusal::None:
	default: return TEXT("NONE");
	}
}

const TCHAR* DeploymentRefusalText(const ERTSDeploymentRefusal Refusal)
{
	switch (Refusal)
	{
	case ERTSDeploymentRefusal::WrongUnitType: return TEXT("WRONG UNIT TYPE");
	case ERTSDeploymentRefusal::NoStartingZone: return TEXT("NO STARTING ZONE");
	case ERTSDeploymentRefusal::OutsideStartingZone: return TEXT("OUTSIDE STARTING ZONE");
	case ERTSDeploymentRefusal::InvalidGround: return TEXT("INVALID GROUND");
	case ERTSDeploymentRefusal::FootprintBlocked: return TEXT("FOOTPRINT BLOCKED");
	case ERTSDeploymentRefusal::HeadquartersAlreadyDeployed: return TEXT("HQ ALREADY DEPLOYED");
	case ERTSDeploymentRefusal::EconomyRejected: return TEXT("ECONOMY REJECTED");
	case ERTSDeploymentRefusal::CommandIdentityRejected: return TEXT("COMMAND TRANSFER REJECTED");
	case ERTSDeploymentRefusal::SpawnFailed: return TEXT("HQ SPAWN FAILED");
	case ERTSDeploymentRefusal::MatchResolved: return TEXT("MATCH COMPLETE");
	case ERTSDeploymentRefusal::InvalidCommandVehicle: return TEXT("INVALID COMMAND VEHICLE");
	case ERTSDeploymentRefusal::None:
	default: return TEXT("NONE");
	}
}

const TCHAR* ReclaimRefusalText(const ERTSReclaimRefusal Refusal)
{
	switch (Refusal)
	{
	case ERTSReclaimRefusal::InvalidReclaimer: return TEXT("INVALID RECLAIMER");
	case ERTSReclaimRefusal::ReclaimerUnavailable: return TEXT("RECLAIMER UNAVAILABLE");
	case ERTSReclaimRefusal::NotInfantry: return TEXT("ONLY INFANTRY CAN RECLAIM");
	case ERTSReclaimRefusal::InvalidWreckage: return TEXT("INVALID WRECKAGE");
	case ERTSReclaimRefusal::WreckageUnavailable: return TEXT("WRECKAGE UNAVAILABLE");
	case ERTSReclaimRefusal::WrongWorld: return TEXT("WRECKAGE IS IN ANOTHER WORLD");
	case ERTSReclaimRefusal::AlreadyLeased: return TEXT("WRECKAGE ALREADY CLAIMED");
	case ERTSReclaimRefusal::OutOfRange: return TEXT("MOVE CLOSER TO RECLAIM");
	case ERTSReclaimRefusal::EconomyRejected: return TEXT("RECLAIM PAYOUT REJECTED");
	case ERTSReclaimRefusal::MatchResolved: return TEXT("MATCH COMPLETE");
	case ERTSReclaimRefusal::None:
	default: return TEXT("");
	}
}

const TCHAR* OrderFailureText(const ERTSOrderFailure Failure)
{
	switch (Failure)
	{
	case ERTSOrderFailure::NavigationUnavailable: return TEXT("NAVIGATION UNAVAILABLE");
	case ERTSOrderFailure::DestinationNotNavigable: return TEXT("DESTINATION NOT NAVIGABLE");
	case ERTSOrderFailure::OutsideStartingZone: return TEXT("OUTSIDE STARTING ZONE");
	case ERTSOrderFailure::NoReachableSlot: return TEXT("NO REACHABLE FORMATION SLOT");
	case ERTSOrderFailure::MoveRequestRejected: return TEXT("MOVE REQUEST REJECTED");
	case ERTSOrderFailure::PathFollowingFailed: return TEXT("PATH FOLLOWING FAILED");
	case ERTSOrderFailure::InvalidTarget: return TEXT("INVALID TARGET");
	case ERTSOrderFailure::TargetUnavailable: return TEXT("TARGET UNAVAILABLE");
	case ERTSOrderFailure::FriendlyTarget: return TEXT("CANNOT ATTACK FRIENDLY TARGET");
	case ERTSOrderFailure::InvalidWreckage: return TEXT("INVALID WRECKAGE");
	case ERTSOrderFailure::WreckageUnavailable: return TEXT("WRECKAGE UNAVAILABLE");
	case ERTSOrderFailure::NotReclaimer: return TEXT("ONLY INFANTRY CAN RECLAIM");
	case ERTSOrderFailure::ReclaimContended: return TEXT("WRECKAGE ALREADY CLAIMED");
	case ERTSOrderFailure::ReclaimInterrupted: return TEXT("RECLAIM INTERRUPTED");
	case ERTSOrderFailure::MatchResolved: return TEXT("MATCH COMPLETE");
	case ERTSOrderFailure::InvalidUnit: return TEXT("INVALID UNIT");
	case ERTSOrderFailure::UnitUnavailable: return TEXT("UNIT UNAVAILABLE");
	case ERTSOrderFailure::WrongWorld: return TEXT("TARGET IS IN ANOTHER WORLD");
	case ERTSOrderFailure::None:
	default: return TEXT("");
	}
}

FString TargetText(const FRTSOrderSnapshot& Order)
{
	if (Order.Kind == ERTSOrderKind::Attack && IsValid(Order.Target))
	{
		return FString::Printf(TEXT("TARGET U%d"), Order.Target->GetStableUnitId());
	}
	if (Order.Kind == ERTSOrderKind::Attack && IsValid(Order.TargetStructure))
	{
		return FString::Printf(TEXT("TARGET S%d"), Order.TargetStructure->GetStableStructureId());
	}
	if (Order.Kind == ERTSOrderKind::Reclaim && IsValid(Order.WreckageTarget))
	{
		return FString::Printf(TEXT("TARGET W%d"), Order.WreckageTarget->GetSnapshot().StableWreckageId);
	}
	return FString();
}

FString FormatDuration(const double DurationSeconds)
{
	const int32 TotalSeconds = FMath::Max(0, FMath::RoundToInt(DurationSeconds));
	return FString::Printf(TEXT("MATCH TIME  %02d:%02d"), TotalSeconds / 60, TotalSeconds % 60);
}

void AddAction(
	FRTSHUDSnapshot& Snapshot,
	const ERTSHUDActionKind Kind,
	const TCHAR* Hotkey,
	const TCHAR* Label)
{
	Snapshot.Actions.Add({Kind, Hotkey, Label});
}
}

FRTSHUDSnapshot FRTSHUDSnapshotAdapter::Capture(const ARTSPlayerController* PlayerController)
{
	// Restore this contractor-owned implementation.
	return {};
}
