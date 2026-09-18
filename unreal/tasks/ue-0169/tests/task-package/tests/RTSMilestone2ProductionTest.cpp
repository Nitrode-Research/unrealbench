// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSTurretCombatComponent.h"
#include "Camera/RTSCameraPawn.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Production/RTSProductionComponent.h"
#include "Selection/RTSSelectionSubsystem.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
UWorld* FindProductionPIEWorld()
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

ARTSCombatUnit* FindCommandVehicle(UWorld& World)
{
	for (TActorIterator<ARTSCombatUnit> Iterator(&World); Iterator; ++Iterator)
	{
		if (Iterator->GetGenericTeamId() == RTSTeams::Player
			&& Iterator->IsMilestone2Unit()
			&& Iterator->GetUnitType() == ERTSUnitType::CommandVehicle)
		{
			return *Iterator;
		}
	}
	return nullptr;
}

ARTSStructure* FindStructure(UWorld& World, const int32 StableStructureId)
{
	for (TActorIterator<ARTSStructure> Iterator(&World); Iterator; ++Iterator)
	{
		if (Iterator->GetStableStructureId() == StableStructureId)
		{
			return *Iterator;
		}
	}
	return nullptr;
}

int32 CountPlayerUnits(UWorld& World, const ERTSUnitType UnitType)
{
	int32 Count = 0;
	for (TActorIterator<ARTSCombatUnit> Iterator(&World); Iterator; ++Iterator)
	{
		if (Iterator->IsAlive()
			&& Iterator->GetGenericTeamId() == RTSTeams::Player
			&& Iterator->IsMilestone2Unit()
			&& Iterator->GetUnitType() == UnitType)
		{
			++Count;
		}
	}
	return Count;
}

FRTSPlacementRequest MakePlacement(const ERTSStructureType Type, const FVector& Location)
{
	FRTSPlacementRequest Request;
	Request.TeamId = RTSTeams::Player;
	Request.StructureType = Type;
	Request.DesiredWorldLocation = Location;
	return Request;
}
}

class FRTSVerifyProductionCommand final : public IAutomationLatentCommand
{
public:
	explicit FRTSVerifyProductionCommand(FAutomationTestBase* InTest, const double InDeadline)
		: Test(InTest), Deadline(InDeadline)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = FindProductionPIEWorld();
		if (World == nullptr)
		{
			if (FPlatformTime::Seconds() >= Deadline)
			{
				Test->AddError(TEXT("PIE did not expose the production fixture."));
				return true;
			}
			return false;
		}
		if (APlayerController* Controller = World->GetFirstPlayerController())
		{
			if (ARTSCameraPawn* Camera = Cast<ARTSCameraPawn>(Controller->GetPawn()))
			{
				Camera->SetEdgePanInput(FVector2D::ZeroVector);
				Camera->SetActorLocation(FVector(1000.0f, -11500.0f, 100.0f));
			}
		}
		switch (Phase)
		{
		case EPhase::Setup: return Setup(*World);
		case EPhase::WaitConstruction: return VerifyConstruction(*World);
		case EPhase::ObservePowered: return ObservePowered(*World);
		case EPhase::AwaitRenderedCapture: return AwaitRenderedCapture(*World);
		case EPhase::ObserveBrownout: return ObserveBrownout(*World);
		case EPhase::WaitCompletion: return VerifyCompletion(*World);
		case EPhase::VerifyExactlyOnce: return VerifyExactlyOnce(*World);
		}
		return true;
	}

