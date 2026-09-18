// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/RTSAIStrategySubsystem.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "Configuration/RTSMilestone3Configuration.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "Match/RTSMatchSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSUnitRegistrySubsystem.h"
#include "Units/RTSTeams.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RTSMilestone3AIEconomyTestPrivate
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

bool HasConstructedCount(
	const URTSStructureSubsystem& Structures,
	const ERTSStructureType StructureType,
	const int32 ExpectedCount)
{
	const TArray<FRTSStructureSnapshot> Matches = Structures.Query(RTSTeams::Enemy, StructureType);
	return Matches.Num() == ExpectedCount
		&& Matches.FilterByPredicate([](const FRTSStructureSnapshot& Structure)
		{
			return Structure.bAlive && Structure.bConstructed;
		}).Num() == ExpectedCount;
}

bool HasCompleteEconomy(const URTSStructureSubsystem& Structures)
{
	return HasConstructedCount(Structures, ERTSStructureType::Headquarters, 1)
		&& HasConstructedCount(Structures, ERTSStructureType::MaterialExtractor, 1)
		&& HasConstructedCount(Structures, ERTSStructureType::PowerGenerator, 1)
		&& HasConstructedCount(Structures, ERTSStructureType::SupplyDepot, 1)
		&& HasConstructedCount(Structures, ERTSStructureType::Factory, 1);
}

bool HasCompleteRoster(const URTSUnitRegistrySubsystem& Units)
{
	return Units.Query(RTSTeams::Enemy, ERTSUnitType::InfantrySquad).Num() >= 1
		&& Units.Query(RTSTeams::Enemy, ERTSUnitType::LightVehicle).Num() >= 1
		&& Units.Query(RTSTeams::Enemy, ERTSUnitType::HeavyVehicle).Num() >= 1;
}

bool StartFastEnemy(FAutomationTestBase& Test, UWorld& World)
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
	FRTSAIProfile EconomyOnlyProfile = FRTSMilestone3Configuration::Load().FastTest;
	// This Slice 2 receipt must remain about economy and production even after later tactical
	// policy exists. Impossible tactical locations/group size keep those later actions inert.
	EconomyOnlyProfile.TargetInfantrySquads = 1;
	EconomyOnlyProfile.MinimumAttackGroupSize = 100;
	EconomyOnlyProfile.TurretAnchorRadius = 100000.0f;
	const FRTSAIStartResult Start = Strategy->StartFaction(
		RTSTeams::Enemy,
		EconomyOnlyProfile);
	return Test.TestTrue(TEXT("The fast enemy strategy starts"), Start.bAccepted);
}

void RestoreTimeDilation(UWorld* World)
{
	if (World != nullptr)
	{
		World->GetWorldSettings()->SetTimeDilation(1.0f);
	}
}
}

class FRTSMilestone3AIEconomyCommand final : public IAutomationLatentCommand
{
public:
	FRTSMilestone3AIEconomyCommand(FAutomationTestBase* InTest, const double InDeadline)
		: Test(InTest), Deadline(InDeadline)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = RTSMilestone3AIEconomyTestPrivate::FindPIEWorld();
		if (World == nullptr)
		{
			return FinishIfTimedOut(TEXT("PIE did not expose the economy world."));
		}
		URTSEconomySubsystem* Economy = World->GetSubsystem<URTSEconomySubsystem>();
		URTSStructureSubsystem* Structures = World->GetSubsystem<URTSStructureSubsystem>();
		URTSAIStrategySubsystem* Strategy = World->GetSubsystem<URTSAIStrategySubsystem>();
		if (Economy == nullptr || Structures == nullptr || Strategy == nullptr)
		{
			return FinishIfTimedOut(TEXT("The economy fixture is missing an authoritative subsystem."));
		}
		if (!bStarted)
		{
			const int32 PlayerMaterials = Economy->GetSnapshot(RTSTeams::Player).Materials;
			const int32 EnemyMaterials = Economy->GetSnapshot(RTSTeams::Enemy).Materials;
			Test->TestEqual(TEXT("Both factions start with equal Materials"), EnemyMaterials, PlayerMaterials);
			if (!RTSMilestone3AIEconomyTestPrivate::StartFastEnemy(*Test, *World))
			{
				return true;
			}
			bStarted = true;
			return false;
		}
		const FRTSAISnapshot StrategySnapshot = Strategy->GetSnapshot(RTSTeams::Enemy);
		bSawConstructionWait |= StrategySnapshot.LastRefusal == ERTSAIRefusal::ConstructionInProgress;
		if (!RTSMilestone3AIEconomyTestPrivate::HasCompleteEconomy(*Structures))
		{
			return FinishIfTimedOut(TEXT("The enemy economy did not complete before the deadline."));
		}

