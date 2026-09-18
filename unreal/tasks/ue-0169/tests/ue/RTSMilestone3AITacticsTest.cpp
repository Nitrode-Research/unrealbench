// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/RTSAIStrategySubsystem.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSTurretCombatComponent.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "Configuration/RTSMilestone3Configuration.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "Match/RTSMatchSubsystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Reclaim/RTSWreckage.h"
#include "Reclaim/RTSWreckageSubsystem.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSUnitRegistrySubsystem.h"
#include "Units/RTSTeams.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RTSMilestone3AITacticsTestPrivate
{
constexpr float TestTimeDilation = 5.0f;

UWorld* FindPIEWorld()
{
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE)
		{
			return Context.World();
		}
	}
	return nullptr;
}

ARTSCombatUnit* FindEnemyCommandVehicle(UWorld& World)
{
	URTSUnitRegistrySubsystem* Units = World.GetSubsystem<URTSUnitRegistrySubsystem>();
	const TArray<FRTSUnitSnapshot> CommandVehicles = Units != nullptr
		? Units->Query(RTSTeams::Enemy, ERTSUnitType::CommandVehicle)
		: TArray<FRTSUnitSnapshot>();
	return !CommandVehicles.IsEmpty() && Units != nullptr
		? Units->FindActor(CommandVehicles[0].StableUnitId)
		: nullptr;
}

bool StartEnemy(FAutomationTestBase& Test, UWorld& World, const FRTSAIProfile& Profile)
{
	URTSAIStrategySubsystem* Strategy = World.GetSubsystem<URTSAIStrategySubsystem>();
	ARTSCombatUnit* CommandVehicle = FindEnemyCommandVehicle(World);
	if (!Test.TestNotNull(TEXT("The strategy subsystem exists"), Strategy)
		|| !Test.TestNotNull(TEXT("The enemy Command Vehicle exists"), CommandVehicle))
	{
		return false;
	}
	Strategy->StopFaction(RTSTeams::Enemy);
	CommandVehicle->GetOrderComponent()->Cancel();
	World.GetWorldSettings()->SetTimeDilation(TestTimeDilation);
	return Test.TestTrue(
		TEXT("The fast tactical strategy starts"),
		Strategy->StartFaction(RTSTeams::Enemy, Profile).bAccepted);
}

FRTSAIProfile MakeNonAttackingProfile()
{
	FRTSAIProfile Profile = FRTSMilestone3Configuration::Load().FastTest;
	Profile.MinimumAttackGroupSize = 100;
	return Profile;
}

void RestoreTimeDilation(UWorld* World)
{
	if (World != nullptr)
	{
		World->GetWorldSettings()->SetTimeDilation(1.0f);
	}
}

double MeasureCompleteRouteLength(
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
		return -1.0;
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
			NavigationData))
	{
		return -1.0;
	}
	FPathFindingQuery Query(
		nullptr,
		*NavigationData,
		ProjectedStart.Location,
		ProjectedTarget.Location);
	Query.SetAllowPartialPaths(false);
	const FPathFindingResult PathResult = Navigation->FindPathSync(Query);
	return PathResult.IsSuccessful() && !PathResult.IsPartial() && PathResult.Path.IsValid()
		? PathResult.Path->GetLength()
		: -1.0;
}

bool HasConstructed(
	const URTSStructureSubsystem& Structures,
	const ERTSStructureType StructureType,
	const int32 MinimumCount = 1)
{
	return Structures.Query(RTSTeams::Enemy, StructureType).FilterByPredicate(
		[](const FRTSStructureSnapshot& Structure)
		{
			return Structure.bAlive && Structure.bConstructed;
		}).Num() >= MinimumCount;
}

bool HasReadyTacticalBase(UWorld& World, const FRTSAIProfile& Profile)
{
	const URTSStructureSubsystem* Structures = World.GetSubsystem<URTSStructureSubsystem>();
	const URTSUnitRegistrySubsystem* Units = World.GetSubsystem<URTSUnitRegistrySubsystem>();
	return Structures != nullptr
		&& Units != nullptr
		&& HasConstructed(*Structures, ERTSStructureType::Headquarters)
		&& HasConstructed(*Structures, ERTSStructureType::Factory)
		&& HasConstructed(
			*Structures,
			ERTSStructureType::DefensiveTurret,
			Profile.MaximumDefensiveTurrets)
		&& Units->Query(RTSTeams::Enemy, ERTSUnitType::InfantrySquad).Num()
			>= Profile.TargetInfantrySquads
		&& Units->Query(RTSTeams::Enemy, ERTSUnitType::LightVehicle).Num()
			>= Profile.TargetLightVehicles
		&& Units->Query(RTSTeams::Enemy, ERTSUnitType::HeavyVehicle).Num()
			>= Profile.TargetHeavyVehicles;
}

