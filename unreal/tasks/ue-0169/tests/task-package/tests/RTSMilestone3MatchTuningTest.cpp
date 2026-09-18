// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/RTSAIStrategySubsystem.h"
#include "Configuration/RTSMilestone3Configuration.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformTime.h"
#include "Match/RTSMatchSubsystem.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Structures/RTSStructureSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSUnitRegistrySubsystem.h"
#include "Units/RTSTeams.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RTSMilestone3MatchTuningTestPrivate
{
// UE 5.8's CharacterMovement can discard simulation steps when host hitches are amplified at 5x.
// Three times normal speed keeps the retained full-match sample practical without changing the
// movement outcome the measurement is meant to observe.
constexpr float MeasurementTimeDilation = 3.0f;
constexpr double MeasurementFixedDeltaSeconds = 1.0 / 60.0;
constexpr double MaximumMeasuredGameSeconds = 15.0 * 60.0;

struct FFactionMeasurement
{
	double HeadquartersSeconds = -1.0;
	double ExtractorSeconds = -1.0;
	double FactorySeconds = -1.0;
	double FirstUnitSeconds = -1.0;
	double FirstTurretSeconds = -1.0;
	double FirstAttackSeconds = -1.0;
	double MaterialsBlockedSeconds = 0.0;
	double PowerBlockedSeconds = 0.0;
	double SupplyBlockedSeconds = 0.0;
	double QueueBlockedSeconds = 0.0;
	double PlacementBlockedSeconds = 0.0;
	double AttackStallSeconds = 0.0;
	double IdleSeconds = 0.0;
	FVector HeadquartersLocation = FVector::ZeroVector;
};

struct FMatchMeasurement
{
	int32 Seed = 0;
	double FirstCombatSeconds = -1.0;
	double LastSampleSeconds = 0.0;
	FFactionMeasurement Player;
	FFactionMeasurement Enemy;
};

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

ARTSCombatUnit* FindCommandVehicle(UWorld& World, const FGenericTeamId TeamId)
{
	URTSUnitRegistrySubsystem* Units = World.GetSubsystem<URTSUnitRegistrySubsystem>();
	const TArray<FRTSUnitSnapshot> Matches = Units != nullptr
		? Units->Query(TeamId, ERTSUnitType::CommandVehicle)
		: TArray<FRTSUnitSnapshot>();
	return Units != nullptr && !Matches.IsEmpty()
		? Units->FindActor(Matches[0].StableUnitId)
		: nullptr;
}

const FRTSFactionMatchStatistics& StatisticsFor(
	const FRTSMatchSnapshot& Snapshot,
	const FGenericTeamId TeamId)
{
	return TeamId == RTSTeams::Player
		? Snapshot.PlayerStatistics
		: Snapshot.EnemyStatistics;
}

bool HasConstructed(
	const URTSStructureSubsystem& Structures,
	const FGenericTeamId TeamId,
	const ERTSStructureType Type,
	FRTSStructureSnapshot* OutFirst = nullptr)
{
	for (const FRTSStructureSnapshot& Structure : Structures.Query(TeamId, Type))
	{
		if (Structure.bAlive && Structure.bConstructed)
		{
			if (OutFirst != nullptr)
			{
				*OutFirst = Structure;
			}
			return true;
		}
	}
	return false;
}

void RecordFirst(double& Milestone, const bool bReached, const double ElapsedSeconds)
{
	if (Milestone < 0.0 && bReached)
	{
		Milestone = ElapsedSeconds;
	}
}

void SampleFaction(
	UWorld& World,
	const FGenericTeamId TeamId,
	const double ElapsedSeconds,
	const double DeltaSeconds,
	FFactionMeasurement& Measurement)
{
	const URTSAIStrategySubsystem* Strategy = World.GetSubsystem<URTSAIStrategySubsystem>();
	const URTSStructureSubsystem* Structures = World.GetSubsystem<URTSStructureSubsystem>();
	const URTSUnitRegistrySubsystem* Units = World.GetSubsystem<URTSUnitRegistrySubsystem>();
	const URTSMatchSubsystem* Match = World.GetSubsystem<URTSMatchSubsystem>();
	const URTSEconomySubsystem* Economy = World.GetSubsystem<URTSEconomySubsystem>();
	if (Strategy == nullptr || Structures == nullptr || Units == nullptr || Match == nullptr || Economy == nullptr)
	{
		return;
	}

	const FRTSAISnapshot StrategySnapshot = Strategy->GetSnapshot(TeamId);
	const FRTSMatchSnapshot MatchSnapshot = Match->GetSnapshot();
	const FRTSEconomySnapshot EconomySnapshot = Economy->GetSnapshot(TeamId);
	FRTSStructureSnapshot Headquarters;
	const bool bHasHeadquarters = HasConstructed(
		*Structures,
		TeamId,
		ERTSStructureType::Headquarters,
		&Headquarters);
	RecordFirst(Measurement.HeadquartersSeconds, bHasHeadquarters, ElapsedSeconds);
	if (bHasHeadquarters && Measurement.HeadquartersLocation.IsNearlyZero())
	{
		Measurement.HeadquartersLocation = Headquarters.WorldLocation;
	}
	RecordFirst(
		Measurement.ExtractorSeconds,
		HasConstructed(*Structures, TeamId, ERTSStructureType::MaterialExtractor),
		ElapsedSeconds);
	RecordFirst(
		Measurement.FactorySeconds,
		HasConstructed(*Structures, TeamId, ERTSStructureType::Factory),
		ElapsedSeconds);
	RecordFirst(
		Measurement.FirstTurretSeconds,
		HasConstructed(*Structures, TeamId, ERTSStructureType::DefensiveTurret),
		ElapsedSeconds);
	RecordFirst(
		Measurement.FirstUnitSeconds,
		StatisticsFor(MatchSnapshot, TeamId).UnitsProduced > 0,
		ElapsedSeconds);
	RecordFirst(
		Measurement.FirstAttackSeconds,
		StrategySnapshot.AttackLaunchCount > 0,
		ElapsedSeconds);

	if (StrategySnapshot.CurrentAction == ERTSAIAction::None)
	{
		Measurement.IdleSeconds += DeltaSeconds;
	}
	if (StrategySnapshot.ObservedProductionState == ERTSProductionState::PausedPower)
	{
		Measurement.PowerBlockedSeconds += DeltaSeconds;
	}
	if (StrategySnapshot.LastProductionRefusal == ERTSProductionRefusal::QueueFull)
	{
		Measurement.QueueBlockedSeconds += DeltaSeconds;
	}
	if (StrategySnapshot.LastProductionRefusal == ERTSProductionRefusal::EconomyRejected)
	{
		if (EconomySnapshot.SupplyUsed + EconomySnapshot.SupplyReserved >= EconomySnapshot.SupplyCapacity)
		{
			Measurement.SupplyBlockedSeconds += DeltaSeconds;
		}
		else
		{
			Measurement.MaterialsBlockedSeconds += DeltaSeconds;
		}
	}
	if (StrategySnapshot.LastPlacementRefusal == ERTSPlacementRefusal::EconomyRejected)
	{
		Measurement.MaterialsBlockedSeconds += DeltaSeconds;
	}
	if (StrategySnapshot.LastRefusal == ERTSAIRefusal::NoLegalStructureCandidate
		|| StrategySnapshot.LastRefusal == ERTSAIRefusal::StructurePlacementRejected
		|| StrategySnapshot.LastRefusal == ERTSAIRefusal::TurretPlacementRejected)
	{
		Measurement.PlacementBlockedSeconds += DeltaSeconds;
	}
	if (StrategySnapshot.LastRefusal == ERTSAIRefusal::AttackGroupUnavailable
		|| StrategySnapshot.LastRefusal == ERTSAIRefusal::AttackCommandRejected)
	{
		Measurement.AttackStallSeconds += DeltaSeconds;
	}
}

FString SecondsField(const double Seconds)
{
	return Seconds < 0.0 ? TEXT("") : FString::Printf(TEXT("%.2f"), Seconds);
}

FString LocationField(const FVector& Location)
{
	return FString::Printf(TEXT("%.0f|%.0f|%.0f"), Location.X, Location.Y, Location.Z);
}

FString TeamField(const FGenericTeamId TeamId)
{
	return FString::FromInt(TeamId.GetId());
}

bool WriteMeasurement(
	FAutomationTestBase& Test,
	const FMatchMeasurement& Measurement,
	const FRTSMatchSnapshot& MatchSnapshot,
	const FRTSAISnapshot& PlayerStrategy,
	const FRTSAISnapshot& EnemyStrategy,
	const bool bTimedOut)
{
	const FString Directory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Artifacts/RTS/M3Slice7"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*Directory);
	const FString Path = FPaths::Combine(
		Directory,
		FString::Printf(TEXT("normal-match-seed-%d.csv"), Measurement.Seed));
	const FString Header = TEXT(
		"seed,timed_out,duration_seconds,winner_team,loser_team,resolution,first_combat_seconds,"
		"player_hq_location,player_hq_seconds,player_extractor_seconds,player_factory_seconds,"
		"player_first_unit_seconds,player_first_turret_seconds,player_first_attack_seconds,"
		"player_material_stall_seconds,player_power_stall_seconds,player_supply_stall_seconds,"
		"player_queue_stall_seconds,player_placement_stall_seconds,player_idle_seconds,"
		"player_units_produced,player_units_lost,player_materials_reclaimed,player_attacks,player_defenses,player_reclaims,"
		"enemy_hq_location,enemy_hq_seconds,enemy_extractor_seconds,enemy_factory_seconds,"
		"enemy_first_unit_seconds,enemy_first_turret_seconds,enemy_first_attack_seconds,"
		"enemy_material_stall_seconds,enemy_power_stall_seconds,enemy_supply_stall_seconds,"
		"enemy_queue_stall_seconds,enemy_placement_stall_seconds,enemy_idle_seconds,"
		"enemy_units_produced,enemy_units_lost,enemy_materials_reclaimed,enemy_attacks,enemy_defenses,enemy_reclaims,"
		"player_final_action,player_final_refusal,player_final_placement_refusal,player_accepted_placements,player_accepted_production,"
		"enemy_final_action,enemy_final_refusal,enemy_final_placement_refusal,enemy_accepted_placements,enemy_accepted_production,"
		"player_muster_timeouts,player_attack_releases,player_attack_stall_seconds,"
		"enemy_muster_timeouts,enemy_attack_releases,enemy_attack_stall_seconds\n");
	const FFactionMeasurement& Player = Measurement.Player;
	const FFactionMeasurement& Enemy = Measurement.Enemy;
	const FRTSFactionMatchStatistics& PlayerStatistics = MatchSnapshot.PlayerStatistics;
	const FRTSFactionMatchStatistics& EnemyStatistics = MatchSnapshot.EnemyStatistics;
	const FString Row = FString::Printf(
		TEXT("%d,%d,%.2f,%s,%s,%d,%s,%s,%s,%s,%s,%s,%s,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%d,%d,%d,%s,%s,%s,%s,%s,%s,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%d,%d,%.2f\n"),
		Measurement.Seed,
		bTimedOut ? 1 : 0,
		bTimedOut ? Measurement.LastSampleSeconds : MatchSnapshot.ActiveDurationSeconds,
		*TeamField(MatchSnapshot.WinningTeamId),
		*TeamField(MatchSnapshot.LosingTeamId),
		static_cast<int32>(MatchSnapshot.Resolution),
		*SecondsField(Measurement.FirstCombatSeconds),
		*LocationField(Player.HeadquartersLocation),
		*SecondsField(Player.HeadquartersSeconds),
		*SecondsField(Player.ExtractorSeconds),
		*SecondsField(Player.FactorySeconds),
		*SecondsField(Player.FirstUnitSeconds),
		*SecondsField(Player.FirstTurretSeconds),
		*SecondsField(Player.FirstAttackSeconds),
		Player.MaterialsBlockedSeconds,
		Player.PowerBlockedSeconds,
		Player.SupplyBlockedSeconds,
		Player.QueueBlockedSeconds,
		Player.PlacementBlockedSeconds,
		Player.IdleSeconds,
		PlayerStatistics.UnitsProduced,
		PlayerStatistics.UnitsLost,
		PlayerStatistics.MaterialsReclaimed,
		PlayerStrategy.AttackLaunchCount,
		PlayerStrategy.DefenseResponseCount,
		PlayerStrategy.ReclaimOrderCount,
		*LocationField(Enemy.HeadquartersLocation),
		*SecondsField(Enemy.HeadquartersSeconds),
		*SecondsField(Enemy.ExtractorSeconds),
		*SecondsField(Enemy.FactorySeconds),
		*SecondsField(Enemy.FirstUnitSeconds),
		*SecondsField(Enemy.FirstTurretSeconds),
		*SecondsField(Enemy.FirstAttackSeconds),
		Enemy.MaterialsBlockedSeconds,
		Enemy.PowerBlockedSeconds,
		Enemy.SupplyBlockedSeconds,
		Enemy.QueueBlockedSeconds,
		Enemy.PlacementBlockedSeconds,
		Enemy.IdleSeconds,
		EnemyStatistics.UnitsProduced,
		EnemyStatistics.UnitsLost,
		EnemyStatistics.MaterialsReclaimed,
		EnemyStrategy.AttackLaunchCount,
		EnemyStrategy.DefenseResponseCount,
		EnemyStrategy.ReclaimOrderCount,
		static_cast<int32>(PlayerStrategy.CurrentAction),
		static_cast<int32>(PlayerStrategy.LastRefusal),
		static_cast<int32>(PlayerStrategy.LastPlacementRefusal),
		PlayerStrategy.AcceptedStructurePlacementCount,
		PlayerStrategy.AcceptedProductionEnqueueCount,
		static_cast<int32>(EnemyStrategy.CurrentAction),
		static_cast<int32>(EnemyStrategy.LastRefusal),
		static_cast<int32>(EnemyStrategy.LastPlacementRefusal),
		EnemyStrategy.AcceptedStructurePlacementCount,
		EnemyStrategy.AcceptedProductionEnqueueCount,
		PlayerStrategy.MusterTimeoutCount,
		PlayerStrategy.AttackReleaseCount,
		Player.AttackStallSeconds,
		EnemyStrategy.MusterTimeoutCount,
		EnemyStrategy.AttackReleaseCount,
		Enemy.AttackStallSeconds);
	const bool bWritten = FFileHelper::SaveStringToFile(Header + Row, *Path);
	Test.TestTrue(*FString::Printf(TEXT("Measurement written to %s"), *Path), bWritten);
	Test.AddInfo(FString::Printf(
		TEXT("M3 normal match seed=%d duration=%.2fs winner=%d output=%s"),
		Measurement.Seed,
		bTimedOut ? Measurement.LastSampleSeconds : MatchSnapshot.ActiveDurationSeconds,
		MatchSnapshot.WinningTeamId.GetId(),
		*Path));
	return bWritten;
}
}