private:
	enum class EPhase : uint8
	{
		Setup,
		WaitConstruction,
		ObservePowered,
		AwaitRenderedCapture,
		ObserveBrownout,
		WaitCompletion,
		VerifyExactlyOnce
	};

	bool Setup(UWorld& World)
	{
		ARTSCombatUnit* CommandVehicle = FindCommandVehicle(World);
		URTSStructureSubsystem* Structures = World.GetSubsystem<URTSStructureSubsystem>();
		URTSEconomySubsystem* Economy = World.GetSubsystem<URTSEconomySubsystem>();
		if (CommandVehicle == nullptr || Structures == nullptr || Economy == nullptr)
		{
			if (FPlatformTime::Seconds() >= Deadline)
			{
				Test->AddError(TEXT("The production fixture did not expose its Command Vehicle, structures and economy before the setup deadline."));
				return true;
			}
			return false;
		}
		if (!Test->TestTrue(TEXT("Production starts from a deployed Headquarters"), Structures->TryDeployHeadquarters(*CommandVehicle).bAccepted))
		{
			return true;
		}
		FRTSEconomyDelta Funding;
		Funding.Materials = 10000;
		Test->TestTrue(TEXT("The production fixture is funded through the economy interface"), Economy->TryCommit(
			Economy->AllocateTransactionId(), RTSTeams::Player, Funding).bAccepted);

		const TPair<ERTSStructureType, FVector> Placements[] = {
			{ERTSStructureType::MaterialExtractor, FVector(0.0f, -12000.0f, 100.0f)},
			{ERTSStructureType::PowerGenerator, FVector(-2500.0f, -9000.0f, 0.0f)},
			{ERTSStructureType::Factory, FVector(2000.0f, -9000.0f, 0.0f)},
			{ERTSStructureType::Factory, FVector(2500.0f, -5500.0f, 0.0f)},
			{ERTSStructureType::SupplyDepot, FVector(0.0f, -5500.0f, 0.0f)},
			{ERTSStructureType::DefensiveTurret, FVector(-1500.0f, -5500.0f, 0.0f)}};
		for (const TPair<ERTSStructureType, FVector>& Placement : Placements)
		{
			const FRTSPlacementResult Result = Structures->TryPlace(MakePlacement(Placement.Key, Placement.Value));
			if (!Test->TestTrue(TEXT("Every Slice 3 fixture structure has a legal placement"), Result.bAccepted))
			{
				return true;
			}
			switch (Placement.Key)
			{
			case ERTSStructureType::PowerGenerator: GeneratorId = Result.StableStructureId; break;
			case ERTSStructureType::Factory:
				if (FactoryId == INDEX_NONE) FactoryId = Result.StableStructureId;
				else ExtraFactoryId = Result.StableStructureId;
				break;
			case ERTSStructureType::SupplyDepot: SupplyDepotId = Result.StableStructureId; break;
			case ERTSStructureType::DefensiveTurret: TurretId = Result.StableStructureId; break;
			default: break;
			}
		}
		ARTSStructure* UnderConstructionFactory = FindStructure(World, FactoryId);
		URTSProductionComponent* UnderConstructionProduction = UnderConstructionFactory != nullptr
			? UnderConstructionFactory->GetProductionComponent()
			: nullptr;
		if (!Test->TestNotNull(TEXT("A Factory owns production while it is under construction"), UnderConstructionProduction))
		{
			return true;
		}
		const FRTSEconomySnapshot BeforeEarlyQueue = Economy->GetSnapshot(RTSTeams::Player);
		const FRTSProductionResult EarlyQueue = UnderConstructionProduction->TryEnqueue(ERTSUnitType::InfantrySquad);
		Test->TestFalse(TEXT("An unfinished Factory cannot accept a production request"), EarlyQueue.bAccepted);
		Test->TestEqual(
			TEXT("An unfinished Factory reports that production is unavailable"),
			EarlyQueue.Refusal,
			ERTSProductionRefusal::FactoryUnavailable);
		Test->TestTrue(
			TEXT("An unfinished Factory keeps an empty queue"),
			UnderConstructionProduction->GetSnapshot().Queue.IsEmpty());
		const FRTSEconomySnapshot AfterEarlyQueue = Economy->GetSnapshot(RTSTeams::Player);
		Test->TestEqual(
			TEXT("A refused unfinished-Factory request spends no Materials"),
			AfterEarlyQueue.Materials,
			BeforeEarlyQueue.Materials);
		Test->TestEqual(
			TEXT("A refused unfinished-Factory request reserves no Supply"),
			AfterEarlyQueue.SupplyReserved,
			BeforeEarlyQueue.SupplyReserved);
		World.GetWorldSettings()->SetTimeDilation(10.0f);
		Deadline = FPlatformTime::Seconds() + 1.5;
		Phase = EPhase::WaitConstruction;
		return false;
	}

	bool VerifyConstruction(UWorld& World)
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		ARTSStructure* Factory = FindStructure(World, FactoryId);
		ARTSStructure* Turret = FindStructure(World, TurretId);
		Production = Factory != nullptr ? Factory->GetProductionComponent() : nullptr;
		TurretCombat = Turret != nullptr ? Turret->GetTurretCombatComponent() : nullptr;
		if (!Test->TestNotNull(TEXT("A Factory owns its production module"), Production.Get())
			|| !Test->TestNotNull(TEXT("A Defensive Turret owns its combat module"), TurretCombat.Get()))
		{
			return true;
		}
		if (APlayerController* Controller = World.GetFirstPlayerController())
		{
			if (ULocalPlayer* LocalPlayer = Controller->GetLocalPlayer())
			{
				if (URTSSelectionSubsystem* Selection = LocalPlayer->GetSubsystem<URTSSelectionSubsystem>())
				{
					Selection->ReplaceWithStructure(Factory);
				}
			}
		}

		BaselineInfantry = CountPlayerUnits(World, ERTSUnitType::InfantrySquad);
		BaselineLight = CountPlayerUnits(World, ERTSUnitType::LightVehicle);
		BaselineHeavy = CountPlayerUnits(World, ERTSUnitType::HeavyVehicle);
		URTSEconomySubsystem* Economy = World.GetSubsystem<URTSEconomySubsystem>();
		const FRTSEconomySnapshot BeforeQueue = Economy->GetSnapshot(RTSTeams::Player);
		BaselineSupplyUsed = BeforeQueue.SupplyUsed;
		for (const ERTSUnitType Type : {
			ERTSUnitType::InfantrySquad,
			ERTSUnitType::LightVehicle,
			ERTSUnitType::HeavyVehicle,
			ERTSUnitType::InfantrySquad,
			ERTSUnitType::InfantrySquad})
		{
			Test->TestTrue(TEXT("Each affordable unit enters the bounded queue"), Production->TryEnqueue(Type).bAccepted);
		}
		Test->TestEqual(TEXT("A sixth entry is refused by the queue boundary"),
			Production->TryEnqueue(ERTSUnitType::InfantrySquad).Refusal, ERTSProductionRefusal::QueueFull);
		Test->TestEqual(TEXT("The Command Vehicle cannot be factory-produced"),
			Production->TryEnqueue(ERTSUnitType::CommandVehicle).Refusal, ERTSProductionRefusal::InvalidUnitType);
		const FRTSEconomySnapshot Queued = Economy->GetSnapshot(RTSTeams::Player);
		Test->TestEqual(TEXT("Five accepted entries spend Materials exactly once"), Queued.Materials, BeforeQueue.Materials - 1050);
		Test->TestEqual(TEXT("Five accepted entries reserve their exact Supply"), Queued.SupplyReserved, 9);
		ARTSStructure* ExtraFactory = FindStructure(World, ExtraFactoryId);
		URTSProductionComponent* ExtraProduction = ExtraFactory != nullptr
			? ExtraFactory->GetProductionComponent()
			: nullptr;
		if (!Test->TestNotNull(TEXT("A second Factory exposes the same production interface"), ExtraProduction))
		{
			return true;
		}
		for (int32 EntryIndex = 0; EntryIndex < 4; ++EntryIndex)
		{
			Test->TestTrue(TEXT("Supply can be reserved across independent Factory queues"),
				ExtraProduction->TryEnqueue(ERTSUnitType::HeavyVehicle).bAccepted);
		}
		Test->TestEqual(TEXT("A queue entry that exceeds faction Supply is refused"),
			ExtraProduction->TryEnqueue(ERTSUnitType::HeavyVehicle).Refusal,
			ERTSProductionRefusal::EconomyRejected);
		Test->TestEqual(TEXT("A refused entry reserves no partial Supply"),
			Economy->GetSnapshot(RTSTeams::Player).SupplyReserved, 25);

		const FVector TargetLocation = Turret->GetActorLocation() + FVector(550.0f, 0.0f, 110.0f);
		Target = World.SpawnActorDeferred<ARTSCombatUnit>(
			ARTSCombatUnit::StaticClass(), FTransform(FRotator::ZeroRotator, TargetLocation), nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Test->TestNotNull(TEXT("The turret target fixture spawns"), Target.Get()))
		{
			return true;
		}
		Target->ConfigureMilestone2Unit(ERTSUnitType::HeavyVehicle, 9001, RTSTeams::Enemy);
		Target->FinishSpawning(FTransform(FRotator::ZeroRotator, TargetLocation));
		Deadline = FPlatformTime::Seconds() + 0.25;
		Phase = EPhase::ObservePowered;
		return false;
	}

	bool ObservePowered(UWorld& World)
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		Test->TestTrue(TEXT("A powered turret acquires and damages a hostile unit"),
			TurretCombat->GetSnapshot().ShotsFired > 0
			&& Target->GetHealthComponent()->GetSnapshot().CurrentHealth
				< Target->GetHealthComponent()->GetSnapshot().MaximumHealth);
		if (!FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
		{
			ScreenshotPath = FPaths::Combine(
				FPaths::ProjectSavedDir(),
				TEXT("Screenshots/RTS/M2ProductionAndTurret.png"));
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ScreenshotPath), true);
			IFileManager::Get().Delete(*ScreenshotPath, false, true);
			FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
			Deadline = FPlatformTime::Seconds() + 5.0;
			Phase = EPhase::AwaitRenderedCapture;
			return false;
		}
		return BeginBrownout(World);
	}

	bool AwaitRenderedCapture(UWorld& World)
	{
		if (IFileManager::Get().FileSize(*ScreenshotPath) <= 0)
		{
			if (FPlatformTime::Seconds() >= Deadline)
			{
				Test->AddError(TEXT("The rendered production screenshot was not written."));
				return true;
			}
			return false;
		}
		Test->AddInfo(FString::Printf(TEXT("Rendered production capture: %s"), *ScreenshotPath));
		return BeginBrownout(World);
	}

	bool BeginBrownout(UWorld& World)
	{
		const FRTSProductionSnapshot BeforeBrownout = Production->GetSnapshot();
		if (!Test->TestTrue(TEXT("An accepted production entry remains available for the brownout observation"), !BeforeBrownout.Queue.IsEmpty()))
		{
			return true;
		}
		PausedProgress = BeforeBrownout.Queue[0].Progress;
		HealthBeforeBrownout = Target->GetHealthComponent()->GetSnapshot().CurrentHealth;
		ARTSStructure* Generator = FindStructure(World, GeneratorId);
		if (Generator == nullptr)
		{
			Test->AddError(TEXT("The brownout fixture lost its Generator."));
			return true;
		}
		Generator->ApplyDamage(Generator->GetHealthComponent()->GetSnapshot().CurrentHealth);
		MaterialsBeforeBrownout = World.GetSubsystem<URTSEconomySubsystem>()->GetSnapshot(RTSTeams::Player).Materials;
		Deadline = FPlatformTime::Seconds() + 0.35;
		Phase = EPhase::ObserveBrownout;
		return false;
	}

	bool ObserveBrownout(UWorld& World)
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		const FRTSProductionSnapshot Paused = Production->GetSnapshot();
		Test->TestEqual(TEXT("A Factory explicitly reports a Power pause"), Paused.State, ERTSProductionState::PausedPower);
		Test->TestTrue(TEXT("Power interruption preserves front-entry progress"),
			!Paused.Queue.IsEmpty() && FMath::IsNearlyEqual(Paused.Queue[0].Progress, PausedProgress, 0.04f));
		Test->TestEqual(TEXT("A turret cannot deal damage during a brownout"),
			Target->GetHealthComponent()->GetSnapshot().CurrentHealth, HealthBeforeBrownout);
		Test->TestEqual(TEXT("The turret reports its unpowered state"),
			TurretCombat->GetSnapshot().State, ERTSTurretState::Unpowered);
		Test->TestTrue(TEXT("A Material Extractor preserves the recovery path during a brownout"),
			World.GetSubsystem<URTSEconomySubsystem>()->GetSnapshot(RTSTeams::Player).Materials
				> MaterialsBeforeBrownout);

		ARTSStructure* ExtraFactory = FindStructure(World, ExtraFactoryId);
		if (Test->TestNotNull(TEXT("The removable Power consumer remains available"), ExtraFactory))
		{
			ExtraFactory->ApplyDamage(ExtraFactory->GetHealthComponent()->GetSnapshot().CurrentHealth);
		}
		ARTSStructure* SupplyDepot = FindStructure(World, SupplyDepotId);
		if (Test->TestNotNull(TEXT("The removable Supply Depot remains available"), SupplyDepot))
		{
			SupplyDepot->ApplyDamage(SupplyDepot->GetHealthComponent()->GetSnapshot().CurrentHealth);
		}
		Test->TestEqual(TEXT("Destroying a Factory releases its unfinished Supply reservations"),
			World.GetSubsystem<URTSEconomySubsystem>()->GetSnapshot(RTSTeams::Player).SupplyReserved, 9);
		Deadline = FPlatformTime::Seconds() + 10.0;
		Phase = EPhase::WaitCompletion;
		return false;
	}

	bool VerifyCompletion(UWorld& World)
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		const FRTSProductionSnapshot Complete = Production->GetSnapshot();
		const FRTSEconomySnapshot Economy = World.GetSubsystem<URTSEconomySubsystem>()->GetSnapshot(RTSTeams::Player);
		Test->TestTrue(TEXT("Power restoration drains the complete Factory queue"), Complete.Queue.IsEmpty());
		Test->TestEqual(TEXT("Completed entries release every Supply reservation"), Economy.SupplyReserved, 0);
		Test->TestEqual(TEXT("Reserved Supply becomes live unit Supply exactly once"), Economy.SupplyUsed, BaselineSupplyUsed + 9);
		Test->TestEqual(TEXT("Three Infantry entries spawn three Infantry units"), CountPlayerUnits(World, ERTSUnitType::InfantrySquad), BaselineInfantry + 3);
		Test->TestEqual(TEXT("One Light entry spawns one Light Vehicle"), CountPlayerUnits(World, ERTSUnitType::LightVehicle), BaselineLight + 1);
		Test->TestEqual(TEXT("One Heavy entry spawns one Heavy Vehicle"), CountPlayerUnits(World, ERTSUnitType::HeavyVehicle), BaselineHeavy + 1);
		Deadline = FPlatformTime::Seconds() + 0.7;
		Phase = EPhase::VerifyExactlyOnce;
		return false;
	}

	bool VerifyExactlyOnce(UWorld& World)
	{
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		Test->TestEqual(TEXT("An already-completed queue cannot spawn another Infantry"), CountPlayerUnits(World, ERTSUnitType::InfantrySquad), BaselineInfantry + 3);
		Test->TestEqual(TEXT("An already-completed queue cannot spawn another Light Vehicle"), CountPlayerUnits(World, ERTSUnitType::LightVehicle), BaselineLight + 1);
		Test->TestEqual(TEXT("An already-completed queue cannot spawn another Heavy Vehicle"), CountPlayerUnits(World, ERTSUnitType::HeavyVehicle), BaselineHeavy + 1);
		return true;
	}

	FAutomationTestBase* Test = nullptr;
	EPhase Phase = EPhase::Setup;
	double Deadline = 0.0;
	int32 FactoryId = INDEX_NONE;
	int32 ExtraFactoryId = INDEX_NONE;
	int32 GeneratorId = INDEX_NONE;
	int32 TurretId = INDEX_NONE;
	int32 SupplyDepotId = INDEX_NONE;
	int32 BaselineInfantry = 0;
	int32 BaselineLight = 0;
	int32 BaselineHeavy = 0;
	int32 BaselineSupplyUsed = 0;
	float PausedProgress = 0.0f;
	float HealthBeforeBrownout = 0.0f;
	int32 MaterialsBeforeBrownout = 0;
	FString ScreenshotPath;
	TObjectPtr<URTSProductionComponent> Production;
	TObjectPtr<URTSTurretCombatComponent> TurretCombat;
	TObjectPtr<ARTSCombatUnit> Target;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone2ProductionTest,
	"Task0169.Headless.RTS.Milestone2.Production.QueuePowerAndTurret",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone2ProductionTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("The battlefield opens before production verification"), AutomationOpenMap(TEXT("/Game/RTS/Maps/M2_PlayerLoop"), true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyProductionCommand(this, FPlatformTime::Seconds() + 10.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