ARTSCombatUnit* SpawnFixtureUnit(
	UWorld& World,
	const ERTSUnitType UnitType,
	const int32 StableUnitId,
	const FGenericTeamId TeamId,
	const FVector& Location)
{
	const FTransform Transform(FRotator::ZeroRotator, Location);
	ARTSCombatUnit* Unit = World.SpawnActorDeferred<ARTSCombatUnit>(
		ARTSCombatUnit::StaticClass(),
		Transform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Unit != nullptr)
	{
		Unit->ConfigureMilestone2Unit(UnitType, StableUnitId, TeamId);
		Unit->FinishSpawning(Transform);
	}
	return Unit;
}
}

enum class ERTSMilestone3TacticalScenario : uint8
{
	Turrets,
	Defense,
	Reclaim,
	Recovery
};

class FRTSMilestone3AITacticalScenarioCommand final : public IAutomationLatentCommand
{
public:
	FRTSMilestone3AITacticalScenarioCommand(
		FAutomationTestBase* InTest,
		const ERTSMilestone3TacticalScenario InScenario,
		const double InDeadline)
		: Test(InTest), Scenario(InScenario), Deadline(InDeadline)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = RTSMilestone3AITacticsTestPrivate::FindPIEWorld();
		if (World == nullptr)
		{
			return FinishIfTimedOut(TEXT("PIE did not expose the tactical scenario world."));
		}
		URTSAIStrategySubsystem* Strategy = World->GetSubsystem<URTSAIStrategySubsystem>();
		URTSStructureSubsystem* Structures = World->GetSubsystem<URTSStructureSubsystem>();
		URTSUnitRegistrySubsystem* Units = World->GetSubsystem<URTSUnitRegistrySubsystem>();
		URTSWreckageSubsystem* Wreckage = World->GetSubsystem<URTSWreckageSubsystem>();
		URTSEconomySubsystem* Economy = World->GetSubsystem<URTSEconomySubsystem>();
		if (Strategy == nullptr || Structures == nullptr || Units == nullptr
			|| Wreckage == nullptr || Economy == nullptr)
		{
			return FinishIfTimedOut(TEXT("The tactical scenario lost an authoritative subsystem."));
		}
		if (!bStarted)
		{
			Profile = RTSMilestone3AITacticsTestPrivate::MakeNonAttackingProfile();
			if (Scenario == ERTSMilestone3TacticalScenario::Reclaim)
			{
				// Separate tactical safety from the broader base-defense radius so the fixture can
				// prove that a nearby hostile excludes one wreck without triggering a defense order.
				Profile.BaseThreatRadius = 500.0f;
			}
			if (!RTSMilestone3AITacticsTestPrivate::StartEnemy(
					*Test,
					*World,
					Profile))
			{
				return true;
			}
			bStarted = true;
			return false;
		}
		if (!bReady)
		{
			if (!RTSMilestone3AITacticsTestPrivate::HasReadyTacticalBase(*World, Profile))
			{
				return FinishIfTimedOut(TEXT("The tactical base did not become ready."));
			}
			bReady = true;
		}

		switch (Scenario)
		{
		case ERTSMilestone3TacticalScenario::Turrets:
			return VerifyTurrets(*World, *Strategy, *Structures, *Economy);
		case ERTSMilestone3TacticalScenario::Defense:
			return VerifyDefense(*World, *Strategy, *Structures, *Units);
		case ERTSMilestone3TacticalScenario::Reclaim:
			return VerifyReclaim(*World, *Strategy, *Structures, *Units, *Wreckage, *Economy);
		case ERTSMilestone3TacticalScenario::Recovery:
			return VerifyRecovery(*World, *Strategy, *Structures);
		}
		return true;
	}