		const FRTSEconomySnapshot Snapshot = Economy->GetSnapshot(RTSTeams::Enemy);
		const FRTSMilestone2Configuration Configuration = FRTSMilestone2Configuration::Load();
		Test->TestTrue(TEXT("The strategy retained an intention during construction"), bSawConstructionWait);
		Test->TestEqual(TEXT("Four normal structures were accepted"), StrategySnapshot.AcceptedStructurePlacementCount, 4);
		Test->TestEqual(TEXT("Every accepted placement retained its receipt"), StrategySnapshot.StructureCommits.Num(), 4);
		for (const FRTSAIStructureCommitReceipt& Receipt : StrategySnapshot.StructureCommits)
		{
			Test->TestTrue(TEXT("Each structure receipt has an actor ID"), Receipt.StableStructureId > 0);
			Test->TestTrue(TEXT("Each paid structure has an economy transaction"), Receipt.MaterialTransactionId > 0);
		}
		Test->TestTrue(TEXT("The Extractor produces positive Materials"), Snapshot.Materials > 0);
		Test->TestEqual(
			TEXT("HQ and Generator contribute normal Power"),
			Snapshot.PowerGeneration,
			Configuration.Headquarters.PowerGeneration + Configuration.PowerGenerator.PowerGeneration);
		Test->TestEqual(
			TEXT("Completed economy structures contribute normal demand"),
			Snapshot.PowerDemand,
			Configuration.MaterialExtractor.PowerDemand
				+ Configuration.SupplyDepot.PowerDemand
				+ Configuration.Factory.PowerDemand);
		Test->TestEqual(
			TEXT("HQ and Supply Depot contribute normal capacity"),
			Snapshot.SupplyCapacity,
			Configuration.Headquarters.SupplyCapacity + Configuration.SupplyDepot.SupplyCapacity);
		RTSMilestone3AIEconomyTestPrivate::RestoreTimeDilation(World);
		return true;
	}

private:
	bool FinishIfTimedOut(const TCHAR* Failure)
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		RTSMilestone3AIEconomyTestPrivate::RestoreTimeDilation(
			RTSMilestone3AIEconomyTestPrivate::FindPIEWorld());
		Test->AddError(Failure);
		return true;
	}

	FAutomationTestBase* Test = nullptr;
	double Deadline = 0.0;
	bool bStarted = false;
	bool bSawConstructionWait = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone3AIEconomyTest,
	"Task0169.Headless.RTS.Milestone3.AI.Economy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone3AIEconomyTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("The economy skirmish opens"), AutomationOpenMap(TEXT("/Game/RTS/Maps/M3_Skirmish"), true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRTSMilestone3AIEconomyCommand(this, FPlatformTime::Seconds() + 60.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

class FRTSMilestone3AIProductionCommand final : public IAutomationLatentCommand
{
public:
	FRTSMilestone3AIProductionCommand(FAutomationTestBase* InTest, const double InDeadline)
		: Test(InTest), Deadline(InDeadline)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = RTSMilestone3AIEconomyTestPrivate::FindPIEWorld();
		if (World == nullptr)
		{
			return FinishIfTimedOut(TEXT("PIE did not expose the production world."));
		}
		URTSAIStrategySubsystem* Strategy = World->GetSubsystem<URTSAIStrategySubsystem>();
		URTSUnitRegistrySubsystem* Units = World->GetSubsystem<URTSUnitRegistrySubsystem>();
		URTSEconomySubsystem* Economy = World->GetSubsystem<URTSEconomySubsystem>();
		URTSMatchSubsystem* Match = World->GetSubsystem<URTSMatchSubsystem>();
		URTSStructureSubsystem* Structures = World->GetSubsystem<URTSStructureSubsystem>();
		if (Strategy == nullptr || Units == nullptr || Economy == nullptr || Match == nullptr
			|| Structures == nullptr)
		{
			return FinishIfTimedOut(TEXT("The production fixture is missing an authoritative subsystem."));
		}
		if (!bStarted)
		{
			if (!RTSMilestone3AIEconomyTestPrivate::StartFastEnemy(*Test, *World))
			{
				return true;
			}
			bStarted = true;
			return false;
		}