class FRTSMilestone3NormalMatchMeasurementCommand final : public IAutomationLatentCommand
{
public:
	FRTSMilestone3NormalMatchMeasurementCommand(
		FAutomationTestBase* InTest,
		const int32 InSeed,
		const double InWallDeadline)
		: Test(InTest), Seed(InSeed), WallDeadline(InWallDeadline)
	{
		Measurement.Seed = Seed;
	}

	virtual bool Update() override
	{
		using namespace RTSMilestone3MatchTuningTestPrivate;
		UWorld* World = FindPIEWorld();
		if (World == nullptr)
		{
			return FinishIfTimedOut(TEXT("PIE did not expose the normal-match measurement world."));
		}
		URTSAIStrategySubsystem* Strategy = World->GetSubsystem<URTSAIStrategySubsystem>();
		URTSStructureSubsystem* Structures = World->GetSubsystem<URTSStructureSubsystem>();
		URTSUnitRegistrySubsystem* Units = World->GetSubsystem<URTSUnitRegistrySubsystem>();
		URTSMatchSubsystem* Match = World->GetSubsystem<URTSMatchSubsystem>();
		if (Strategy == nullptr || Structures == nullptr || Units == nullptr || Match == nullptr)
		{
			return FinishIfTimedOut(TEXT("The normal-match fixture lost an authoritative subsystem."));
		}

		if (!bStarted)
		{
			ARTSCombatUnit* PlayerVehicle = FindCommandVehicle(*World, RTSTeams::Player);
			ARTSCombatUnit* EnemyVehicle = FindCommandVehicle(*World, RTSTeams::Enemy);
			if (!Test->TestNotNull(TEXT("The player Command Vehicle exists"), PlayerVehicle)
				|| !Test->TestNotNull(TEXT("The enemy Command Vehicle exists"), EnemyVehicle))
			{
				return true;
			}
			Strategy->StopFaction(RTSTeams::Player);
			Strategy->StopFaction(RTSTeams::Enemy);
			PlayerVehicle->GetOrderComponent()->Cancel();
			EnemyVehicle->GetOrderComponent()->Cancel();
			FRTSAIProfile PlayerProfile = FRTSMilestone3Configuration::Load().Normal;
			FRTSAIProfile EnemyProfile = PlayerProfile;
			// An exactly mirrored strategy match can settle into a rebuild equilibrium that a human
			// player does not share. The measurement driver recognizes the command-structure objective
			// after one working-base raid while the shipped enemy profile remains unchanged.
			PlayerProfile.MinimumRaidsBeforeHeadquarters = 1;
			PlayerProfile.StrategySeed = Seed;
			EnemyProfile.StrategySeed = Seed;
			if (!Test->TestTrue(
					TEXT("The player normal strategy starts"),
					Strategy->StartFaction(RTSTeams::Player, PlayerProfile).bAccepted)
				|| !Test->TestTrue(
					TEXT("The enemy normal strategy starts"),
					Strategy->StartFaction(RTSTeams::Enemy, EnemyProfile).bAccepted))
			{
				return true;
			}
			PreviousFixedDeltaSeconds = FApp::GetFixedDeltaTime();
			bPreviouslyUsedFixedTimeStep = FApp::UseFixedTimeStep();
			FApp::SetFixedDeltaTime(MeasurementFixedDeltaSeconds);
			FApp::SetUseFixedTimeStep(true);
			bTimingConfigured = true;
			World->GetWorldSettings()->SetTimeDilation(MeasurementTimeDilation);
			MeasurementStartSeconds = World->GetTimeSeconds();
			LastWorldSampleSeconds = MeasurementStartSeconds;
			bStarted = true;
			return false;
		}

		const double WorldSeconds = World->GetTimeSeconds();
		const double ElapsedSeconds = FMath::Max(0.0, WorldSeconds - MeasurementStartSeconds);
		const double DeltaSeconds = FMath::Max(0.0, WorldSeconds - LastWorldSampleSeconds);
		LastWorldSampleSeconds = WorldSeconds;
		Measurement.LastSampleSeconds = ElapsedSeconds;
		SampleFaction(*World, RTSTeams::Player, ElapsedSeconds, DeltaSeconds, Measurement.Player);
		SampleFaction(*World, RTSTeams::Enemy, ElapsedSeconds, DeltaSeconds, Measurement.Enemy);
		const FRTSMatchSnapshot MatchSnapshot = Match->GetSnapshot();
		if (Measurement.FirstCombatSeconds < 0.0
			&& (MatchSnapshot.PlayerStatistics.UnitsLost > 0
				|| MatchSnapshot.EnemyStatistics.UnitsLost > 0))
		{
			Measurement.FirstCombatSeconds = ElapsedSeconds;
		}

		if (MatchSnapshot.State == ERTSMatchState::Resolved)
		{
			const FRTSAISnapshot PlayerStrategy = Strategy->GetSnapshot(RTSTeams::Player);
			const FRTSAISnapshot EnemyStrategy = Strategy->GetSnapshot(RTSTeams::Enemy);
			WriteMeasurement(*Test, Measurement, MatchSnapshot, PlayerStrategy, EnemyStrategy, false);
			Test->TestEqual(TEXT("The match result publishes once"), MatchSnapshot.ResolutionSequence, 1);
			Test->TestTrue(
				TEXT("The normal driver reaches Headquarters deployment for both factions"),
				Measurement.Player.HeadquartersSeconds >= 0.0
					&& Measurement.Enemy.HeadquartersSeconds >= 0.0);
			Test->TestTrue(
				TEXT("The normal driver reaches economy, production, turrets, attacks, and combat"),
				Measurement.Player.ExtractorSeconds >= 0.0
					&& Measurement.Enemy.ExtractorSeconds >= 0.0
					&& Measurement.Player.FactorySeconds >= 0.0
					&& Measurement.Enemy.FactorySeconds >= 0.0
					&& Measurement.Player.FirstUnitSeconds >= 0.0
					&& Measurement.Enemy.FirstUnitSeconds >= 0.0
					&& Measurement.Player.FirstTurretSeconds >= 0.0
					&& Measurement.Enemy.FirstTurretSeconds >= 0.0
					&& Measurement.Player.FirstAttackSeconds >= 0.0
					&& Measurement.Enemy.FirstAttackSeconds >= 0.0
					&& Measurement.FirstCombatSeconds >= 0.0);
			Test->AddInfo(TEXT(
				"Automated two-sided duration is diagnostic; only a normal human PIE/package match can accept the GDD pacing gate."));
			RestoreTiming(World);
			return true;
		}

		if (ElapsedSeconds >= MaximumMeasuredGameSeconds)
		{
			return FinishFailure(TEXT("The normal match did not resolve within fifteen game minutes."));
		}
		return FinishIfTimedOut(TEXT("The normal match exceeded its wall-clock measurement budget."));
	}

private:
	bool FinishIfTimedOut(const TCHAR* Failure)
	{
		if (FPlatformTime::Seconds() < WallDeadline)
		{
			return false;
		}
		return FinishFailure(Failure);
	}

