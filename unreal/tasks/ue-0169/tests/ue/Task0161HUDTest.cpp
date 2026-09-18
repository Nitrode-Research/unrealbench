// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/RTSAIStrategySubsystem.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/RTSPlayerController.h"
#include "Match/RTSMatchSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Selection/RTSSelectionSubsystem.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UI/RTSHUDSnapshot.h"
#include "UI/SRTSStatusOverlay.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
UWorld* FindHUDPIEWorld()
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

FRTSHUDSnapshot MakeLayoutFixture(const ERTSHUDSelectionMode SelectionMode)
{
	FRTSHUDSnapshot Snapshot;
	Snapshot.bAvailable = true;
	Snapshot.Economy.TeamId = RTSTeams::Player;
	Snapshot.Economy.Materials = 1200;
	Snapshot.Economy.PowerGeneration = 220;
	Snapshot.Economy.PowerDemand = 240;
	Snapshot.Economy.SupplyUsed = 21;
	Snapshot.Economy.SupplyReserved = 3;
	Snapshot.Economy.SupplyCapacity = 100;
	Snapshot.MaterialsText = TEXT("MATERIALS  1200");
	Snapshot.PowerText = TEXT("POWER  220 GENERATED / 240 REQUIRED");
	Snapshot.SupplyText = TEXT("SUPPLY  21 USED + 3 QUEUED / 100 CAP");
	Snapshot.SelectionMode = SelectionMode;
	Snapshot.SelectionTitle = TEXT("FACTORY  S7     HP 100%");
	Snapshot.SelectionDetail = TEXT("PAUSED - LOW POWER");
	Snapshot.QueueText = TEXT("QUEUE 5 / 5     1 INFANTRY 16%     2 LIGHT VEHICLE 0%     3 HEAVY VEHICLE 0%     4 INFANTRY 0%     5 HEAVY VEHICLE 0%");
	Snapshot.AvailableActionsText = TEXT("PRODUCE     [Z] INFANTRY     [X] LIGHT     [C] HEAVY");
	Snapshot.ContextText = TEXT("PLACEMENT REFUSED     INSUFFICIENT MATERIALS");
	Snapshot.ContextTone = ERTSHUDMessageTone::Critical;
	Snapshot.WarningText = TEXT("LOW POWER     ADD 20 GENERATION     FACTORIES AND TURRETS PAUSED");
	Snapshot.WarningTone = ERTSHUDMessageTone::Critical;
	return Snapshot;
}

ARTSCombatUnit* SpawnHUDUnit(UWorld& World, const int32 StableUnitId, const FVector& Location)
{
	ARTSCombatUnit* Unit = World.SpawnActorDeferred<ARTSCombatUnit>(
		ARTSCombatUnit::StaticClass(),
		FTransform(FRotator::ZeroRotator, Location),
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Unit != nullptr)
	{
		Unit->ConfigureMilestone2Unit(ERTSUnitType::InfantrySquad, StableUnitId, RTSTeams::Player);
		Unit->FinishSpawning(FTransform(FRotator::ZeroRotator, Location));
	}
	return Unit;
}

class FVerifyLiveHUDSnapshotCommand final : public IAutomationLatentCommand
{
public:
	explicit FVerifyLiveHUDSnapshotCommand(FAutomationTestBase* InTest, const double InDeadline)
		: Test(InTest), Deadline(InDeadline)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = FindHUDPIEWorld();
		if (World == nullptr)
		{
			if (FPlatformTime::Seconds() >= Deadline)
			{
				Test->AddError(TEXT("PIE did not expose the HUD fixture."));
				return true;
			}
			return false;
		}

		ARTSPlayerController* Controller = Cast<ARTSPlayerController>(World->GetFirstPlayerController());
		ULocalPlayer* LocalPlayer = Controller != nullptr ? Controller->GetLocalPlayer() : nullptr;
		URTSSelectionSubsystem* Selection = LocalPlayer != nullptr
			? LocalPlayer->GetSubsystem<URTSSelectionSubsystem>()
			: nullptr;
		URTSEconomySubsystem* Economy = World->GetSubsystem<URTSEconomySubsystem>();
		URTSMatchSubsystem* Match = World->GetSubsystem<URTSMatchSubsystem>();
		URTSStructureSubsystem* StructureSystem = World->GetSubsystem<URTSStructureSubsystem>();
		if (Controller == nullptr || Selection == nullptr || Economy == nullptr || Match == nullptr || StructureSystem == nullptr)
		{
			if (FPlatformTime::Seconds() >= Deadline) { Test->AddError(TEXT("Incomplete HUD fixture")); return true; }
			return false;
		}
		if (URTSAIStrategySubsystem* Strategy = World->GetSubsystem<URTSAIStrategySubsystem>())
		{
			Strategy->StopFaction(RTSTeams::Enemy);
		}