private:
	bool VerifyTurrets(
		UWorld& World,
		URTSAIStrategySubsystem& Strategy,
		URTSStructureSubsystem& Structures,
		URTSEconomySubsystem& Economy)
	{
		const FRTSAIProfile ExpectedProfile = FRTSMilestone3Configuration::Load().FastTest;
		const TArray<FRTSStructureSnapshot> Turrets = Structures.Query(
			RTSTeams::Enemy,
			ERTSStructureType::DefensiveTurret);
		Test->TestEqual(TEXT("Turret count remains at the configured bound"), Turrets.Num(), ExpectedProfile.MaximumDefensiveTurrets);
		for (int32 LeftIndex = 0; LeftIndex < Turrets.Num(); ++LeftIndex)
		{
			ARTSStructure* Turret = Structures.FindActor(Turrets[LeftIndex].StableStructureId);
			Test->TestTrue(TEXT("Every turret completed normal construction"), Turrets[LeftIndex].bConstructed);
			Test->TestNotNull(TEXT("Every turret owns the normal combat module"),
				Turret != nullptr ? Turret->GetTurretCombatComponent() : nullptr);
			if (Turret != nullptr && Turret->GetTurretCombatComponent() != nullptr)
			{
				Test->TestNotEqual(
					TEXT("Constructed turrets are normally powered"),
					Turret->GetTurretCombatComponent()->GetSnapshot().State,
					ERTSTurretState::Unpowered);
			}
			for (int32 RightIndex = LeftIndex + 1; RightIndex < Turrets.Num(); ++RightIndex)
			{
				Test->TestTrue(
					TEXT("Turret policy preserves configured spacing"),
					FVector::Dist2D(Turrets[LeftIndex].WorldLocation, Turrets[RightIndex].WorldLocation)
						+ UE_KINDA_SMALL_NUMBER >= ExpectedProfile.MinimumTurretSpacing);
			}
		}
		const FRTSAISnapshot Snapshot = Strategy.GetSnapshot(RTSTeams::Enemy);
		const int32 TurretReceipts = Snapshot.StructureCommits.FilterByPredicate(
			[](const FRTSAIStructureCommitReceipt& Receipt)
			{
				return Receipt.Action == ERTSAIAction::PlaceDefensiveTurret
					&& Receipt.MaterialTransactionId > 0;
			}).Num();
		Test->TestEqual(TEXT("Every turret has an accepted paid placement receipt"), TurretReceipts, Turrets.Num());
		Test->TestFalse(TEXT("Normal turret demand does not create a brownout"), Economy.GetSnapshot(RTSTeams::Enemy).IsInBrownout());
		RTSMilestone3AITacticsTestPrivate::RestoreTimeDilation(&World);
		return true;
	}

	bool VerifyDefense(
		UWorld& World,
		URTSAIStrategySubsystem& Strategy,
		URTSStructureSubsystem& Structures,
		URTSUnitRegistrySubsystem& Units)
	{
		if (Phase == 0)
		{
			const TArray<FRTSStructureSnapshot> Headquarters = Structures.Query(
				RTSTeams::Enemy,
				ERTSStructureType::Headquarters);
			if (Headquarters.IsEmpty())
			{
				Test->AddError(TEXT("Defense setup lost the enemy HQ."));
				return true;
			}
			// Remove passive fire from this fixture after proving the normal tactical base was
			// built. Defense has first priority on the next strategy decision, so the resulting
			// incursion must be owned by the explicit group command rather than turret timing.
			for (const FRTSStructureSnapshot& Turret : Structures.Query(
				RTSTeams::Enemy,
				ERTSStructureType::DefensiveTurret))
			{
				if (ARTSStructure* TurretActor = Structures.FindActor(Turret.StableStructureId))
				{
					TurretActor->ApplyDamage(100000.0f);
				}
			}
			UNavigationSystemV1* Navigation = UNavigationSystemV1::GetCurrent(&World);
			FNavLocation ProjectedIncursion;
			if (Navigation == nullptr
				|| !Navigation->ProjectPointToNavigation(
					Headquarters[0].WorldLocation + FVector(1200.0f, 0.0f, 0.0f),
					ProjectedIncursion,
					FVector(800.0f, 800.0f, 500.0f)))
			{
				Test->AddError(TEXT("Defense setup could not project a reachable incursion point."));
				return true;
			}
			Intruder = RTSMilestone3AITacticsTestPrivate::SpawnFixtureUnit(
				World,
				ERTSUnitType::HeavyVehicle,
				91001,
				RTSTeams::Player,
				ProjectedIncursion.Location + FVector(0.0f, 0.0f, 100.0f));
			if (!Test->TestNotNull(TEXT("The base-incursion fixture spawns"), Intruder.Get()))
			{
				return true;
			}
			Test->TestTrue(
				TEXT("The base-incursion fixture is visible through the unit registry"),
				Units.Find(91001).IsSet());
			DefenseSpawnDecisionSequence = Strategy.GetSnapshot(RTSTeams::Enemy).DecisionSequence;
			Phase = 1;
			return false;
		}
		const FRTSAISnapshot Snapshot = Strategy.GetSnapshot(RTSTeams::Enemy);
		if (Phase == 1)
		{
			const FRTSAICommandCommitReceipt* DefenseReceipt = Snapshot.CommandCommits.FindByPredicate(
				[](const FRTSAICommandCommitReceipt& Receipt)
				{
					return Receipt.Phase == ERTSAITacticalPhase::Defending
						&& Receipt.ObjectiveStableId == 91001;
				});
			if (DefenseReceipt == nullptr)
			{
				if (Snapshot.DecisionSequence >= DefenseSpawnDecisionSequence + 3)
				{
					Test->AddError(FString::Printf(
						TEXT("Three strategy decisions ignored the registered incursion: Action=%d Refusal=%d OrderFailure=%d TacticalPhase=%d Objective=%d Responses=%d."),
						static_cast<int32>(Snapshot.CurrentAction),
						static_cast<int32>(Snapshot.LastRefusal),
						static_cast<int32>(Snapshot.LastOrderFailure),
						static_cast<int32>(Snapshot.TacticalPhase),
						Snapshot.ObjectiveStableId,
						Snapshot.DefenseResponseCount));
					RTSMilestone3AITacticsTestPrivate::RestoreTimeDilation(&World);
					return true;
				}
				return FinishIfTimedOut(TEXT("Enemy defenders did not accept the incursion target."));
			}
			Test->TestTrue(TEXT("Defense uses a stable tactical commitment"), DefenseReceipt->TacticalCommitmentId > 0);
			Test->TestTrue(TEXT("Defense records one legal group command"), DefenseReceipt->GroupCommandId > 0);
			Test->TestTrue(TEXT("Defense assigns at least the configured minimum"),
				DefenseReceipt->MemberStableUnitIds.Num()
					>= FRTSMilestone3Configuration::Load().FastTest.MinimumDefenseUnits);
			DefenseGroupCommandId = DefenseReceipt->GroupCommandId;
			DefenseMemberIds = DefenseReceipt->MemberStableUnitIds;
			if (Intruder.IsValid())
			{
				Intruder->GetHealthComponent()->ApplyDamage(100000.0f);
			}
			Phase = 2;
			return false;
		}
		if (Snapshot.TacticalPhase == ERTSAITacticalPhase::Defending)
		{
			return FinishIfTimedOut(TEXT("The defense commitment did not yield after the threat resolved."));
		}
		for (const int32 StableUnitId : DefenseMemberIds)
		{
			if (ARTSCombatUnit* Defender = Units.FindActor(StableUnitId); IsValid(Defender))
			{
				const FRTSOrderSnapshot Order = Defender->GetOrderComponent()->GetSnapshot();
				Test->TestFalse(
					TEXT("Released defenders do not retain the expired defense target"),
					Order.GroupCommandId == DefenseGroupCommandId && Order.Kind == ERTSOrderKind::Attack);
			}
		}
		RTSMilestone3AITacticsTestPrivate::RestoreTimeDilation(&World);
		return true;
	}

	bool VerifyReclaim(
		UWorld& World,
		URTSAIStrategySubsystem& Strategy,
		URTSStructureSubsystem& Structures,
		URTSUnitRegistrySubsystem& Units,
		URTSWreckageSubsystem& Wreckage,
		URTSEconomySubsystem& Economy)
	{
		if (Phase == 0)
		{
			const TArray<FRTSStructureSnapshot> Headquarters = Structures.Query(
				RTSTeams::Enemy,
				ERTSStructureType::Headquarters);
			if (Headquarters.IsEmpty())
			{
				Test->AddError(TEXT("Reclaim setup lost the enemy HQ."));
				return true;
			}
			const FVector SafeWreckLocation = Headquarters[0].WorldLocation
				+ FVector(2200.0f, 400.0f, 100.0f);
			const FVector ThreatenedWreckLocation = Headquarters[0].WorldLocation
				+ FVector(4800.0f, 0.0f, 100.0f);
			ARTSCombatUnit* SafeSource = RTSMilestone3AITacticsTestPrivate::SpawnFixtureUnit(
				World,
				ERTSUnitType::LightVehicle,
				92001,
				RTSTeams::Player,
				SafeWreckLocation);
			ARTSCombatUnit* ThreatenedSource = RTSMilestone3AITacticsTestPrivate::SpawnFixtureUnit(
				World,
				ERTSUnitType::LightVehicle,
				92002,
				RTSTeams::Player,
				ThreatenedWreckLocation);
			Threat = RTSMilestone3AITacticsTestPrivate::SpawnFixtureUnit(
				World,
				ERTSUnitType::HeavyVehicle,
				92003,
				RTSTeams::Player,
				ThreatenedWreckLocation + FVector(0.0f, 200.0f, 0.0f));
			if (!Test->TestNotNull(TEXT("The safe reclaim source fixture spawns"), SafeSource)
				|| !Test->TestNotNull(TEXT("The threatened reclaim source fixture spawns"), ThreatenedSource)
				|| !Test->TestNotNull(TEXT("The reclaim threat fixture spawns"), Threat.Get()))
			{
				return true;
			}
			URTSMatchSubsystem* Match = World.GetSubsystem<URTSMatchSubsystem>();
			if (!Test->TestNotNull(TEXT("The match subsystem records reclaim accounting"), Match))
			{
				return true;
			}
			StartingReclaimedMaterials = Match->GetSnapshot().EnemyStatistics.MaterialsReclaimed;
			SafeSource->GetHealthComponent()->ApplyDamage(100000.0f);
			ThreatenedSource->GetHealthComponent()->ApplyDamage(100000.0f);
			const TArray<FRTSWreckageSnapshot> Available = Wreckage.QueryAvailableSnapshots();
			const FRTSWreckageSnapshot* SafeWreck = Available.FindByPredicate(
				[&SafeWreckLocation](const FRTSWreckageSnapshot& Candidate)
				{
					return FVector::DistSquared2D(Candidate.WorldLocation, SafeWreckLocation) < 100.0f;
				});
			const FRTSWreckageSnapshot* ThreatenedWreck = Available.FindByPredicate(
				[&ThreatenedWreckLocation](const FRTSWreckageSnapshot& Candidate)
				{
					return FVector::DistSquared2D(Candidate.WorldLocation, ThreatenedWreckLocation) < 100.0f;
				});
			if (SafeWreck == nullptr || ThreatenedWreck == nullptr)
			{
				Test->AddError(TEXT("The destroyed fixtures did not create both reclaim candidates."));
				return true;
			}
			ReclaimWreckageId = SafeWreck->StableWreckageId;
			ThreatenedWreckageId = ThreatenedWreck->StableWreckageId;
			ReclaimValue = SafeWreck->ReclaimMaterialValue;
			Phase = 1;
			return false;
		}
		const FRTSAISnapshot Snapshot = Strategy.GetSnapshot(RTSTeams::Enemy);
		if (Phase == 1)
		{
			ARTSWreckage* Target = Wreckage.Find(ReclaimWreckageId);
			if (Snapshot.ReclaimCommits.IsEmpty()
				|| Snapshot.ReclaimCommits.Last().StableWreckageId != ReclaimWreckageId
				|| !IsValid(Target)
				|| Target->GetSnapshot().LeaseHolderStableUnitId == INDEX_NONE)
			{
				return FinishIfTimedOut(TEXT("An idle enemy Infantry did not acquire the safe wreck lease."));
			}
			Test->TestTrue(TEXT("Reclaim uses a normal command receipt"), Snapshot.ReclaimCommits.Last().GroupCommandId > 0);
			const ARTSWreckage* ThreatenedWreck = Wreckage.Find(ThreatenedWreckageId);
			Test->TestTrue(
				TEXT("The AI leaves threatened wreckage unleased"),
				IsValid(ThreatenedWreck)
					&& ThreatenedWreck->GetSnapshot().LeaseHolderStableUnitId == INDEX_NONE);
			ReclaimerStableUnitId = Target->GetSnapshot().LeaseHolderStableUnitId;
			ARTSCombatUnit* Reclaimer = Units.FindActor(ReclaimerStableUnitId);
			if (!Test->TestNotNull(TEXT("The leased Infantry resolves through the unit registry"), Reclaimer))
			{
				return true;
			}
			const FRTSMilestone2Configuration Configuration = FRTSMilestone2Configuration::Load();
			ReclaimRouteLength = RTSMilestone3AITacticsTestPrivate::MeasureCompleteRouteLength(
				World,
				Reclaimer->GetActorLocation(),
				Target->GetActorLocation(),
				Configuration.Reclaim.Range);
			if (!Test->TestTrue(TEXT("The leased wreck retains a complete approach route"), ReclaimRouteLength >= 0.0))
			{
				return true;
			}
			const double PathTravelSeconds = ReclaimRouteLength / Configuration.InfantrySquad.MovementSpeed;
			// High dilation can make CharacterMovement discard simulation steps under host load. Once the
			// normal lease exists, prove the real approach and reclaim at normal time. The constructed base
			// can force a substantial detour, so the bound follows the accepted Recast route rather than the
			// misleading straight-line distance to the wreck.
			RTSMilestone3AITacticsTestPrivate::RestoreTimeDilation(&World);
			Deadline = FPlatformTime::Seconds()
				+ PathTravelSeconds * 2.0
				+ Configuration.Reclaim.DurationSeconds
				+ 5.0;
			Phase = 2;
			return false;
		}
		if (IsValid(Wreckage.Find(ReclaimWreckageId)))
		{
			if (FPlatformTime::Seconds() < Deadline)
			{
				return false;
			}
			const ARTSCombatUnit* Reclaimer = Units.FindActor(ReclaimerStableUnitId);
			const ARTSWreckage* Target = Wreckage.Find(ReclaimWreckageId);
			const FRTSOrderSnapshot Order = IsValid(Reclaimer)
				? Reclaimer->GetOrderComponent()->GetSnapshot()
				: FRTSOrderSnapshot();
			Test->AddError(FString::Printf(
				TEXT("Reclaim did not finish: target_valid=%d distance=%.1f route=%.1f kind=%d phase=%d failure=%d lease=%d."),
				IsValid(Target),
				IsValid(Reclaimer) && IsValid(Target)
					? FVector::Dist2D(Reclaimer->GetActorLocation(), Target->GetActorLocation())
					: -1.0f,
				ReclaimRouteLength,
				static_cast<int32>(Order.Kind),
				static_cast<int32>(Order.Phase),
				static_cast<int32>(Order.LastFailure),
				IsValid(Target) ? Target->GetSnapshot().LeaseHolderStableUnitId : INDEX_NONE));
			RTSMilestone3AITacticsTestPrivate::RestoreTimeDilation(&World);
			return true;
		}
		const URTSMatchSubsystem* Match = World.GetSubsystem<URTSMatchSubsystem>();
		Test->TestNotNull(TEXT("The match subsystem remains available"), Match);
		if (Match != nullptr)
		{
			Test->TestEqual(
				TEXT("Exactly one reclaim payout is recorded independently of extractor income"),
				Match->GetSnapshot().EnemyStatistics.MaterialsReclaimed,
				StartingReclaimedMaterials + ReclaimValue);
		}
		Test->TestTrue(
			TEXT("Threatened wreckage remains available after the safe reclaim"),
			IsValid(Wreckage.Find(ThreatenedWreckageId)));
		RTSMilestone3AITacticsTestPrivate::RestoreTimeDilation(&World);
		return true;
	}

	bool VerifyRecovery(
		UWorld& World,
		URTSAIStrategySubsystem& Strategy,
		URTSStructureSubsystem& Structures)
	{
		if (Phase == 0)
		{
			const TArray<FRTSStructureSnapshot> Factories = Structures.Query(
				RTSTeams::Enemy,
				ERTSStructureType::Factory);
			if (Factories.Num() != 1)
			{
				Test->AddError(TEXT("Recovery setup requires exactly one enemy Factory."));
				return true;
			}
			DestroyedFactoryId = Factories[0].StableStructureId;
			const FRTSAISnapshot Snapshot = Strategy.GetSnapshot(RTSTeams::Enemy);
			InitialFactoryReceiptCount = Snapshot.StructureCommits.FilterByPredicate(
				[](const FRTSAIStructureCommitReceipt& Receipt)
				{
					return Receipt.StructureType == ERTSStructureType::Factory;
				}).Num();
			ARTSStructure* Factory = Structures.FindActor(DestroyedFactoryId);
			if (!Test->TestNotNull(TEXT("The recovery Factory resolves"), Factory))
			{
				return true;
			}
			Factory->ApplyDamage(100000.0f);
			Phase = 1;
			return false;
		}
		const TArray<FRTSStructureSnapshot> Factories = Structures.Query(
			RTSTeams::Enemy,
			ERTSStructureType::Factory);
		const FRTSStructureSnapshot* Replacement = Factories.FindByPredicate(
			[this](const FRTSStructureSnapshot& Factory)
			{
				return Factory.bAlive
					&& Factory.bConstructed
					&& Factory.StableStructureId != DestroyedFactoryId;
			});
		if (Replacement == nullptr)
		{
			return FinishIfTimedOut(TEXT("The destroyed Factory was not reconstructed."));
		}
		const FRTSAISnapshot Snapshot = Strategy.GetSnapshot(RTSTeams::Enemy);
		if (Phase == 1)
		{
			ReplacementFactoryId = Replacement->StableStructureId;
			RecoveryReadyDecisionSequence = Snapshot.DecisionSequence;
			Phase = 2;
			return false;
		}
		if (Snapshot.DecisionSequence <= RecoveryReadyDecisionSequence
			|| Snapshot.LastRefusal == ERTSAIRefusal::ConstructionInProgress)
		{
			return FinishIfTimedOut(TEXT("The strategy did not resume after Factory reconstruction."));
		}
		const int32 FinalFactoryReceiptCount = Snapshot.StructureCommits.FilterByPredicate(
			[](const FRTSAIStructureCommitReceipt& Receipt)
			{
				return Receipt.StructureType == ERTSStructureType::Factory;
			}).Num();
		Test->TestEqual(
			TEXT("Factory recovery commits exactly one replacement"),
			FinalFactoryReceiptCount,
			InitialFactoryReceiptCount + 1);
		Test->TestEqual(TEXT("Factory recovery leaves one live Factory"), Factories.Num(), 1);
		Test->TestEqual(
			TEXT("The live Factory is the committed replacement"),
			Factories[0].StableStructureId,
			ReplacementFactoryId);
		RTSMilestone3AITacticsTestPrivate::RestoreTimeDilation(&World);
		return true;
	}

	bool FinishIfTimedOut(const TCHAR* Failure)
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		RTSMilestone3AITacticsTestPrivate::RestoreTimeDilation(
			RTSMilestone3AITacticsTestPrivate::FindPIEWorld());
		Test->AddError(Failure);
		return true;
	}

	FAutomationTestBase* Test = nullptr;
	ERTSMilestone3TacticalScenario Scenario;
	FRTSAIProfile Profile;
	double Deadline = 0.0;
	bool bStarted = false;
	bool bReady = false;
	int32 Phase = 0;
	TWeakObjectPtr<ARTSCombatUnit> Intruder;
	TWeakObjectPtr<ARTSCombatUnit> Threat;
	int64 DefenseGroupCommandId = 0;
	int32 DefenseSpawnDecisionSequence = 0;
	TArray<int32> DefenseMemberIds;
	int32 StartingReclaimedMaterials = 0;
	int32 ReclaimWreckageId = INDEX_NONE;
	int32 ReclaimerStableUnitId = INDEX_NONE;
	double ReclaimRouteLength = -1.0;
	int32 ThreatenedWreckageId = INDEX_NONE;
	int32 ReclaimValue = 0;
	int32 DestroyedFactoryId = INDEX_NONE;
	int32 ReplacementFactoryId = INDEX_NONE;
	int32 InitialFactoryReceiptCount = 0;
	int32 RecoveryReadyDecisionSequence = 0;
};