		const FRTSAISnapshot StrategySnapshot = Strategy->GetSnapshot(RTSTeams::Enemy);
		bSawAffordabilityRefusal |= StrategySnapshot.LastProductionRefusal
			== ERTSProductionRefusal::EconomyRejected;
		if (!RTSMilestone3AIEconomyTestPrivate::HasCompleteRoster(*Units))
		{
			return FinishIfTimedOut(TEXT("The enemy did not complete the three-unit roster."));
		}
		const ERTSStructureType RecoveryTypes[] = {
			ERTSStructureType::MaterialExtractor,
			ERTSStructureType::PowerGenerator,
			ERTSStructureType::SupplyDepot,
			ERTSStructureType::Factory};
		if (RecoveryIndex < UE_ARRAY_COUNT(RecoveryTypes))
		{
			const ERTSStructureType RecoveryType = RecoveryTypes[RecoveryIndex];
			if (DestroyedStructureId == INDEX_NONE)
			{
				const TArray<FRTSStructureSnapshot> Existing = Structures->Query(
					RTSTeams::Enemy,
					RecoveryType);
				if (Existing.IsEmpty())
				{
					return FinishIfTimedOut(TEXT("A required structure vanished before recovery setup."));
				}
				DestroyedStructureId = Existing[0].StableStructureId;
				ARTSStructure* Structure = Structures->FindActor(DestroyedStructureId);
				if (!Test->TestNotNull(TEXT("The recovery target resolves through the structure seam"), Structure))
				{
					return true;
				}
				Structure->ApplyDamage(100000.0f);
				return false;
			}
			const TArray<FRTSStructureSnapshot> Replacements = Structures->Query(
				RTSTeams::Enemy,
				RecoveryType);
			const bool bReplacementComplete = Replacements.ContainsByPredicate(
				[this](const FRTSStructureSnapshot& Structure)
				{
					return Structure.bAlive
						&& Structure.bConstructed
						&& Structure.StableStructureId != DestroyedStructureId;
				});
			if (!bReplacementComplete)
			{
				return FinishIfTimedOut(TEXT("The AI did not restore a destroyed required structure."));
			}
			++RecoveryIndex;
			DestroyedStructureId = INDEX_NONE;
			return false;
		}

		const FRTSAISnapshot FinalStrategySnapshot = Strategy->GetSnapshot(RTSTeams::Enemy);
		const FRTSEconomySnapshot EconomySnapshot = Economy->GetSnapshot(RTSTeams::Enemy);
		const FRTSMatchSnapshot MatchSnapshot = Match->GetSnapshot();
		Test->AddInfo(bSawAffordabilityRefusal ? TEXT("Affordability refusal observed") : TEXT("Strategy waited until affordable"));
		Test->TestEqual(TEXT("Three queue operations were accepted"), FinalStrategySnapshot.AcceptedProductionEnqueueCount, 3);
		Test->TestEqual(TEXT("Every queue operation retained its receipt"), FinalStrategySnapshot.ProductionCommits.Num(), 3);
		TSet<ERTSUnitType> ProducedTypes;
		for (const FRTSAIProductionCommitReceipt& Receipt : FinalStrategySnapshot.ProductionCommits)
		{
			ProducedTypes.Add(Receipt.UnitType);
			Test->TestTrue(TEXT("Each queue receipt has an entry ID"), Receipt.QueueEntryId > 0);
			Test->TestTrue(TEXT("Each queue receipt has a Material reservation"), Receipt.MaterialReservationTransactionId > 0);
		}
		Test->TestEqual(TEXT("The queue receipts cover the complete roster"), ProducedTypes.Num(), 3);
		Test->TestEqual(TEXT("Exactly three enemy units completed production"), MatchSnapshot.EnemyStatistics.UnitsProduced, 3);
		Test->TestEqual(TEXT("All four required structures were restored once"), FinalStrategySnapshot.StructureCommits.Num(), 8);
		Test->TestEqual(TEXT("Completed units consume seven Supply"), EconomySnapshot.SupplyUsed, 7);
		Test->TestEqual(TEXT("No completed queue leaves Supply reserved"), EconomySnapshot.SupplyReserved, 0);
		RTSMilestone3AIEconomyTestPrivate::RestoreTimeDilation(World);
		return true;
	}