		Selection->Clear();
		const FRTSHUDSnapshot Empty = FRTSHUDSnapshotAdapter::Capture(Controller);
		Test->TestTrue(TEXT("The live HUD adapter has an authoritative world"), Empty.bAvailable);
		Test->TestEqual(TEXT("Empty selection remains explicit"), Empty.SelectionMode, ERTSHUDSelectionMode::None);
		Test->TestTrue(TEXT("Empty selection does not invent contextual orders"), Empty.Actions.IsEmpty());
		Test->TestEqual(
			TEXT("The HUD mirrors player Materials"),
			Empty.Economy.Materials,
			Economy->GetSnapshot(RTSTeams::Player).Materials);

		ARTSCombatUnit* CommandVehicle = nullptr;
		for (TActorIterator<ARTSCombatUnit> Iterator(World); Iterator; ++Iterator)
		{
			if (Iterator->IsAlive()
				&& Iterator->GetGenericTeamId() == RTSTeams::Player
				&& Iterator->GetUnitType() == ERTSUnitType::CommandVehicle)
			{
				CommandVehicle = *Iterator;
				break;
			}
		}
		if (!Test->TestNotNull(TEXT("The HUD fixture has the player Command Vehicle"), CommandVehicle))
		{
			return true;
		}
		ARTSCombatUnit* FirstFixtureUnit = SpawnHUDUnit(
			*World,
			95001,
			CommandVehicle->GetActorLocation() + FVector(3000.0f, 0.0f, 100.0f));
		ARTSCombatUnit* SecondFixtureUnit = SpawnHUDUnit(
			*World,
			95002,
			CommandVehicle->GetActorLocation() + FVector(0.0f, 3000.0f, 100.0f));
		if (!Test->TestNotNull(TEXT("The HUD fixture creates its first typed unit"), FirstFixtureUnit)
			|| !Test->TestNotNull(TEXT("The HUD fixture creates its second typed unit"), SecondFixtureUnit))
		{
			return true;
		}

		TArray<ARTSCombatUnit*> SingleUnit{FirstFixtureUnit};
		Selection->ReplaceWith(SingleUnit);
		const FRTSHUDSnapshot Unit = FRTSHUDSnapshotAdapter::Capture(Controller);
		Test->TestEqual(TEXT("One selected unit uses the single-unit panel"), Unit.SelectionMode, ERTSHUDSelectionMode::SingleUnit);
		Test->TestEqual(TEXT("The selected stable unit ID is preserved"), Unit.SelectedStableUnitId, FirstFixtureUnit->GetStableUnitId());
		Test->TestTrue(TEXT("The single-unit panel exposes actions"), !Unit.AvailableActionsText.IsEmpty());
		Test->TestEqual(TEXT("Infantry exposes its three supported contextual orders"), Unit.Actions.Num(), 3);
		if (Unit.Actions.IsValidIndex(0)) { Test->TestEqual(TEXT("The first contextual order retains typed identity"), Unit.Actions[0].Kind, ERTSHUDActionKind::Move); }

		TArray<ARTSCombatUnit*> MultipleUnits{FirstFixtureUnit, SecondFixtureUnit};
		Selection->ReplaceWith(MultipleUnits);
		const FRTSHUDSnapshot Multiple = FRTSHUDSnapshotAdapter::Capture(Controller);
		Test->TestEqual(TEXT("Several selected units use the multiple panel"), Multiple.SelectionMode, ERTSHUDSelectionMode::Multiple);
		Test->TestEqual(TEXT("The multiple panel preserves the unit count"), Multiple.SelectedUnitCount, 2);

		Selection->Clear();
		const FRTSHeadquartersDeploymentResult Deployment = StructureSystem->TryDeployHeadquarters(*CommandVehicle);
		if (!Test->TestTrue(TEXT("The HUD fixture deploys through the normal transaction"), Deployment.bAccepted))
		{
			return true;
		}
		ARTSStructure* Headquarters = Match->GetHeadquarters(RTSTeams::Player);
		if (!Test->TestNotNull(TEXT("Deployment exposes the real Headquarters"), Headquarters))
		{
			return true;
		}
		Selection->ReplaceWithStructure(Headquarters);
		const FRTSHUDSnapshot Structure = FRTSHUDSnapshotAdapter::Capture(Controller);
		Test->TestEqual(TEXT("One selected structure uses the single-structure panel"), Structure.SelectionMode, ERTSHUDSelectionMode::SingleStructure);
		Test->TestEqual(TEXT("The selected stable structure ID is preserved"), Structure.SelectedStableStructureId, Headquarters->GetStableStructureId());