#define IMPLEMENT_RTS_TACTICAL_SCENARIO_TEST(TestClass, TestPath, ScenarioValue, MapLabel) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST( \
		TestClass, \
		TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters) \
	{ \
		if (!TestTrue(MapLabel, AutomationOpenMap(TEXT("/Game/RTS/Maps/M3_Skirmish"), true))) \
		{ \
			return false; \
		} \
		ADD_LATENT_AUTOMATION_COMMAND(FRTSMilestone3AITacticalScenarioCommand( \
			this, \
			ScenarioValue, \
			FPlatformTime::Seconds() + 120.0)); \
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand()); \
		return true; \
	}

IMPLEMENT_RTS_TACTICAL_SCENARIO_TEST(
	FRTSMilestone3AITurretsTest,
	"Task0169.Headless.RTS.Milestone3.AI.Turrets",
	ERTSMilestone3TacticalScenario::Turrets,
	TEXT("The turret strategy map opens"))

IMPLEMENT_RTS_TACTICAL_SCENARIO_TEST(
	FRTSMilestone3AIDefenseTest,
	"Task0169.Headless.RTS.Milestone3.AI.Defense",
	ERTSMilestone3TacticalScenario::Defense,
	TEXT("The defense strategy map opens"))

IMPLEMENT_RTS_TACTICAL_SCENARIO_TEST(
	FRTSMilestone3AIReclaimTest,
	"Task0169.Headless.RTS.Milestone3.AI.Reclaim",
	ERTSMilestone3TacticalScenario::Reclaim,
	TEXT("The reclaim strategy map opens"))

