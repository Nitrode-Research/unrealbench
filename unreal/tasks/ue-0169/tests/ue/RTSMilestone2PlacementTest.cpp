// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/RTSHealthComponent.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "NavAreas/NavArea_Null.h"
#include "NavModifierComponent.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
UWorld* FindPIEWorld()
{
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType == EWorldType::PIE)
		{
			return WorldContext.World();
		}
	}
	return nullptr;
}

ARTSCombatUnit* FindPlayerCommandVehicle(UWorld& World)
{
	for (TActorIterator<ARTSCombatUnit> UnitIterator(&World); UnitIterator; ++UnitIterator)
	{
		if (UnitIterator->GetGenericTeamId() == RTSTeams::Player
			&& UnitIterator->IsMilestone2Unit()
			&& UnitIterator->GetUnitType() == ERTSUnitType::CommandVehicle)
		{
			return *UnitIterator;
		}
	}
	return nullptr;
}

FRTSPlacementRequest PlacementRequest(
	const ERTSStructureType StructureType,
	const FVector& DesiredLocation)
{
	FRTSPlacementRequest Request;
	Request.TeamId = RTSTeams::Player;
	Request.StructureType = StructureType;
	Request.DesiredWorldLocation = DesiredLocation;
	return Request;
}
}

class FRTSVerifyPlacementCommand final : public IAutomationLatentCommand
{
public:
	explicit FRTSVerifyPlacementCommand(FAutomationTestBase* InTest, const double InWorldDeadline)
		: Test(InTest)
		, WorldDeadline(InWorldDeadline)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = FindPIEWorld();
		if (World == nullptr)
		{
			if (FPlatformTime::Seconds() >= WorldDeadline)
			{
				Test->AddError(TEXT("PIE did not expose the Milestone 2 placement world."));
				return true;
			}
			return false;
		}

		if (!bPlacementCommitted)
		{
			return CommitPlacementScenario(*World);
		}
		if (FPlatformTime::Seconds() < ConstructionDeadline)
		{
			return false;
		}
		VerifyConstructionAndRemoval(*World);
		return true;
	}