	bool FinishFailure(const TCHAR* Failure)
	{
		using namespace RTSMilestone3MatchTuningTestPrivate;
		UWorld* World = FindPIEWorld();
		if (World != nullptr)
		{
			URTSAIStrategySubsystem* Strategy = World->GetSubsystem<URTSAIStrategySubsystem>();
			URTSMatchSubsystem* Match = World->GetSubsystem<URTSMatchSubsystem>();
			if (Strategy != nullptr && Match != nullptr)
			{
				WriteMeasurement(
					*Test,
					Measurement,
					Match->GetSnapshot(),
					Strategy->GetSnapshot(RTSTeams::Player),
					Strategy->GetSnapshot(RTSTeams::Enemy),
					true);
			}
		}
		RestoreTiming(World);
		Test->AddError(Failure);
		return true;
	}

	void RestoreTiming(UWorld* World)
	{
		if (World != nullptr)
		{
			World->GetWorldSettings()->SetTimeDilation(1.0f);
		}
		if (bTimingConfigured)
		{
			FApp::SetFixedDeltaTime(PreviousFixedDeltaSeconds);
			FApp::SetUseFixedTimeStep(bPreviouslyUsedFixedTimeStep);
			bTimingConfigured = false;
		}
	}

	FAutomationTestBase* Test = nullptr;
	int32 Seed = 0;
	double WallDeadline = 0.0;
	double MeasurementStartSeconds = 0.0;
	double LastWorldSampleSeconds = 0.0;
	double PreviousFixedDeltaSeconds = 0.0;
	bool bStarted = false;
	bool bPreviouslyUsedFixedTimeStep = false;
	bool bTimingConfigured = false;
	RTSMilestone3MatchTuningTestPrivate::FMatchMeasurement Measurement;
};

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FRTSMilestone3NormalMatchMeasurementTest,
	"Task0169.Headless.RTS.Milestone3.Match.Normal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FRTSMilestone3NormalMatchMeasurementTest::GetTests(
	TArray<FString>& OutBeautifiedNames,
	TArray<FString>& OutTestCommands) const
{
	const int32 RetainedSeeds[] = {1337, 2027, 4099, 6151, 8191};
	for (const int32 Seed : RetainedSeeds)
	{
		OutBeautifiedNames.Add(FString::Printf(TEXT("Seed%d"), Seed));
		OutTestCommands.Add(FString::FromInt(Seed));
	}
}

bool FRTSMilestone3NormalMatchMeasurementTest::RunTest(const FString& Parameters)
{
	int32 Seed = 0;
	if (!LexTryParseString(Seed, *Parameters))
	{
		AddError(FString::Printf(TEXT("Invalid retained match seed: %s"), *Parameters));
		return false;
	}
	if (!TestTrue(
			TEXT("The normal-match measurement map opens"),
			AutomationOpenMap(TEXT("/Game/RTS/Maps/M3_Skirmish"), true)))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FRTSMilestone3NormalMatchMeasurementCommand(
		this,
		Seed,
		FPlatformTime::Seconds() + 360.0));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