IMPLEMENT_RTS_TACTICAL_SCENARIO_TEST(
	FRTSMilestone3AIRecoveryTest,
	"Task0169.Headless.RTS.Milestone3.AI.Recovery",
	ERTSMilestone3TacticalScenario::Recovery,
	TEXT("The recovery strategy map opens"))

#undef IMPLEMENT_RTS_TACTICAL_SCENARIO_TEST

class FRTSMilestone3AIMatchSmokeCommand final : public IAutomationLatentCommand
{
public:
	FRTSMilestone3AIMatchSmokeCommand(
		FAutomationTestBase* InTest,
		const bool bInRequireRetarget,
		const double InDeadline)
		: Test(InTest), Deadline(InDeadline), bRequireRetarget(bInRequireRetarget)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = RTSMilestone3AITacticsTestPrivate::FindPIEWorld();
		if (World == nullptr)
		{
			return FinishIfTimedOut(TEXT("PIE did not expose the tactical smoke world."));
		}
		URTSAIStrategySubsystem* Strategy = World->GetSubsystem<URTSAIStrategySubsystem>();
		URTSStructureSubsystem* Structures = World->GetSubsystem<URTSStructureSubsystem>();
		URTSUnitRegistrySubsystem* Units = World->GetSubsystem<URTSUnitRegistrySubsystem>();
		if (Strategy == nullptr || Structures == nullptr || Units == nullptr)
		{
			return FinishIfTimedOut(TEXT("The tactical smoke fixture lost an authoritative subsystem."));
		}
		if (!bStarted)
		{
			FRTSAIProfile Profile = FRTSMilestone3Configuration::Load().FastTest;
			if (bRequireRetarget)
			{
				Profile.AttackCommitmentSeconds = 8.0f;
				Profile.AttackMusterTolerance = 1.0f;
				Profile.AttackMusterTimeoutSeconds = 2.0f;
			}
			if (!RTSMilestone3AITacticsTestPrivate::StartEnemy(*Test, *World, Profile))
			{
				return true;
			}
			bStarted = true;
			return false;
		}