private:
	bool CommitPlacementScenario(UWorld& World)
	{
		ARTSCombatUnit* CommandVehicle = FindPlayerCommandVehicle(World);
		URTSStructureSubsystem* Structures = World.GetSubsystem<URTSStructureSubsystem>();
		URTSEconomySubsystem* Economy = World.GetSubsystem<URTSEconomySubsystem>();
		if (CommandVehicle == nullptr || Structures == nullptr || Economy == nullptr)
		{
			if (FPlatformTime::Seconds() >= WorldDeadline)
			{
				Test->AddError(TEXT("The placement fixture did not initialize its command vehicle and subsystems."));
				return true;
			}
			return false;
		}

		const FRTSHeadquartersDeploymentResult Deployment = Structures->TryDeployHeadquarters(*CommandVehicle);
		if (!Test->TestTrue(TEXT("Placement verification begins from a deployed HQ"), Deployment.bAccepted))
		{
			return true;
		}

		FRTSEconomyDelta TestFunding;
		TestFunding.Materials = 2000;
		Test->TestTrue(
			TEXT("The public economy interface can fund the bounded placement fixture"),
			Economy->TryCommit(Economy->AllocateTransactionId(), RTSTeams::Player, TestFunding).bAccepted);

		const FRTSPlacementRequest NoDeposit = PlacementRequest(
			ERTSStructureType::MaterialExtractor,
			FVector(-2500.0f, -9000.0f, 0.0f));
		const FRTSPlacementPreview NoDepositPreview = Structures->EvaluatePlacement(NoDeposit);
		Test->TestEqual(
			TEXT("An Extractor without a deposit has an explicit refusal"),
			NoDepositPreview.Refusal,
			ERTSPlacementRefusal::DepositRequired);
		Test->TestTrue(
			TEXT("A refused Extractor preview remains anchored to the requested location"),
			NoDepositPreview.SnappedGroundLocation.Equals(NoDeposit.DesiredWorldLocation, 5.0f));
		const FRTSPlacementRequest OutsideBuildArea = PlacementRequest(
			ERTSStructureType::PowerGenerator,
			FVector(7000.0f, -9000.0f, 0.0f));
		Test->TestEqual(
			TEXT("The complete footprint must remain in the build-area union"),
			Structures->EvaluatePlacement(OutsideBuildArea).Refusal,
			ERTSPlacementRefusal::OutsideBuildArea);

		Requests = {
			PlacementRequest(ERTSStructureType::MaterialExtractor, FVector(0.0f, -12000.0f, 100.0f)),
			PlacementRequest(ERTSStructureType::PowerGenerator, FVector(-2500.0f, -9000.0f, 0.0f)),
			PlacementRequest(ERTSStructureType::Factory, FVector(2000.0f, -9000.0f, 0.0f)),
			PlacementRequest(ERTSStructureType::SupplyDepot, FVector(0.0f, -5500.0f, 0.0f)),
			PlacementRequest(ERTSStructureType::DefensiveTurret, FVector(-1500.0f, -5500.0f, 0.0f))};
		for (const FRTSPlacementRequest& Request : Requests)
		{
			const FRTSPlacementPreview Preview = Structures->EvaluatePlacement(Request);
			Test->TestTrue(TEXT("Every configured non-HQ structure has a legal fixture location"), Preview.bValid);
			const FRTSPlacementResult Result = Structures->TryPlace(Request);
			Test->TestTrue(TEXT("Every legal structure placement commits"), Result.bAccepted);
			if (Result.bAccepted)
			{
				PlacedStructureIds.Add(Result.StableStructureId);
			}
		}
		const FRTSPlacementRequest FactoryExitBlockers[] = {
			PlacementRequest(ERTSStructureType::DefensiveTurret, FVector(1200.0f, -9000.0f, 0.0f)),
			PlacementRequest(ERTSStructureType::DefensiveTurret, FVector(2800.0f, -9000.0f, 0.0f)),
			PlacementRequest(ERTSStructureType::DefensiveTurret, FVector(2000.0f, -8300.0f, 0.0f)),
			PlacementRequest(ERTSStructureType::DefensiveTurret, FVector(2000.0f, -9700.0f, 0.0f))};
		for (int32 BlockerIndex = 0; BlockerIndex < 3; ++BlockerIndex)
		{
			const FRTSPlacementResult Result = Structures->TryPlace(FactoryExitBlockers[BlockerIndex]);
			Test->TestTrue(TEXT("A Factory remains legal while at least one exit apron is open"), Result.bAccepted);
			if (Result.bAccepted)
			{
				PlacedStructureIds.Add(Result.StableStructureId);
			}
		}
		Test->TestEqual(
			TEXT("Placement cannot consume a Factory's last usable exit apron"),
			Structures->EvaluatePlacement(FactoryExitBlockers[3]).Refusal,
			ERTSPlacementRefusal::FactoryExitBlocked);

		const int32 ExpectedMaterials = FRTSMilestone2Configuration::Load().Economy.StartingMaterials
			+ TestFunding.Materials - 2200;
		Test->TestEqual(
			TEXT("Five construction costs are deducted exactly once"),
			Economy->GetSnapshot(RTSTeams::Player).Materials,
			ExpectedMaterials);
		Test->TestEqual(
			TEXT("A repeated occupied request is explicitly blocked"),
			Structures->EvaluatePlacement(Requests[1]).Refusal,
			ERTSPlacementRefusal::FootprintBlocked);
		Test->TestEqual(
			TEXT("A claimed deposit is distinguishable from generic occupancy"),
			Structures->EvaluatePlacement(Requests[0]).Refusal,
			ERTSPlacementRefusal::DepositClaimed);
		Test->TestEqual(
			TEXT("Rejected repeats spend nothing"),
			Economy->GetSnapshot(RTSTeams::Player).Materials,
			ExpectedMaterials);
		Test->TestEqual(
			TEXT("The stable registry contains the HQ and eight construction actors"),
			Structures->Query(RTSTeams::Player).Num(),
			9);

		bPlacementCommitted = true;
		ConstructionDeadline = FPlatformTime::Seconds() + 11.0;
		return false;
	}

	void VerifyConstructionAndRemoval(UWorld& World)
	{
		URTSStructureSubsystem* Structures = World.GetSubsystem<URTSStructureSubsystem>();
		URTSEconomySubsystem* Economy = World.GetSubsystem<URTSEconomySubsystem>();
		if (Structures == nullptr || Economy == nullptr)
		{
			Test->AddError(TEXT("Placement subsystems disappeared before construction verification."));
			return;
		}

		for (const int32 StructureId : PlacedStructureIds)
		{
			const TOptional<FRTSStructureSnapshot> Snapshot = Structures->Find(StructureId);
			if (Test->TestTrue(TEXT("Every registered structure remains queryable"), Snapshot.IsSet()))
			{
				Test->TestTrue(TEXT("Every structure completes its configured construction timer"), Snapshot->bConstructed);
				Test->TestEqual(TEXT("Completed construction reports full progress"), Snapshot->ConstructionProgress, 1.0f);
			}
		}
		const FRTSEconomySnapshot ConstructedEconomy = Economy->GetSnapshot(RTSTeams::Player);
		Test->TestEqual(TEXT("Completed providers contribute Power exactly once"), ConstructedEconomy.PowerGeneration, 220);
		Test->TestEqual(TEXT("Completed consumers contribute nominal Power demand exactly once"), ConstructedEconomy.PowerDemand, 180);
		Test->TestEqual(TEXT("The completed Supply Depot extends capacity exactly once"), ConstructedEconomy.SupplyCapacity, 30);

		TArray<ARTSStructure*> Actors;
		ARTSStructure* Generator = nullptr;
		ARTSStructure* Extractor = nullptr;
		for (TActorIterator<ARTSStructure> StructureIterator(&World); StructureIterator; ++StructureIterator)
		{
			Actors.Add(*StructureIterator);
			if (StructureIterator->GetStructureType() == ERTSStructureType::PowerGenerator)
			{
				Generator = *StructureIterator;
			}
			else if (StructureIterator->GetStructureType() == ERTSStructureType::MaterialExtractor)
			{
				Extractor = *StructureIterator;
			}
			const UNavModifierComponent* Modifier = StructureIterator->FindComponentByClass<UNavModifierComponent>();
			Test->TestNotNull(TEXT("Every structure owns a dynamic navigation modifier"), Modifier);
			if (Modifier != nullptr)
			{
				Test->TestTrue(TEXT("Structure footprints carve the shared navmesh"), Modifier->AreaClass == UNavArea_Null::StaticClass());
			}
		}
		if (Generator != nullptr)
		{
			UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
			ANavigationData* NavigationData = Navigation != nullptr
				? Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)
				: nullptr;
			FNavLocation ProjectedStart;
			FNavLocation ProjectedEnd;
			const FVector GeneratorLocation = Generator->GetActorLocation();
			const bool bProjectedEndpoints = Navigation != nullptr
				&& NavigationData != nullptr
				&& Navigation->ProjectPointToNavigation(
					GeneratorLocation + FVector(-1000.0f, 0.0f, 0.0f),
					ProjectedStart,
					FVector(200.0f, 200.0f, 300.0f),
					NavigationData)
				&& Navigation->ProjectPointToNavigation(
					GeneratorLocation + FVector(1000.0f, 0.0f, 0.0f),
					ProjectedEnd,
					FVector(200.0f, 200.0f, 300.0f),
					NavigationData);
			Test->TestTrue(TEXT("Both sides of a completed Generator remain navigable"), bProjectedEndpoints);
			if (bProjectedEndpoints)
			{
				FPathFindingQuery PathQuery(
					nullptr,
					*NavigationData,
					ProjectedStart.Location,
					ProjectedEnd.Location);
				PathQuery.SetAllowPartialPaths(false);
				const FPathFindingResult PathResult = Navigation->FindPathSync(PathQuery);
				Test->TestTrue(
					TEXT("Runtime navigation routes around the completed structure footprint"),
					PathResult.IsSuccessful()
						&& !PathResult.IsPartial()
						&& PathResult.Path.IsValid()
						&& PathResult.Path->GetPathPoints().Num() > 2);
			}
		}

		if (Test->TestNotNull(TEXT("The constructed Generator can be destroyed"), Generator))
		{
			Generator->ApplyDamage(Generator->GetHealthComponent()->GetSnapshot().CurrentHealth);
			const FRTSPlacementResult Replacement = Structures->TryPlace(Requests[1]);
			Test->TestTrue(TEXT("Destroying a structure releases every occupied footprint cell"), Replacement.bAccepted);
		}
		if (Test->TestNotNull(TEXT("The constructed Extractor can be destroyed"), Extractor))
		{
			Extractor->ApplyDamage(Extractor->GetHealthComponent()->GetSnapshot().CurrentHealth);
			const FRTSPlacementResult Replacement = Structures->TryPlace(Requests[0]);
			Test->TestTrue(TEXT("Destroying an Extractor releases its typed deposit claim"), Replacement.bAccepted);
		}

		Actors.Reset();
		for (TActorIterator<ARTSStructure> StructureIterator(&World); StructureIterator; ++StructureIterator)
		{
			Actors.Add(*StructureIterator);
		}
		for (ARTSStructure* Structure : Actors)
		{
			if (IsValid(Structure))
			{
				Structure->ApplyDamage(Structure->GetHealthComponent()->GetSnapshot().CurrentHealth);
			}
		}
		Test->TestTrue(TEXT("Destroyed actors cannot remain in the stable registry"), Structures->Query(RTSTeams::Player).IsEmpty());
		const FRTSEconomySnapshot RemovedEconomy = Economy->GetSnapshot(RTSTeams::Player);
		Test->TestEqual(TEXT("Destruction removes all structure Power generation"), RemovedEconomy.PowerGeneration, 0);
		Test->TestEqual(TEXT("Destruction removes all structure Power demand"), RemovedEconomy.PowerDemand, 0);
		Test->TestEqual(TEXT("Destruction removes all structure Supply capacity"), RemovedEconomy.SupplyCapacity, 0);
	}

	FAutomationTestBase* Test = nullptr;
	double WorldDeadline = 0.0;
	double ConstructionDeadline = 0.0;
	bool bPlacementCommitted = false;
	TArray<FRTSPlacementRequest> Requests;
	TArray<int32> PlacedStructureIds;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone2PlacementTest,
	"Task0169.Headless.RTS.Milestone2.Structures.PlacementConstructionAndRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone2PlacementTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR PlayerLoopMapPackage[] = TEXT("/Game/RTS/Maps/M2_PlayerLoop");
	if (!TestTrue(TEXT("The battlefield opens before placement verification"), AutomationOpenMap(PlayerLoopMapPackage, true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyPlacementCommand(this, FPlatformTime::Seconds() + 10.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