		Selection->ReplaceWith(MultipleUnits);
		Selection->ToggleStructure(Headquarters);
		const FRTSHUDSnapshot Mixed = FRTSHUDSnapshotAdapter::Capture(Controller);
		Test->TestEqual(TEXT("Units and a structure use the mixed multiple panel"), Mixed.SelectionMode, ERTSHUDSelectionMode::Multiple);
		Test->TestEqual(TEXT("Mixed selection preserves the structure count"), Mixed.SelectedStructureCount, 1);

		Match->ReportUnitProduced(RTSTeams::Player, 91001);
		Match->ReportUnitLost(RTSTeams::Player, 91002);
		Match->ReportMaterialsReclaimed(RTSTeams::Player, 91003, 125);
		ARTSCombatUnit* EnemyCommandVehicle = Match->GetCommandVehicle(RTSTeams::Enemy);
		if (!Test->TestNotNull(TEXT("The result fixture retains the enemy Command Vehicle"), EnemyCommandVehicle))
		{
			return true;
		}
		Test->TestTrue(
			TEXT("The result fixture resolves through match authority"),
			Match->ReportCommandVehicleDestroyed(*EnemyCommandVehicle).bAccepted);
		const FRTSHUDSnapshot Result = FRTSHUDSnapshotAdapter::Capture(Controller);
		Test->TestEqual(TEXT("The result adapter preserves the match state"), Result.Match.State, ERTSMatchState::Resolved);
		Test->TestTrue(TEXT("The player victory is explicit"), Result.bPlayerVictory);
		Test->TestEqual(TEXT("Produced totals come from the match snapshot"), Result.Match.PlayerStatistics.UnitsProduced, 1);
		Test->TestEqual(TEXT("Lost totals come from the match snapshot"), Result.Match.PlayerStatistics.UnitsLost, 1);
		Test->TestEqual(TEXT("Reclaim totals come from the match snapshot"), Result.Match.PlayerStatistics.MaterialsReclaimed, 125);
		return true;
	}

private:
	FAutomationTestBase* Test = nullptr;
	double Deadline = 0.0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone3HUDLayoutStatesTest,
	"Task0169.Headless.Task0161.HUD.LayoutStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone3HUDLayoutStatesTest::RunTest(const FString& Parameters)
{
	TArray<FRTSHUDSnapshot> Fixtures;
	Fixtures.Add(MakeLayoutFixture(ERTSHUDSelectionMode::None));
	Fixtures.Add(MakeLayoutFixture(ERTSHUDSelectionMode::SingleUnit));
	Fixtures.Add(MakeLayoutFixture(ERTSHUDSelectionMode::SingleStructure));
	Fixtures.Add(MakeLayoutFixture(ERTSHUDSelectionMode::Multiple));

	FRTSHUDSnapshot Victory = MakeLayoutFixture(ERTSHUDSelectionMode::Multiple);
	Victory.Match.State = ERTSMatchState::Resolved;
	Victory.bPlayerVictory = true;
	Victory.ResultTitle = TEXT("VICTORY");
	Victory.ResultReason = TEXT("ENEMY HEADQUARTERS DESTROYED");
	Victory.ResultDuration = TEXT("MATCH TIME  09:42");
	Victory.PlayerResultStatistics = TEXT("YOU     PRODUCED 24     LOST 17     RECLAIMED 1125 MATERIALS");
	Victory.EnemyResultStatistics = TEXT("ENEMY     PRODUCED 22     LOST 24     RECLAIMED 875 MATERIALS");
	Fixtures.Add(Victory);
	FRTSHUDSnapshot Defeat = Victory;
	Defeat.bPlayerVictory = false;
	Defeat.ResultTitle = TEXT("DEFEAT");
	Defeat.ResultReason = TEXT("YOUR HEADQUARTERS DESTROYED");
	Fixtures.Add(Defeat);

	for (const FRTSHUDSnapshot& Fixture : Fixtures)
	{
		const TSharedRef<SRTSStatusOverlay> Widget = SNew(SRTSStatusOverlay).Snapshot(Fixture);
		Widget->SlatePrepass(1.0f);
		const FVector2D DesiredSize = Widget->GetDesiredSize();
		TestTrue(TEXT("Every required HUD fixture constructs with positive width"), DesiredSize.X > 0.0f);
		TestTrue(TEXT("Every required HUD fixture constructs with positive height"), DesiredSize.Y > 0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone3HUDSnapshotAdapterTest,
	"Task0169.Headless.Task0161.HUD.SnapshotAdapter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSMilestone3HUDSnapshotAdapterTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("The skirmish opens before HUD verification"), AutomationOpenMap(TEXT("/Game/RTS/Maps/M3_Skirmish"), true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FVerifyLiveHUDSnapshotCommand(this, FPlatformTime::Seconds() + 40.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