private:
	bool FinishIfTimedOut(const TCHAR* Failure)
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		RTSMilestone3AIEconomyTestPrivate::RestoreTimeDilation(
			RTSMilestone3AIEconomyTestPrivate::FindPIEWorld());
		Test->AddError(Failure);
		return true;
	}

	FAutomationTestBase* Test = nullptr;
	double Deadline = 0.0;
	bool bStarted = false;
	bool bSawAffordabilityRefusal = false;
	int32 RecoveryIndex = 0;
	int32 DestroyedStructureId = INDEX_NONE;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone3AIProductionTest,
	"Task0169.Headless.RTS.Milestone3.AI.Production",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone3AIProductionTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("The production skirmish opens"), AutomationOpenMap(TEXT("/Game/RTS/Maps/M3_Skirmish"), true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRTSMilestone3AIProductionCommand(this, FPlatformTime::Seconds() + 90.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

class FRTSMilestone3AIFairnessCommand final : public IAutomationLatentCommand
{
public:
	FRTSMilestone3AIFairnessCommand(FAutomationTestBase* InTest, const double InDeadline)
		: Test(InTest), Deadline(InDeadline)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = RTSMilestone3AIEconomyTestPrivate::FindPIEWorld();
		if (World == nullptr)
		{
			return FinishIfTimedOut(TEXT("PIE did not expose the fairness world."));
		}
		URTSEconomySubsystem* Economy = World->GetSubsystem<URTSEconomySubsystem>();
		URTSAIStrategySubsystem* Strategy = World->GetSubsystem<URTSAIStrategySubsystem>();
		URTSUnitRegistrySubsystem* Units = World->GetSubsystem<URTSUnitRegistrySubsystem>();
		if (Economy == nullptr || Strategy == nullptr || Units == nullptr)
		{
			return FinishIfTimedOut(TEXT("The fairness fixture is missing an authoritative subsystem."));
		}
		if (!bStarted)
		{
			InitialEconomy = Economy->GetSnapshot(RTSTeams::Enemy);
			TransactionHandle = Economy->OnTransactionApplied().AddRaw(
				this,
				&FRTSMilestone3AIFairnessCommand::RecordTransaction);
			if (!RTSMilestone3AIEconomyTestPrivate::StartFastEnemy(*Test, *World))
			{
				Economy->OnTransactionApplied().Remove(TransactionHandle);
				return true;
			}
			bStarted = true;
			return false;
		}
		if (!RTSMilestone3AIEconomyTestPrivate::HasCompleteRoster(*Units))
		{
			return FinishIfTimedOut(TEXT("The fairness run did not complete the roster."));
		}

		Economy->OnTransactionApplied().Remove(TransactionHandle);
		const FRTSAISnapshot StrategySnapshot = Strategy->GetSnapshot(RTSTeams::Enemy);
		TSet<int64> AuthorizedMaterialTransactions;
		TSet<int64> ProductionReservations;
		for (const FRTSAIStructureCommitReceipt& Receipt : StrategySnapshot.StructureCommits)
		{
			AuthorizedMaterialTransactions.Add(Receipt.MaterialTransactionId);
		}
		for (const FRTSAIProductionCommitReceipt& Receipt : StrategySnapshot.ProductionCommits)
		{
			AuthorizedMaterialTransactions.Add(Receipt.MaterialReservationTransactionId);
			ProductionReservations.Add(Receipt.MaterialReservationTransactionId);
		}

		FRTSEconomyDelta Applied;
		const int32 IncomeAmount = FRTSMilestone2Configuration::Load().MaterialExtractor.MaterialIncomePerInterval;
		for (const FRTSEconomyTransactionReceipt& Receipt : Transactions)
		{
			Applied.Materials += Receipt.Delta.Materials;
			Applied.PowerGeneration += Receipt.Delta.PowerGeneration;
			Applied.PowerDemand += Receipt.Delta.PowerDemand;
			Applied.SupplyUsed += Receipt.Delta.SupplyUsed;
			Applied.SupplyReserved += Receipt.Delta.SupplyReserved;
			Applied.SupplyCapacity += Receipt.Delta.SupplyCapacity;
			if (Receipt.Delta.Materials < 0)
			{
				Test->TestTrue(
					TEXT("Every Material spend belongs to an accepted placement or queue receipt"),
					AuthorizedMaterialTransactions.Contains(Receipt.TransactionId));
			}
			if (Receipt.Delta.Materials > 0)
			{
				Test->TestEqual(TEXT("Positive Materials come from one Extractor interval"), Receipt.Delta.Materials, IncomeAmount);
			}
			if (Receipt.Kind == ERTSEconomyTransactionKind::SupplyConversion)
			{
				Test->TestTrue(
					TEXT("Every Supply conversion refers to an accepted production reservation"),
					ProductionReservations.Contains(Receipt.RelatedTransactionId));
			}
		}

		const FRTSEconomySnapshot Final = Economy->GetSnapshot(RTSTeams::Enemy);
		Test->TestEqual(TEXT("Audited Materials reconcile"), InitialEconomy.Materials + Applied.Materials, Final.Materials);
		Test->TestEqual(TEXT("Audited Power generation reconciles"), InitialEconomy.PowerGeneration + Applied.PowerGeneration, Final.PowerGeneration);
		Test->TestEqual(TEXT("Audited Power demand reconciles"), InitialEconomy.PowerDemand + Applied.PowerDemand, Final.PowerDemand);
		Test->TestEqual(TEXT("Audited Supply usage reconciles"), InitialEconomy.SupplyUsed + Applied.SupplyUsed, Final.SupplyUsed);
		Test->TestEqual(TEXT("Audited Supply reservations reconcile"), InitialEconomy.SupplyReserved + Applied.SupplyReserved, Final.SupplyReserved);
		Test->TestEqual(TEXT("Audited Supply capacity reconciles"), InitialEconomy.SupplyCapacity + Applied.SupplyCapacity, Final.SupplyCapacity);
		RTSMilestone3AIEconomyTestPrivate::RestoreTimeDilation(World);
		return true;
	}

private:
	void RecordTransaction(const FRTSEconomyTransactionReceipt& Receipt)
	{
		if (Receipt.TeamId == RTSTeams::Enemy)
		{
			Transactions.Add(Receipt);
		}
	}

	bool FinishIfTimedOut(const TCHAR* Failure)
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		if (UWorld* World = RTSMilestone3AIEconomyTestPrivate::FindPIEWorld())
		{
			if (URTSEconomySubsystem* Economy = World->GetSubsystem<URTSEconomySubsystem>())
			{
				Economy->OnTransactionApplied().Remove(TransactionHandle);
			}
			RTSMilestone3AIEconomyTestPrivate::RestoreTimeDilation(World);
		}
		Test->AddError(Failure);
		return true;
	}

	FAutomationTestBase* Test = nullptr;
	double Deadline = 0.0;
	bool bStarted = false;
	FDelegateHandle TransactionHandle;
	FRTSEconomySnapshot InitialEconomy;
	TArray<FRTSEconomyTransactionReceipt> Transactions;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone3AIFairnessTest,
	"Task0169.Headless.RTS.Milestone3.AI.Fairness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone3AIFairnessTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("The fairness skirmish opens"), AutomationOpenMap(TEXT("/Game/RTS/Maps/M3_Skirmish"), true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRTSMilestone3AIFairnessCommand(this, FPlatformTime::Seconds() + 75.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
