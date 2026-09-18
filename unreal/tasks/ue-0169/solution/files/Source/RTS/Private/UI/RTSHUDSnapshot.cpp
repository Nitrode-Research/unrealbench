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
	FRTSHUDSnapshot Result;
	Result.MaterialsText = TEXT("MATERIALS  --");
	Result.PowerText = TEXT("POWER  -- / --");
	Result.SupplyText = TEXT("SUPPLY  -- / --");
	Result.SelectionTitle = TEXT("NO SELECTION");
	Result.AvailableActionsText = TEXT("DRAG TO SELECT UNITS OR STRUCTURES");
	if (!IsValid(PlayerController))
	{
		return Result;
	}

	const UWorld* World = PlayerController->GetWorld();
	const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	if (World == nullptr || LocalPlayer == nullptr)
	{
		return Result;
	}
	Result.bAvailable = true;

	if (const URTSEconomySubsystem* Economy = World->GetSubsystem<URTSEconomySubsystem>())
	{
		Result.Economy = Economy->GetSnapshot(RTSTeams::Player);
		Result.MaterialsText = FString::Printf(TEXT("MATERIALS  %d"), Result.Economy.Materials);
		Result.PowerText = FString::Printf(
			TEXT("POWER  %d GENERATED / %d REQUIRED"),
			Result.Economy.PowerGeneration,
			Result.Economy.PowerDemand);
		Result.SupplyText = FString::Printf(
			TEXT("SUPPLY  %d USED + %d QUEUED / %d CAP"),
			Result.Economy.SupplyUsed,
			Result.Economy.SupplyReserved,
			Result.Economy.SupplyCapacity);
	}

	if (const URTSMatchSubsystem* Match = World->GetSubsystem<URTSMatchSubsystem>())
	{
		Result.Match = Match->GetSnapshot();
		if (Result.Match.State == ERTSMatchState::Resolved)
		{
			Result.bPlayerVictory = Result.Match.WinningTeamId == RTSTeams::Player;
			Result.ResultTitle = Result.bPlayerVictory ? TEXT("VICTORY") : TEXT("DEFEAT");
			const bool bHeadquartersDestroyed = Result.Match.Resolution == ERTSMatchResolution::HeadquartersDestroyed;
			Result.ResultReason = FString::Printf(
				TEXT("%s %s DESTROYED"),
				Result.bPlayerVictory ? TEXT("ENEMY") : TEXT("YOUR"),
				bHeadquartersDestroyed ? TEXT("HEADQUARTERS") : TEXT("COMMAND VEHICLE"));
			Result.ResultDuration = FormatDuration(Result.Match.ActiveDurationSeconds);
			Result.PlayerResultStatistics = FString::Printf(
				TEXT("YOU     PRODUCED %d     LOST %d     RECLAIMED %d MATERIALS"),
				Result.Match.PlayerStatistics.UnitsProduced,
				Result.Match.PlayerStatistics.UnitsLost,
				Result.Match.PlayerStatistics.MaterialsReclaimed);
			Result.EnemyResultStatistics = FString::Printf(
				TEXT("ENEMY     PRODUCED %d     LOST %d     RECLAIMED %d MATERIALS"),
				Result.Match.EnemyStatistics.UnitsProduced,
				Result.Match.EnemyStatistics.UnitsLost,
				Result.Match.EnemyStatistics.MaterialsReclaimed);
		}
	}

	const URTSSelectionSubsystem* Selection = LocalPlayer->GetSubsystem<URTSSelectionSubsystem>();
	const TConstArrayView<ARTSCombatUnit*> Units = Selection != nullptr
		? Selection->GetLivingUnits()
		: TConstArrayView<ARTSCombatUnit*>();
	const TConstArrayView<ARTSStructure*> Structures = Selection != nullptr
		? Selection->GetLivingStructures()
		: TConstArrayView<ARTSStructure*>();
	Result.SelectedUnitCount = Units.Num();
	Result.SelectedStructureCount = Structures.Num();

	if (Units.Num() == 1 && Structures.IsEmpty() && IsValid(Units[0]))
	{
		const ARTSCombatUnit* Unit = Units[0];
		Result.SelectionMode = ERTSHUDSelectionMode::SingleUnit;
		Result.SelectedStableUnitId = Unit->GetStableUnitId();
		Result.SelectedUnitType = Unit->GetUnitType();
		Result.SelectedHealth = Unit->GetHealthComponent()->GetSnapshot();
		Result.SelectedOrder = Unit->GetOrderComponent()->GetSnapshot();
		Result.SelectionTitle = FString::Printf(
			TEXT("%s  U%d     HP %d%%"),
			RTSPresentationText::UnitTypeLabel(Result.SelectedUnitType),
			Result.SelectedStableUnitId,
			HealthPercent(Result.SelectedHealth));
		const FString Target = TargetText(Result.SelectedOrder);
		Result.SelectionDetail = Target.IsEmpty()
			? OrderPhaseText(Result.SelectedOrder.Phase)
			: FString::Printf(TEXT("%s     %s"), OrderPhaseText(Result.SelectedOrder.Phase), *Target);
		if (const URTSReclaimComponent* Reclaim = Unit->GetReclaimComponent())
		{
			Result.Reclaim = Reclaim->GetSnapshot();
			if (IsValid(Result.Reclaim->Target))
			{
				const FRTSWreckageSnapshot Wreckage = Result.Reclaim->Target->GetSnapshot();
				Result.SelectionDetail = FString::Printf(
					TEXT("%s     W%d VALUE %d     PROGRESS %d%%"),
					OrderPhaseText(Result.SelectedOrder.Phase),
					Wreckage.StableWreckageId,
					Wreckage.ReclaimMaterialValue,
					FMath::RoundToInt(Result.Reclaim->Progress * 100.0f));
			}
		}
		if (Result.Match.State == ERTSMatchState::Resolved)
		{
			Result.AvailableActionsText = TEXT("MATCH COMPLETE");
		}
		else if (Result.SelectedUnitType == ERTSUnitType::CommandVehicle)
		{
			Result.AvailableActionsText = TEXT("AVAILABLE     MOVE     DEPLOY HQ [E]");
			AddAction(Result, ERTSHUDActionKind::Move, TEXT("RMB"), TEXT("MOVE"));
			AddAction(Result, ERTSHUDActionKind::DeployHeadquarters, TEXT("E"), TEXT("DEPLOY HQ"));
		}
		else if (Result.SelectedUnitType == ERTSUnitType::InfantrySquad)
		{
			Result.AvailableActionsText = TEXT("AVAILABLE     MOVE     ATTACK     RECLAIM");
			AddAction(Result, ERTSHUDActionKind::Move, TEXT("RMB"), TEXT("MOVE"));
			AddAction(Result, ERTSHUDActionKind::Attack, TEXT("RMB"), TEXT("ATTACK"));
			AddAction(Result, ERTSHUDActionKind::Reclaim, TEXT("RMB"), TEXT("RECLAIM"));
		}
		else
		{
			Result.AvailableActionsText = TEXT("AVAILABLE     MOVE     ATTACK");
			AddAction(Result, ERTSHUDActionKind::Move, TEXT("RMB"), TEXT("MOVE"));
			AddAction(Result, ERTSHUDActionKind::Attack, TEXT("RMB"), TEXT("ATTACK"));
		}
	}
	else if (Units.IsEmpty() && Structures.Num() == 1 && IsValid(Structures[0]))
	{
		const ARTSStructure* Structure = Structures[0];
		Result.SelectionMode = ERTSHUDSelectionMode::SingleStructure;
		Result.SelectedStableStructureId = Structure->GetStableStructureId();
		Result.SelectedStructureType = Structure->GetStructureType();
		Result.SelectedHealth = Structure->GetHealthComponent()->GetSnapshot();
		Result.SelectionTitle = FString::Printf(
			TEXT("%s  S%d     HP %d%%"),
			RTSPresentationText::StructureTypeLabel(Result.SelectedStructureType),
			Result.SelectedStableStructureId,
			HealthPercent(Result.SelectedHealth));
		if (!Structure->IsConstructed())
		{
			Result.SelectionDetail = FString::Printf(
				TEXT("CONSTRUCTION     %d%%"),
				FMath::RoundToInt(Structure->GetConstructionProgress() * 100.0f));
			Result.AvailableActionsText = Structure->GetProductionComponent() != nullptr
				? TEXT("PRODUCTION LOCKED UNTIL CONSTRUCTION COMPLETES")
				: TEXT("CONSTRUCTION IN PROGRESS");
		}
		else if (const URTSProductionComponent* Production = Structure->GetProductionComponent())
		{
			Result.Production = Production->GetSnapshot();
			Result.SelectionDetail = ProductionStateText(Result.Production->State);
			Result.AvailableActionsText = TEXT("PRODUCE     [Z] INFANTRY     [X] LIGHT     [C] HEAVY");
			AddAction(Result, ERTSHUDActionKind::ProduceInfantry, TEXT("Z"), TEXT("INFANTRY"));
			AddAction(Result, ERTSHUDActionKind::ProduceLightVehicle, TEXT("X"), TEXT("LIGHT"));
			AddAction(Result, ERTSHUDActionKind::ProduceHeavyVehicle, TEXT("C"), TEXT("HEAVY"));
			if (!Result.Production->Queue.IsEmpty())
			{
				TArray<FString> Entries;
				Entries.Reserve(Result.Production->Queue.Num());
				for (int32 EntryIndex = 0; EntryIndex < Result.Production->Queue.Num(); ++EntryIndex)
				{
					const FRTSProductionQueueEntrySnapshot& Entry = Result.Production->Queue[EntryIndex];
					Entries.Add(FString::Printf(
						TEXT("%d %s %d%%"),
						EntryIndex + 1,
						RTSPresentationText::UnitTypeLabel(Entry.UnitType),
						FMath::RoundToInt(Entry.Progress * 100.0f)));
				}
				FString QueueEntriesText;
				for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
				{
					if (EntryIndex > 0)
					{
						QueueEntriesText += EntryIndex == 3 ? TEXT("\n") : TEXT("    ");
					}
					QueueEntriesText += Entries[EntryIndex];
				}
				Result.QueueText = FString::Printf(
					TEXT("QUEUE %d / 5\n%s"),
					Result.Production->Queue.Num(),
					*QueueEntriesText);
			}
		}
		else if (const URTSTurretCombatComponent* Turret = Structure->GetTurretCombatComponent())
		{
			Result.Turret = Turret->GetSnapshot();
			const TCHAR* StateText = Result.Turret->State == ERTSTurretState::Unpowered
				? TEXT("UNPOWERED")
				: Result.Turret->State == ERTSTurretState::Firing ? TEXT("FIRING") : TEXT("SEARCHING");
			Result.SelectionDetail = Result.Turret->TargetStableUnitId == INDEX_NONE
				? StateText
				: FString::Printf(TEXT("%s     TARGET U%d"), StateText, Result.Turret->TargetStableUnitId);
			Result.AvailableActionsText = TEXT("AUTOMATIC DEFENSE");
		}
		else
		{
			Result.SelectionDetail = TEXT("OPERATIONAL");
			Result.AvailableActionsText = TEXT("NO DIRECT ACTIONS");
		}
	}
	else if (!Units.IsEmpty() || !Structures.IsEmpty())
	{
		Result.SelectionMode = ERTSHUDSelectionMode::Multiple;
		Result.SelectionTitle = FString::Printf(
			TEXT("SELECTION     %d UNITS     %d STRUCTURES"),
			Units.Num(),
			Structures.Num());
		int32 IdleCount = 0;
		int32 MovingCount = 0;
		int32 AttackingCount = 0;
		int32 ReclaimingCount = 0;
		bool bHasInfantry = false;
		for (const ARTSCombatUnit* Unit : Units)
		{
			if (!IsValid(Unit))
			{
				continue;
			}
			bHasInfantry |= Unit->GetUnitType() == ERTSUnitType::InfantrySquad;
			switch (Unit->GetOrderComponent()->GetSnapshot().Phase)
			{
			case ERTSOrderPhase::Moving:
			case ERTSOrderPhase::Chasing: ++MovingCount; break;
			case ERTSOrderPhase::Attacking: ++AttackingCount; break;
			case ERTSOrderPhase::Approaching:
			case ERTSOrderPhase::Reclaiming: ++ReclaimingCount; break;
			case ERTSOrderPhase::Idle:
			default: ++IdleCount; break;
			}
		}
		Result.SelectionDetail = FString::Printf(
			TEXT("IDLE %d     MOVING %d     ATTACKING %d     RECLAIMING %d"),
			IdleCount,
			MovingCount,
			AttackingCount,
			ReclaimingCount);
		Result.AvailableActionsText = Units.IsEmpty()
			? TEXT("NO SHARED DIRECT ACTION")
			: TEXT("AVAILABLE     MOVE     ATTACK     RECLAIM FILTERS TO INFANTRY");
		if (!Units.IsEmpty())
		{
			AddAction(Result, ERTSHUDActionKind::Move, TEXT("RMB"), TEXT("MOVE"));
			AddAction(Result, ERTSHUDActionKind::Attack, TEXT("RMB"), TEXT("ATTACK"));
			if (bHasInfantry)
			{
				AddAction(Result, ERTSHUDActionKind::Reclaim, TEXT("RMB"), TEXT("RECLAIM"));
			}
		}
	}

	const URTSDeploymentViewSubsystem* View = LocalPlayer->GetSubsystem<URTSDeploymentViewSubsystem>();
	const URTSStructureSubsystem* StructureSystem = World->GetSubsystem<URTSStructureSubsystem>();
	if (View != nullptr && StructureSystem != nullptr)
	{
		if (const TOptional<FRTSPlacementRequest> Request = View->GetPlacementRequest(); Request.IsSet())
		{
			const FRTSPlacementPreview Preview = StructureSystem->EvaluatePlacement(Request.GetValue());
			Result.ContextTone = Preview.bValid ? ERTSHUDMessageTone::Positive : ERTSHUDMessageTone::Critical;
			Result.ContextText = Preview.bValid
				? FString::Printf(
					TEXT("PLACE %s     LEFT CLICK TO COMMIT"),
					RTSPresentationText::StructureTypeLabel(Preview.StructureType))
				: FString::Printf(
					TEXT("%s BLOCKED     %s"),
					RTSPresentationText::StructureTypeLabel(Preview.StructureType),
					PlacementRefusalText(Preview.Refusal));
		}
		else if (ARTSCombatUnit* CommandVehicle = View->GetCommandVehicle(); IsValid(CommandVehicle))
		{
			const FRTSHeadquartersDeploymentPreview Preview = StructureSystem->EvaluateHeadquartersDeployment(*CommandVehicle);
			Result.ContextTone = Preview.bValid ? ERTSHUDMessageTone::Positive : ERTSHUDMessageTone::Critical;
			Result.ContextText = Preview.bValid
				? TEXT("HQ DEPLOYMENT VALID     LEFT CLICK TO COMMIT")
				: FString::Printf(TEXT("HQ DEPLOYMENT BLOCKED     %s"), DeploymentRefusalText(Preview.Refusal));
		}
		else if (const TOptional<FRTSPlacementResult> Placement = View->GetLastPlacementResult(); Placement.IsSet())
		{
			Result.ContextTone = Placement->bAccepted ? ERTSHUDMessageTone::Positive : ERTSHUDMessageTone::Critical;
			Result.ContextText = Placement->bAccepted
				? TEXT("STRUCTURE PLACED     CONSTRUCTION STARTED")
				: FString::Printf(TEXT("PLACEMENT REFUSED     %s"), PlacementRefusalText(Placement->Refusal));
		}
		else if (const TOptional<FRTSHeadquartersDeploymentResult> Deployment = View->GetLastResult(); Deployment.IsSet())
		{
			Result.ContextTone = Deployment->bAccepted ? ERTSHUDMessageTone::Positive : ERTSHUDMessageTone::Critical;
			Result.ContextText = Deployment->bAccepted
				? TEXT("HEADQUARTERS DEPLOYED")
				: FString::Printf(TEXT("HEADQUARTERS DEPLOYMENT REFUSED     %s"), DeploymentRefusalText(Deployment->Refusal));
		}
	}

	if (Result.Economy.IsInBrownout())
	{
		Result.WarningTone = ERTSHUDMessageTone::Critical;
		Result.WarningText = FString::Printf(
			TEXT("LOW POWER     ADD %d GENERATION     FACTORIES AND TURRETS PAUSED"),
			Result.Economy.PowerDemand - Result.Economy.PowerGeneration);
	}
	else if (Result.Economy.SupplyCapacity > 0
		&& Result.Economy.SupplyUsed + Result.Economy.SupplyReserved >= Result.Economy.SupplyCapacity)
	{
		Result.WarningTone = ERTSHUDMessageTone::Warning;
		Result.WarningText = TEXT("SUPPLY CAP REACHED     BUILD A SUPPLY DEPOT");
	}
	else if (Result.Production.IsSet() && Result.Production->State == ERTSProductionState::BlockedExit)
	{
		Result.WarningTone = ERTSHUDMessageTone::Critical;
		Result.WarningText = TEXT("FACTORY EXIT BLOCKED     CLEAR THE EXIT AREA");
	}
	else if (Result.Production.IsSet() && Result.Production->LastRefusal != ERTSProductionRefusal::None)
	{
		Result.WarningTone = ERTSHUDMessageTone::Warning;
		Result.WarningText = ProductionRefusalText(Result.Production->LastRefusal);
	}
	else if (Result.Reclaim.IsSet() && Result.Reclaim->LastRefusal != ERTSReclaimRefusal::None)
	{
		Result.WarningTone = ERTSHUDMessageTone::Warning;
		Result.WarningText = ReclaimRefusalText(Result.Reclaim->LastRefusal);
	}
	else if (Result.SelectedOrder.LastFailure != ERTSOrderFailure::None)
	{
		Result.WarningTone = ERTSHUDMessageTone::Warning;
		Result.WarningText = OrderFailureText(Result.SelectedOrder.LastFailure);
	}

	return Result;
}