		const FRTSAISnapshot Snapshot = Strategy->GetSnapshot(RTSTeams::Enemy);
		if (Snapshot.AttackLaunchCount < 1)
		{
			return FinishIfTimedOut(TEXT("The fresh enemy did not reach a legal attack launch."));
		}
		if (bRequireRetarget && FirstAttackCommitmentId == 0)
		{
			FirstAttackCommitmentId = Snapshot.TacticalCommitmentId;
			return false;
		}
		if (bRequireRetarget
			&& (Snapshot.AttackLaunchCount < 2
				|| Snapshot.TacticalCommitmentId == FirstAttackCommitmentId))
		{
			return FinishIfTimedOut(TEXT("The expired attack commitment did not release and retarget."));
		}
		Test->TestTrue(
			TEXT("The enemy deployed a constructed HQ"),
			RTSMilestone3AITacticsTestPrivate::HasConstructed(*Structures, ERTSStructureType::Headquarters));
		Test->TestTrue(
			TEXT("The enemy established Material income"),
			RTSMilestone3AITacticsTestPrivate::HasConstructed(*Structures, ERTSStructureType::MaterialExtractor));
		Test->TestTrue(
			TEXT("The enemy established a Factory"),
			RTSMilestone3AITacticsTestPrivate::HasConstructed(*Structures, ERTSStructureType::Factory));
		Test->TestTrue(
			TEXT("The enemy constructed bounded turret coverage"),
			RTSMilestone3AITacticsTestPrivate::HasConstructed(
				*Structures,
				ERTSStructureType::DefensiveTurret,
				FRTSMilestone3Configuration::Load().FastTest.MaximumDefensiveTurrets));
		Test->TestTrue(
			TEXT("The enemy retained its configured defense reserve"),
			Units->Query(RTSTeams::Enemy).Num()
				- Snapshot.TacticalMemberStableUnitIds.Num()
				>= FRTSMilestone3Configuration::Load().FastTest.DefenseReserveUnits);
		TSet<ERTSUnitType> AttackTypes;
		for (const int32 StableUnitId : Snapshot.TacticalMemberStableUnitIds)
		{
			const TOptional<FRTSUnitSnapshot> Unit = Units->Find(StableUnitId);
			if (Unit.IsSet())
			{
				AttackTypes.Add(Unit->UnitType);
			}
		}
		Test->TestEqual(TEXT("The attack commitment is a three-type mixed force"), AttackTypes.Num(), 3);
		Test->TestTrue(TEXT("The attack has an authoritative group command"), Snapshot.GroupCommandId > 0);
		const FRTSAICommandCommitReceipt* AttackReceipt = Snapshot.CommandCommits.FindByPredicate(
			[&Snapshot](const FRTSAICommandCommitReceipt& Receipt)
			{
				return Receipt.Phase == ERTSAITacticalPhase::Attacking
					&& Receipt.GroupCommandId == Snapshot.GroupCommandId;
			});
		Test->TestNotNull(TEXT("The attack command has a durable typed receipt"), AttackReceipt);
		if (AttackReceipt != nullptr)
		{
			Test->TestEqual(
				TEXT("The receipt preserves every committed attack member"),
				AttackReceipt->MemberStableUnitIds,
				Snapshot.TacticalMemberStableUnitIds);
		}
		if (bRequireRetarget)
		{
			Test->TestTrue(TEXT("A later commitment replaced the expired attack"),
				Snapshot.TacticalCommitmentId != FirstAttackCommitmentId);
			Test->TestTrue(
				TEXT("A blocked formation cannot permanently gate the attack"),
				Snapshot.MusterTimeoutCount > 0);
			Test->TestTrue(
				TEXT("The expired attack commitment records its release"),
				Snapshot.AttackReleaseCount > 0);
		}
		RTSMilestone3AITacticsTestPrivate::RestoreTimeDilation(World);
		return true;
	}

private:
	bool FinishIfTimedOut(const TCHAR* Failure)
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		RTSMilestone3AITacticsTestPrivate::RestoreTimeDilation(
			RTSMilestone3AITacticsTestPrivate::FindPIEWorld());
		Test->AddError(Failure);
		return true;
	}

	FAutomationTestBase* Test = nullptr;
	double Deadline = 0.0;
	bool bStarted = false;
	bool bRequireRetarget = false;
	int64 FirstAttackCommitmentId = 0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone3AIMatchSmokeTest,
	"Task0169.Headless.RTS.Milestone3.Match.Smoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone3AIMatchSmokeTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("The tactical smoke map opens"), AutomationOpenMap(TEXT("/Game/RTS/Maps/M3_Skirmish"), true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRTSMilestone3AIMatchSmokeCommand(
		this,
		false,
		FPlatformTime::Seconds() + 120.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone3AIAttackGroupTest,
	"Task0169.Headless.RTS.Milestone3.AI.AttackGroup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone3AIAttackGroupTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("The attack-group strategy map opens"), AutomationOpenMap(TEXT("/Game/RTS/Maps/M3_Skirmish"), true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRTSMilestone3AIMatchSmokeCommand(
		this,
		true,
		FPlatformTime::Seconds() + 120.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
