#if WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ModifierPythonRuntime.h"
#include "ModifierVisualReview.h"
#include "NightSkyEngine/Fixtures/ModifierCapture.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SOverlay.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"

#include "NightSkyEngine/Fixtures/ModifierFixture.h"
#include "NightSkyEngine/Battle/NightSkyPlayerController.h"
#include "NightSkyEngine/Data/PrimaryCharaData.h"
#include "NightSkyEngine/Battle/FighterRunners/FighterLocalRunner.h"
#include "NightSkyEngine/Miscellaneous/ReplayInfo.h"
#include "Kismet/GameplayStatics.h"

namespace ModifierTests
{
struct FPlayableBattle
{
	UModifierFixtureGameInstance* Instance;
	UWorld* World;
	AModifierFixtureBattle* Game;
	UGameViewportClient* Viewport;
	TSharedPtr<SOverlay> ViewportOverlay;
	explicit FPlayableBattle(const FModifierConfiguration& C)
	{
		Instance = NewObject<UModifierFixtureGameInstance>(GEngine);
		Instance->InitializeStandalone();
		World = Instance->GetWorld();
		Viewport = NewObject<UGameViewportClient>(GEngine);
		Instance->GetWorldContext()->GameViewport = Viewport;
		Viewport->Init(*Instance->GetWorldContext(), Instance, false);
		ViewportOverlay = SNew(SOverlay);
		Viewport->SetViewportOverlayWidget(nullptr, ViewportOverlay.ToSharedRef());
		Instance->BattleVersion = TEXT("ModifiersFixture1");
		Instance->IsTraining = true;
		Instance->IsReplay = true;
		Instance->FighterRunner = LocalPlay;
		Instance->BattleData.Modifiers = C;
		Instance->AvailableModifiers = C.Definitions;
		Instance->BattleData.Random = FRandomManager(41001);
		Instance->BattleData.BattleFormat = EBattleFormat::Rounds;
		Instance->BattleData.TimeUntilRoundStart = 0;
		Instance->BattleData.StartRoundTimer = 999;
		auto* Fighter = GetMutableDefault<UModifierFixtureCharaData>();
		Instance->BattleData.PlayerListP1.Add(Fighter);
		Instance->BattleData.PlayerListP2.Add(Fighter);
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
		World->SetGameMode(URL);
		World->InitializeActorsForPlay(URL);
		World->BeginPlay();
		World->SpawnActor<ANightSkyPlayerController>();
		World->SpawnActor<ANightSkyPlayerController>();
		Game = World->SpawnActor<AModifierFixtureBattle>();
		World->SetGameState(Game);
		Instance->IsTraining = false;
	}
	~FPlayableBattle()
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		Instance->Shutdown();
	}
	void Step(int32 P1 = 0, int32 P2 = 0)
	{
		Game->UpdateGameState(P1, P2, false);
	}
};
} // namespace ModifierTests

namespace ModifierTests
{
struct FBattle : FPlayableBattle
{
	APlayerObject* P[2];
	FBattle() : FPlayableBattle(FModifierConfiguration{})
	{
		P[0] = Game->GetMainPlayer(true);
		P[1] = Game->GetMainPlayer(false);
	}
	bool Start(const FModifierConfiguration& Configuration)
	{
		const int32 InitialMeter[2] = {Game->BattleState.Meter[0], Game->BattleState.Meter[1]};
		Instance->BattleData.Modifiers = Configuration;
		Instance->AvailableModifiers = Configuration.Definitions;
		Instance->IsTraining = true;
		Game->MatchInit();
		Instance->IsTraining = false;
		Game->BattleState.Meter[0] = InitialMeter[0];
		Game->BattleState.Meter[1] = InitialMeter[1];
		Step();
		return Game->GetModifierRejection().IsEmpty();
	}

	int32 Hit(int32 Damage = 21)
	{
		const int32 Before = P[1]->CurrentHealth;
		P[0]->ComboCounter = 0;
		P[1]->AttackOwner = P[0];
		P[1]->ReceivedHit.Damage = Damage;
		P[1]->ReceivedHit.Hitstop = 3;
		P[1]->ReceivedHit.EnemyHitstopModifier = 0;
		P[1]->ReceivedHit.MinimumDamagePercent = 0;
		P[1]->ReceivedHit.CustomHitAction =
		    FGameplayTag::RequestGameplayTag(TEXT("State.Universal.Stand"));
		P[1]->HandleHitAction(HACT_Custom);
		return Before - P[1]->CurrentHealth;
	}
};
FModifierDefinition Rule(const TCHAR* Identifier, EModifierEvent EventKind,
                         EModifierOperation OperationKind, int32 Amount, int32 Denominator = 1)
{
	FModifierDefinition Definition;
	Definition.Identifier = Identifier;
	Definition.DisplayName = Identifier;
	Definition.Subscription = EventKind;
	FModifierOperation Operation;
	Operation.Operation = OperationKind;
	Operation.Amount = Amount;
	Operation.Denominator = Denominator;
	Definition.Operations.Add(Operation);
	return Definition;
}
FModifierConfiguration Config(TArray<FModifierDefinition> Definitions)
{
	FModifierConfiguration Configuration;
	Configuration.Definitions = Definitions;
	for (const auto& Definition : Definitions)
	{
		FModifierInterval Interval;
		Interval.Identifier = Definition.Identifier;
		Configuration.Schedule.Add(Interval);
	}
	return Configuration;
}
} // namespace ModifierTests
using namespace ModifierTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierComposition, "NightSky.Modifiers.Composition",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierComposition::RunTest(const FString&)
{
	auto A = Rule(TEXT("A"), EModifierEvent::Damage, EModifierOperation::Add, 3);
	auto B = Rule(TEXT("B"), EModifierEvent::Damage, EModifierOperation::Multiply, 3, 2);
	for (int32 Permutation = 0; Permutation < 2; ++Permutation)
	{
		FBattle BattleFixture;
		TestTrue(TEXT("setup accepted"),
		         BattleFixture.Start(Config(Permutation ? TArray<FModifierDefinition>{B, A}
		                                                : TArray<FModifierDefinition>{A, B})));
		TestEqual(TEXT("21 + 3 then floor times 3/2"), BattleFixture.Hit(), 36);
		TestEqual(TEXT("real fighter health"), BattleFixture.P[1]->CurrentHealth, 964);
	}
	B.Priority = -1;
	{
		FBattle BattleFixture;
		BattleFixture.Start(Config({A, B}));
		TestEqual(TEXT("changed priorities change result"), BattleFixture.Hit(), 34);
	}
	A.Operations[0].Amount = -30;
	{
		FBattle BattleFixture;
		B.Priority = 1;
		BattleFixture.Start(Config({A, B}));
		TestEqual(TEXT("clamp after each operation"), BattleFixture.Hit(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierConversion, "NightSky.Modifiers.ConversionMeterChildren",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierConversion::RunTest(const FString&)
{
	for (bool Convert : {false, true})
	{
		FBattle BattleFixture;
		auto A = Rule(TEXT("Award"), EModifierEvent::Damage, EModifierOperation::ChildMeter, 7);
		A.Operations[0].ToAttacker = true;
		A.ConvertDamage = Convert;
		auto Scale = Rule(TEXT("Scale"), EModifierEvent::Damage, EModifierOperation::Multiply, 2);
		BattleFixture.Start(Config({A, Scale}));
		BattleFixture.P[0]->MeterPercentOnHit = 100;
		BattleFixture.P[1]->MeterPercentOnReceiveHit = 100;
		BattleFixture.Game->BattleState.Meter[0] = 10;
		BattleFixture.Game->BattleState.Meter[1] = 50;
		BattleFixture.Hit();
		TestEqual(TEXT("victim health"), BattleFixture.P[1]->CurrentHealth, Convert ? 1000 : 958);
		TestEqual(TEXT("victim meter"), BattleFixture.Game->BattleState.Meter[1], Convert ? 8 : 92);
		TestEqual(TEXT("attacker meter includes authored child"),
		          BattleFixture.Game->BattleState.Meter[0], Convert ? 17 : 59);
	}
	{
		auto Emit = Rule(TEXT("Emit"), EModifierEvent::Meter, EModifierOperation::ChildDamage, 21);
		auto Scale =
		    Rule(TEXT("Scale"), EModifierEvent::Damage, EModifierOperation::Multiply, MAX_int32);
		FBattle BattleFixture;
		BattleFixture.Start(Config({Emit, Scale}));
		BattleFixture.P[0]->MeterPercentOnHit = 100;
		BattleFixture.P[1]->MeterPercentOnReceiveHit = 100;
		BattleFixture.P[1]->AddMeter(1);
		TestEqual(TEXT("large transformed child damage clamps health"),
		          BattleFixture.P[1]->CurrentHealth, 0);
		TestEqual(TEXT("large transformed receive meter cannot overflow negative"),
		          BattleFixture.Game->BattleState.Meter[1], 100);
		TestEqual(TEXT("large transformed attack meter cannot overflow negative"),
		          BattleFixture.Game->BattleState.Meter[0], 100);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierAncestry, "NightSky.Modifiers.AncestryAndCancellation",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierAncestry::RunTest(const FString&)
{
	auto A = Rule(TEXT("A"), EModifierEvent::Damage, EModifierOperation::ChildMeter, 7);
	auto B = Rule(TEXT("B"), EModifierEvent::Meter, EModifierOperation::ChildDamage, 3);
	{
		FBattle BattleFixture;
		BattleFixture.Start(Config({A, B}));
		BattleFixture.Hit(10);
		TestEqual(TEXT("ancestry terminates"), BattleFixture.P[1]->CurrentHealth, 987);
		TestEqual(TEXT("child meter commits once"), BattleFixture.Game->BattleState.Meter[1], 7);
	}
	for (int32 Priority : {-1, 1})
	{
		auto Cancel = Rule(TEXT("Cancel"), EModifierEvent::Damage, EModifierOperation::Cancel, 0);
		Cancel.Priority = Priority;
		FBattle BattleFixture;
		BattleFixture.Start(Config({A, B, Cancel}));
		BattleFixture.Hit(10);
		TestEqual(TEXT("ordinary contact hitstop survives damage cancellation"),
		          BattleFixture.P[1]->Hitstop, 3);
		TestEqual(TEXT("canceled root"), BattleFixture.P[1]->CurrentHealth, 1000);
		TestEqual(TEXT("canceled pending children"), BattleFixture.Game->BattleState.Meter[1], 0);
	}
	auto Cancel = Rule(TEXT("Cancel"), EModifierEvent::Meter, EModifierOperation::Cancel, 0);
	Cancel.Priority = 1;
	{
		FBattle BattleFixture;
		BattleFixture.Start(Config({A, B, Cancel}));
		BattleFixture.Hit(10);
		TestEqual(TEXT("root survives child cancellation"), BattleFixture.P[1]->CurrentHealth, 990);
		TestEqual(TEXT("meter canceled"), BattleFixture.Game->BattleState.Meter[1], 0);
	}
	{
		FBattle BattleFixture;
		auto Lethal =
		    Rule(TEXT("LethalChild"), EModifierEvent::Meter, EModifierOperation::ChildDamage, 2000);
		BattleFixture.Start(Config({Lethal}));
		BattleFixture.P[1]->AddMeter(1);
		TestEqual(TEXT("meter-root child damage respects ordinary health floor"),
		          BattleFixture.P[1]->CurrentHealth, 0);
		BattleFixture.Step();
		const int32 AwardedRounds = BattleFixture.Game->BattleState.P1RoundsWon;
		BattleFixture.P[1]->AddMeter(1);
		TestEqual(TEXT("already depleted health cannot count as new committed health damage"),
		          BattleFixture.P[1]->CurrentHealth, 0);
		BattleFixture.Step();
		TestEqual(TEXT("depleted-health child cannot award another round"),
		          BattleFixture.Game->BattleState.P1RoundsWon, AwardedRounds);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierBranching, "NightSky.Modifiers.DepthFirstBranches",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierBranching::RunTest(const FString&)
{
	auto A = Rule(TEXT("A"), EModifierEvent::Damage, EModifierOperation::ChildMeter, 20);
	auto Minus = A.Operations[0];
	Minus.Amount = -20;
	A.Operations.Add(Minus);
	auto B = Rule(TEXT("B"), EModifierEvent::Meter, EModifierOperation::ChildMeter, 30);
	B.MatchAmount = true;
	B.RequiredAmount = 20;
	for (bool CancelBranch : {false, true})
	{
		auto C = Config({A, B});
		if (CancelBranch)
		{
			auto D = Rule(TEXT("Cancel"), EModifierEvent::Meter, EModifierOperation::Cancel, 0);
			D.MatchAmount = true;
			D.RequiredAmount = 20;
			D.Priority = 1;
			C = Config({A, B, D});
		}
		FBattle BattleFixture;
		BattleFixture.Start(C);
		BattleFixture.Game->BattleState.Meter[1] = 90;
		BattleFixture.Hit(10);
		TestEqual(TEXT("root committed"), BattleFixture.P[1]->CurrentHealth, 990);
		TestEqual(TEXT("branch completes before sibling, canceled grandchild disappears"),
		          BattleFixture.Game->BattleState.Meter[1], CancelBranch ? 70 : 80);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierValidation, "NightSky.Modifiers.ScheduleValidation",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierValidation::RunTest(const FString&)
{
	const auto A = Rule(TEXT("A"), EModifierEvent::Damage, EModifierOperation::Multiply, 3, 2);
	FString Reason;
	auto C = Config({A, A});
	TestFalse(TEXT("duplicate identifiers"), C.Validate(Reason));
	TestTrue(TEXT("names offending rule"), Reason.Contains(TEXT("A")));
	C = Config({A});
	C.Definitions[0].Operations[0].Denominator = 0;
	TestFalse(TEXT("zero denominator"), C.Validate(Reason));
	C = Config({A});
	C.Schedule[0].Revision = 2;
	TestFalse(TEXT("revision mismatch"), C.Validate(Reason));
	C = Config({A});
	C.Schedule[0].Start = -1;
	TestFalse(TEXT("negative start"), C.Validate(Reason));
	C = Config({A});
	C.Schedule[0].Duration = 0;
	TestFalse(TEXT("zero duration"), C.Validate(Reason));
	auto B = A;
	B.Identifier = TEXT("B");
	C = Config({A, B});
	C.Definitions[0].ExclusiveGroups = {TEXT("g")};
	C.Definitions[1].ExclusiveGroups = {TEXT("g")};
	C.Schedule[0].Round = 2;
	C.Schedule[1].Round = 2;
	TestFalse(TEXT("future round group overlap"), C.Validate(Reason));
	C.Schedule[0].Duration = 2;
	C.Schedule[1].Start = 2;
	TestTrue(TEXT("adjacent intervals"), C.Validate(Reason));
	C.Schedule[1].Round = 3;
	C.Schedule[1].Start = 0;
	TestTrue(TEXT("different rounds"), C.Validate(Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierLifecycle, "NightSky.Modifiers.ScheduleAndRollbackState",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierLifecycle::RunTest(const FString&)
{
	FModifierDefinition Drain;
	Drain.Identifier = TEXT("Drain");
	Drain.Drain = 5;
	FModifierDefinition Restrict;
	Restrict.Identifier = TEXT("Restriction");
	Restrict.RestrictMovement = true;
	auto C = Config({Drain, Restrict});
	C.Schedule[0].Duration = 4;
	C.Schedule[1].Start = 2;
	C.Schedule[1].Duration = 3;
	FBattle BattleFixture;
	BattleFixture.Game->BattleState.Meter[0] = 30;
	BattleFixture.Game->BattleState.Meter[1] = 30;
	BattleFixture.Start(C);
	TestEqual(TEXT("frame zero drain"), BattleFixture.Game->BattleState.Meter[1], 25);
	FRollbackData Before;
	int32 Checksum = 0;
	BattleFixture.Game->SaveGameState(Before, &Checksum);
	for (int32 Frame = 1; Frame <= 5; ++Frame)
	{
		BattleFixture.Step();
		TestEqual(TEXT("restriction boundaries"),
		          !BattleFixture.Game->GetMainPlayer(true)->CheckStateEnabled(
		              EStateType::ForwardWalk, FGameplayTag(),
		              FGameplayTag::RequestGameplayTag(TEXT("StateMachine.Primary"))),
		          Frame >= 2 && Frame < 5);
		if (Frame == 3)
			TestEqual(TEXT("four frame drains"), BattleFixture.Game->BattleState.Meter[1], 10);
	}
	BattleFixture.Game->LoadGameState(Before);
	TestEqual(TEXT("restored meter"), BattleFixture.Game->BattleState.Meter[1], 25);
	if (TestTrue(TEXT("restored active display"),
	             !BattleFixture.Game->GetActiveModifiers().IsEmpty()))
		TestEqual(TEXT("restored active duration"),
		          BattleFixture.Game->GetActiveModifiers()[0].RemainingFrames, 4);
	BattleFixture.Step();
	BattleFixture.Step();
	BattleFixture.Game->DeactivateModifier(TEXT("Restriction"));
	TestTrue(TEXT("deactivate remains current frame"),
	         !BattleFixture.Game->GetMainPlayer(true)->CheckStateEnabled(
	             EStateType::ForwardWalk, FGameplayTag(),
	             FGameplayTag::RequestGameplayTag(TEXT("StateMachine.Primary"))));
	BattleFixture.Step();
	TestFalse(TEXT("deactivate before next step"),
	          !BattleFixture.Game->GetMainPlayer(true)->CheckStateEnabled(
	              EStateType::ForwardWalk, FGameplayTag(),
	              FGameplayTag::RequestGameplayTag(TEXT("StateMachine.Primary"))));
	BattleFixture.Game->RoundInit();
	TestTrue(TEXT("round clears HUD"), BattleFixture.Game->GetActiveModifiers().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierFuzz, "NightSky.Modifiers.SeededArithmeticEpisodes",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierFuzz::RunTest(const FString&)
{
	FBattle BattleFixture;
	for (int32 Seed = 41001; Seed <= 41032; ++Seed)
	{
		FRandomStream R(Seed);
		for (int32 Episode = 0; Episode < 8; ++Episode)
		{
			const int32 Damage = R.RandRange(0, 30), Add = R.RandRange(-30, 30),
			            N = R.RandRange(0, 5), D = R.RandRange(1, 5);
			const int32 Expected = FMath::Max(0, Damage + Add) * N / D;
			auto A = Rule(TEXT("A"), EModifierEvent::Damage, EModifierOperation::Add, Add);
			auto B = Rule(TEXT("a"), EModifierEvent::Damage, EModifierOperation::Multiply, N, D);
			BattleFixture.Instance->BattleData.Random = FRandomManager(41001);
			BattleFixture.Start(Config(Episode % 2 ? TArray<FModifierDefinition>{A, B}
			                                       : TArray<FModifierDefinition>{B, A}));
			TestEqual(FString::Printf(TEXT("seed %d episode %d"), Seed, Episode),
			          BattleFixture.Hit(Damage), Expected);
		}
		if (HasAnyErrors())
			return false;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierRealCombat, "NightSky.Modifiers.PlayableContact",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierRealCombat::RunTest(const FString&)
{
	auto A = Rule(TEXT("A"), EModifierEvent::Damage, EModifierOperation::Add, 3);
	auto B = Rule(TEXT("B"), EModifierEvent::Damage, EModifierOperation::Multiply, 3, 2);
	FPlayableBattle BattleFixture(Config({A, B}));
	if (!TestNotNull(TEXT("real match fighter"), BattleFixture.Game->GetMainPlayer(false)))
		return false;
	TestEqual(TEXT("disclosed initial health"),
	          BattleFixture.Game->GetMainPlayer(false)->CurrentHealth, 1000);
	for (int32 I = 0; I < 12; ++I)
		BattleFixture.Step(I == 0 ? INP_A : 0, 0);
	TestEqual(TEXT("real collision scaled damage hook"),
	          BattleFixture.Game->GetMainPlayer(false)->CurrentHealth, 964);
	TestEqual(TEXT("attacker unchanged"), BattleFixture.Game->GetMainPlayer(true)->CurrentHealth,
	          1000);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierReplayContent, "NightSky.Modifiers.ReplayContentFidelity",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierReplayContent::RunTest(const FString&)
{
	auto D = Rule(TEXT("Scale"), EModifierEvent::Damage, EModifierOperation::Multiply, 2);
	const auto C = Config({D});
	auto* Replay = NewObject<UReplaySaveInfo>();
	Replay->BattleData.Modifiers = C;
	Replay->BattleData.Random = FRandomManager(41001);
	const FString Slot = TEXT("NSE041_") + FGuid::NewGuid().ToString();
	TestTrue(TEXT("serialize configuration with replay"),
	         UGameplayStatics::SaveGameToSlot(Replay, Slot, 0));
	auto* Loaded = Cast<UReplaySaveInfo>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	if (!TestNotNull(TEXT("load saved replay"), Loaded))
	{
		UGameplayStatics::DeleteGameInSlot(Slot, 0);
		return false;
	}
	TestEqual(TEXT("saved battle random seed"), Loaded->BattleData.Random.GetSeed(), uint32(41001));
	auto* GI = NewObject<UNightSkyGameInstance>();
	GI->AvailableModifiers = C.Definitions;
	GI->BattleData.Modifiers = C;
	FString Why;
	TestTrue(TEXT("matching peer configuration accepted"), GI->AcceptModifierPeer(C));
	auto Changed = C;
	Changed.Schedule[0].Start = 1;
	TestFalse(TEXT("peer schedule mismatch blocks setup"), GI->AcceptModifierPeer(Changed));
	TestFalse(TEXT("peer mismatch has reason"), GI->ModifierSetupError.IsEmpty());
	TestTrue(TEXT("compatible replay content"),
	         GI->ValidateModifierContent(Loaded->BattleData.Modifiers, Why));
	GI->AvailableModifiers[0].Revision = 2;
	TestFalse(TEXT("changed revision rejected"),
	          GI->ValidateModifierContent(Loaded->BattleData.Modifiers, Why));
	TestTrue(TEXT("actionable revision error"), Why.Contains(TEXT("Scale")));
	GI->AvailableModifiers.Reset();
	TestFalse(TEXT("missing definition rejected"),
	          GI->ValidateModifierContent(Loaded->BattleData.Modifiers, Why));
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierPlayableClock,
                                 "NightSky.Modifiers.PlayableClockPauseFreeze",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierPlayableClock::RunTest(const FString&)
{
	FModifierDefinition Drain;
	Drain.Identifier = TEXT("Drain");
	Drain.Drain = 5;
	FModifierDefinition Restriction;
	Restriction.Identifier = TEXT("Restriction");
	Restriction.RestrictMovement = true;
	auto C = Config({Drain, Restriction});
	C.Schedule[0].Duration = 4;
	C.Schedule[1].Start = 2;
	C.Schedule[1].Duration = 3;
	FPlayableBattle BattleFixture(C);
	BattleFixture.Game->GetMainPlayer(true)->AddMeter(30);
	BattleFixture.Game->GetMainPlayer(false)->AddMeter(30);
	for (int32 Frame = 0; Frame < 6; ++Frame)
	{
		BattleFixture.Step(Frame == 1 ? INP_C : 0, 0);
		TestEqual(TEXT("playable simulation frame"), BattleFixture.Game->GetPlayableRoundFrame(),
		          Frame);
		TestEqual(TEXT("drain across freeze"), BattleFixture.Game->BattleState.Meter[1],
		          30 - 5 * FMath::Min(Frame + 1, 4));
		TestEqual(TEXT("movement admission status"),
		          !BattleFixture.Game->GetMainPlayer(true)->CheckStateEnabled(
		              EStateType::ForwardWalk, FGameplayTag(),
		              FGameplayTag::RequestGameplayTag(TEXT("StateMachine.Primary"))),
		          Frame >= 2 && Frame < 5);
		if (Frame == 2)
		{
			BattleFixture.Game->SetPaused(true);
			for (int32 Render = 0; Render < 8; ++Render)
				BattleFixture.Step();
			TestEqual(TEXT("paused clock unchanged"), BattleFixture.Game->GetPlayableRoundFrame(),
			          2);
			TestEqual(TEXT("paused drain unchanged"), BattleFixture.Game->BattleState.Meter[1], 15);
			BattleFixture.Game->SetPaused(false);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierPlayableCombination,
                                 "NightSky.Modifiers.PlayableFourRuleVictory",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierPlayableCombination::RunTest(const FString&)
{
	auto C = FModifierConfiguration::FourRulePreset();
	for (auto& I : C.Schedule)
		if (I.Identifier == TEXT("Conversion"))
			I.Duration = 12;
	FPlayableBattle BattleFixture(C);
	BattleFixture.Game->GetMainPlayer(false)->AddMeter(10);
	BattleFixture.Step(INP_A, 0);
	TestEqual(TEXT("activation precedes first drain"), BattleFixture.Game->BattleState.Meter[1], 5);
	for (int32 Frame = 1; Frame < 12; ++Frame)
		BattleFixture.Step();
	TestEqual(TEXT("converted contact keeps health"),
	          BattleFixture.Game->GetMainPlayer(false)->CurrentHealth, 1000);
	TestEqual(TEXT("conversion cannot win sudden death"),
	          BattleFixture.Game->GetCurrentRoundResult(), 0);
	TestEqual(TEXT("converted debit clamps"), BattleFixture.Game->BattleState.Meter[1], 0);
	for (int32 Frame = 12; Frame < 25; ++Frame)
		BattleFixture.Step();
	BattleFixture.Step(INP_A | INP_B, 0);
	for (int32 Frame = 0; Frame < 10; ++Frame)
		BattleFixture.Step();
	TestEqual(TEXT("nonlethal actual health damage wins round"),
	          BattleFixture.Game->GetCurrentRoundResult(), int32(WIN_P1));
	TestEqual(TEXT("round winner awarded once"), BattleFixture.Game->BattleState.P1RoundsWon, 1);
	TestEqual(TEXT("sudden death does not zero health"),
	          BattleFixture.Game->GetMainPlayer(false)->CurrentHealth, 990);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierOwnedAttacks,
                                 "NightSky.Modifiers.PlayableOwnedAttackCleanup",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierOwnedAttacks::RunTest(const FString&)
{
	for (bool RemoveA : {false, true})
	{
		FModifierDefinition A;
		A.Identifier = TEXT("OwnerA");
		A.OwnedAttack = State_ModifierFixture_ProjectileA;
		A.RestrictMovement = true;
		FModifierDefinition B;
		B.Identifier = TEXT("OwnerB");
		B.OwnedAttack = State_ModifierFixture_ProjectileB;
		B.RestrictMovement = true;
		auto C = Config({A, B});
		C.Schedule[0].Duration = 25;
		C.Schedule[1].Duration = 25;
		FPlayableBattle BattleFixture(C);
		BattleFixture.Step();
		TestNotNull(TEXT("ordinary fighter projectile positive control"),
		            BattleFixture.Game->GetMainPlayer(true)->AddBattleObject(
		                State_ModifierFixture_ProjectileB));
		if (RemoveA)
			BattleFixture.Game->DeactivateModifier(TEXT("OwnerA"));
		BattleFixture.Step();
		TestTrue(TEXT("other owner's movement status remains"),
		         !BattleFixture.Game->GetMainPlayer(true)->CheckStateEnabled(
		             EStateType::ForwardWalk, FGameplayTag(),
		             FGameplayTag::RequestGameplayTag(TEXT("StateMachine.Primary"))));
		auto CountAttack = [&](FGameplayTag State, int32 Team)
		{
			int32 Count = 0;
			for (const auto* Object : BattleFixture.Game->Objects)
				if (Object->IsActive && Object->ObjectState && Object->ObjectState->Name == State &&
				    Object->Player == BattleFixture.Game->GetMainPlayer(Team == 0))
					++Count;
			return Count;
		};
		for (int32 Team = 0; Team < 2; ++Team)
		{
			TestEqual(TEXT("only the deactivated owner's authored attack disappears"),
			          CountAttack(State_ModifierFixture_ProjectileA, Team), RemoveA ? 0 : 1);
			TestEqual(TEXT("other owner's attack and ordinary fighter attack remain"),
			          CountAttack(State_ModifierFixture_ProjectileB, Team), Team == 0 ? 2 : 1);
		}
		for (int32 Frame = 2; Frame < 20; ++Frame)
			BattleFixture.Step();
		TestEqual(TEXT("only remaining opponents' projectiles commit"),
		          BattleFixture.Game->GetMainPlayer(true)->CurrentHealth, RemoveA ? 990 : 980);
		TestEqual(TEXT("fighter-owned projectile survives cleanup"),
		          BattleFixture.Game->GetMainPlayer(false)->CurrentHealth, RemoveA ? 980 : 970);
		for (int32 Frame = 20; Frame < 27; ++Frame)
			BattleFixture.Step();
		TestFalse(TEXT("last restriction expires"),
		          !BattleFixture.Game->GetMainPlayer(true)->CheckStateEnabled(
		              EStateType::ForwardWalk, FGameplayTag(),
		              FGameplayTag::RequestGameplayTag(TEXT("StateMachine.Primary"))));
		for (int32 Team = 0; Team < 2; ++Team)
		{
			TestEqual(TEXT("rule expiry removes every remaining authored A attack"),
			          CountAttack(State_ModifierFixture_ProjectileA, Team), 0);
			TestEqual(TEXT("rule expiry preserves only the ordinary fighter-owned B attack"),
			          CountAttack(State_ModifierFixture_ProjectileB, Team), Team == 0 ? 1 : 0);
		}
	}
	{
		FModifierDefinition Old;
		Old.Identifier = TEXT("OldOwner");
		Old.OwnedAttack = State_ModifierFixture_ProjectileA;
		FModifierDefinition Next = Old;
		Next.Identifier = TEXT("NextOwner");
		auto C = Config({Old, Next});
		C.Schedule[0].Duration = 45;
		C.Schedule[1].Start = 40;
		C.Schedule[1].Duration = 30;
		FPlayableBattle Reuse(C);
		Reuse.Step();
		for (int32 I = 0; I < 14; ++I)
			TestNotNull(TEXT("fill every available projectile lifetime"),
			            Reuse.Game->GetMainPlayer(true)->AddBattleObject(
			                State_ModifierFixture_ProjectileB));
		for (int32 Frame = 1; Frame < 36; ++Frame)
			Reuse.Step();
		int32 Live = 0;
		for (const auto* Object : Reuse.Game->Objects)
			if (Object->IsActive)
				++Live;
		TestEqual(TEXT("authored attacks self-expire while their old rule remains active"), Live,
		          0);
		const int32 BeforeP1 = Reuse.Game->GetMainPlayer(true)->CurrentHealth;
		const int32 BeforeP2 = Reuse.Game->GetMainPlayer(false)->CurrentHealth;
		TestTrue(TEXT("first lifetime committed an opposing projectile hit"), BeforeP1 < 1000);
		TestTrue(TEXT("first lifetime committed ordinary projectile hits"), BeforeP2 < 1000);
		for (int32 Frame = 36; Frame < 47; ++Frame)
			Reuse.Step();
		Live = 0;
		for (const auto* Object : Reuse.Game->Objects)
			if (Object->IsActive)
				++Live;
		TestEqual(TEXT("old owner expiry preserves both replacement owner attacks"), Live, 2);
		for (int32 Frame = 47; Frame < 57; ++Frame)
			Reuse.Step();
		TestEqual(TEXT("replacement lifetime initializes and commits its future P1 contact"),
		          Reuse.Game->GetMainPlayer(true)->CurrentHealth, BeforeP1 - 10);
		TestEqual(TEXT("replacement lifetime initializes and commits its future P2 contact"),
		          Reuse.Game->GetMainPlayer(false)->CurrentHealth, BeforeP2 - 10);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierPlayableRollback,
                                 "NightSky.Modifiers.PlayableRollbackOwnedEffects",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierPlayableRollback::RunTest(const FString&)
{
	FModifierDefinition A;
	A.Identifier = TEXT("Owned");
	A.OwnedAttack = State_ModifierFixture_ProjectileA;
	auto C = Config({A});
	C.Schedule[0].Duration = 20;
	FPlayableBattle BattleFixture(C);
	BattleFixture.Step();
	BattleFixture.Step();
	FRollbackData Saved;
	int32 Checksum = 0;
	BattleFixture.Game->SaveGameState(Saved, &Checksum);
	for (int32 Frame = 2; Frame < 15; ++Frame)
		BattleFixture.Step();
	TestEqual(TEXT("unremoved future attack positive control"),
	          BattleFixture.Game->GetMainPlayer(false)->CurrentHealth, 990);
	BattleFixture.Game->LoadGameState(Saved);
	TestEqual(TEXT("ordinary health restored with rule state"),
	          BattleFixture.Game->GetMainPlayer(false)->CurrentHealth, 1000);
	TestEqual(TEXT("rule display frame restored"), BattleFixture.Game->GetPlayableRoundFrame(), 1);
	for (int32 Frame = 2; Frame < 15; ++Frame)
		BattleFixture.Step();
	TestEqual(TEXT("restored owned attack still commits its future hit"),
	          BattleFixture.Game->GetMainPlayer(false)->CurrentHealth, 990);
	TestEqual(TEXT("restored opponent owned attack still commits"),
	          BattleFixture.Game->GetMainPlayer(true)->CurrentHealth, 990);
	BattleFixture.Game->LoadGameState(Saved);
	TestEqual(TEXT("second rollback restores health before corrected removal"),
	          BattleFixture.Game->GetMainPlayer(false)->CurrentHealth, 1000);
	BattleFixture.Game->DeactivateModifier(TEXT("Owned"));
	for (int32 Frame = 2; Frame < 15; ++Frame)
		BattleFixture.Step();
	TestEqual(TEXT("corrected removal prevents future hit"),
	          BattleFixture.Game->GetMainPlayer(false)->CurrentHealth, 1000);
	TestEqual(TEXT("other team has no resurrected hit"),
	          BattleFixture.Game->GetMainPlayer(true)->CurrentHealth, 1000);
	{
		FPlayableBattle Contact(C);
		for (int32 Frame = 0; Frame <= 8; ++Frame)
			Contact.Step();
		TestEqual(TEXT("already-hit checkpoint has one actual projectile contact per fighter"),
		          Contact.Game->GetMainPlayer(false)->CurrentHealth, 990);
		TestEqual(TEXT("already-hit checkpoint opposite contact"),
		          Contact.Game->GetMainPlayer(true)->CurrentHealth, 990);
		FRollbackData AfterContact;
		Contact.Game->SaveGameState(AfterContact, &Checksum);
		for (int32 Frame = 9; Frame <= 22; ++Frame)
			Contact.Step();
		TestEqual(TEXT("ordinary continuation does not repeat the consumed projectile contact"),
		          Contact.Game->GetMainPlayer(false)->CurrentHealth, 990);
		TestTrue(TEXT("speculative continuation expires the owning rule"),
		         Contact.Game->GetActiveModifiers().IsEmpty());
		Contact.Game->LoadGameState(AfterContact);
		TestEqual(TEXT("restoring after expiry restores already-hit fighter health"),
		          Contact.Game->GetMainPlayer(false)->CurrentHealth, 990);
		for (int32 Frame = 9; Frame <= 22; ++Frame)
			Contact.Step();
		TestEqual(TEXT("restored consumed projectile cannot hit P2 twice after speculative expiry"),
		          Contact.Game->GetMainPlayer(false)->CurrentHealth, 990);
		TestEqual(TEXT("restored consumed projectile cannot hit P1 twice after speculative expiry"),
		          Contact.Game->GetMainPlayer(true)->CurrentHealth, 990);
	}

	{
		FPlayableBattle Guard(C);
		Guard.Step();
		Guard.Step();
		auto* Attacker = Guard.Game->GetMainPlayer(true);
		ABattleObject* OwnedTarget = nullptr;
		for (auto* Object : Guard.Game->Objects)
			if (Object->IsActive && Object->ObjectState &&
			    Object->ObjectState->Name == State_ModifierFixture_ProjectileA &&
			    Object->Player == Guard.Game->GetMainPlayer(false))
			{
				OwnedTarget = Object;
				break;
			}
		if (!TestNotNull(TEXT("public owner pool contains target projectile"), OwnedTarget))
			return false;
		OwnedTarget->PosX = Attacker->PosX + 180000;
		OwnedTarget->PosY = Attacker->PosY;
		Attacker->SetFacing(DIR_Right);
		auto Contact = [&]()
		{
			Attacker->SetCelName(FGameplayTag::RequestGameplayTag(TEXT("State.Universal.Throw")));
			OwnedTarget->SetCelName(
			    FGameplayTag::RequestGameplayTag(TEXT("State.Universal.Stand")));
			Attacker->HandleHitCollision(OwnedTarget);
		};
		Attacker->SetAttacking(true);
		Attacker->EnableHit(true);
		Attacker->NormalHit.Hitstop = 3;
		Attacker->NormalHit.Damage = 0;
		Attacker->NormalHit.Hitstun = 0;
		Attacker->NormalHit.GroundHitAction = HACT_Custom;
		Attacker->NormalHit.AirHitAction = HACT_Custom;
		Attacker->NormalHit.CustomHitAction =
		    FGameplayTag::RequestGameplayTag(TEXT("State.Universal.Stand"));
		Attacker->CounterHit = Attacker->NormalHit;
		Contact();
		if (!TestEqual(TEXT("real fighter-to-owned-object collision produces three-frame stop"),
		               OwnedTarget->Hitstop, 3))
			return false;
		FRollbackData ConsumedObject;
		Guard.Game->SaveGameState(ConsumedObject, &Checksum);
		Guard.Step();
		Contact();
		TestEqual(TEXT("ordinary repeated contact does not renew consumed object hitstop"),
		          OwnedTarget->Hitstop, 2);
		for (int32 Frame = 3; Frame <= 22; ++Frame)
			Guard.Step();
		TestFalse(TEXT("speculative expiry resets consumed owned object"), OwnedTarget->IsActive);
		Guard.Game->LoadGameState(ConsumedObject);
		Guard.Step();
		Contact();
		TestEqual(
		    TEXT(
		        "restored consumed object collision cannot renew hitstop after speculative expiry"),
		    OwnedTarget->Hitstop, 2);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierReplayRunner, "NightSky.Modifiers.RecordAndReplayRunner",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierReplayRunner::RunTest(const FString&)
{
	auto A = Rule(TEXT("A"), EModifierEvent::Damage, EModifierOperation::Add, 3);
	auto B = Rule(TEXT("B"), EModifierEvent::Damage, EModifierOperation::Multiply, 3, 2);
	FPlayableBattle BattleFixture(Config({A, B}));
	BattleFixture.Instance->RecordReplay();
	BattleFixture.Instance->IsReplay = false;
	BattleFixture.Instance->RollbackReplay(9);
	auto* EmptyReplay = BattleFixture.Instance->GetRecordedReplay();
	if (!TestNotNull(TEXT("recording creates an ordinary replay tape"), EmptyReplay))
		return false;
	TestEqual(TEXT("empty ordinary tape cannot underflow during rollback"),
	          EmptyReplay->LengthInFrames, 0);
	for (int32 Frame = 0; Frame < 12; ++Frame)
		BattleFixture.Step(Frame == 0 ? INP_A : 0, 0);
	BattleFixture.Instance->IsReplay = true;
	auto* RecordedReplay = BattleFixture.Instance->GetRecordedReplay();
	if (!TestNotNull(TEXT("ordinary local runner retains its recorded replay"), RecordedReplay))
		return false;
	if (!TestEqual(TEXT("ordinary local runner records each input pair"),
	               RecordedReplay->LengthInFrames, 12))
		return false;
	const FString Slot = TEXT("NSE041Runner_") + FGuid::NewGuid().ToString();
	if (!TestTrue(TEXT("save complete ordinary tape"),
	              BattleFixture.Instance->SaveRecordedReplay(Slot)))
		return false;
	BattleFixture.Instance->BattleData.Modifiers = {};
	BattleFixture.Instance->PlayReplayFromBP(Slot);
	auto* LoadedReplay = BattleFixture.Instance->GetRecordedReplay();
	if (!TestNotNull(TEXT("load saved ordinary tape"), LoadedReplay))
	{
		UGameplayStatics::DeleteGameInSlot(Slot, 0);
		return false;
	}
	TestEqual(TEXT("recorded setup replaces current preset"),
	          BattleFixture.Instance->BattleData.Modifiers.Definitions.Num(), 2);
	if (!TestNotNull(TEXT("local runner exists before replay restart"),
	                 BattleFixture.Game->FighterRunner))
	{
		UGameplayStatics::DeleteGameInSlot(Slot, 0);
		return false;
	}
	BattleFixture.Game->FighterRunner->Destroy();
	BattleFixture.Game->FighterRunner = nullptr;
	BattleFixture.Instance->IsTraining = true;
	BattleFixture.Game->MatchInit();
	BattleFixture.Instance->IsTraining = false;
	if (!TestNotNull(TEXT("replay runner starts from the saved tape"),
	                 BattleFixture.Game->FighterRunner))
	{
		UGameplayStatics::DeleteGameInSlot(Slot, 0);
		return false;
	}
	FRollbackData PlaybackStart;
	int32 PlaybackChecksum = 0;
	BattleFixture.Game->SaveGameState(PlaybackStart, &PlaybackChecksum);
	for (int32 Frame = 0; Frame < 6; ++Frame)
		BattleFixture.Game->FighterRunner->Update(OneFrame);
	TestEqual(TEXT("replay checkpoint stimulus advanced six actual frames"),
	          BattleFixture.Game->BattleState.FrameNumber, 6);
	BattleFixture.Game->LoadGameState(PlaybackStart);
	LoadedReplay = BattleFixture.Instance->GetRecordedReplay();
	if (!TestNotNull(TEXT("rewinding playback retains the loaded tape"), LoadedReplay) ||
	    !TestNotNull(TEXT("rewinding playback retains the replay runner"),
	                 BattleFixture.Game->FighterRunner))
	{
		UGameplayStatics::DeleteGameInSlot(Slot, 0);
		return false;
	}
	TestEqual(TEXT("rewinding playback preserves all twelve immutable input pairs"),
	          LoadedReplay->LengthInFrames, 12);
	TestEqual(TEXT("rewinding playback restores frame zero"),
	          BattleFixture.Game->BattleState.FrameNumber, 0);
	for (int32 Frame = 0; Frame < 12; ++Frame)
		BattleFixture.Game->FighterRunner->Update(OneFrame);
	auto* ReplayVictim = BattleFixture.Game->GetMainPlayer(false);
	if (!TestNotNull(TEXT("saved replay retains its victim fighter"), ReplayVictim))
	{
		UGameplayStatics::DeleteGameInSlot(Slot, 0);
		return false;
	}
	TestEqual(TEXT("saved replay passes through actual hit simulation"),
	          ReplayVictim->CurrentHealth, 964);
	BattleFixture.Game->LoadGameState(PlaybackStart);
	if (!TestNotNull(TEXT("second rewind retains the replay runner"),
	                 BattleFixture.Game->FighterRunner))
	{
		UGameplayStatics::DeleteGameInSlot(Slot, 0);
		return false;
	}
	for (int32 Quarter = 0; Quarter < 48; ++Quarter)
	{
		if (Quarter == 24)
		{
			TestEqual(TEXT("quarter-frame presentation reaches exact halfway cursor"),
			          BattleFixture.Game->BattleState.FrameNumber, 6);
			BattleFixture.Game->SetPaused(true);
			for (int32 PauseUpdate = 0; PauseUpdate < 8; ++PauseUpdate)
				BattleFixture.Game->FighterRunner->Update(OneFrame / 4);
			TestEqual(TEXT("pause-only presentation does not consume replay time"),
			          BattleFixture.Game->BattleState.FrameNumber, 6);
			BattleFixture.Game->SetPaused(false);
		}
		BattleFixture.Game->FighterRunner->Update(OneFrame / 4);
	}
	TestEqual(TEXT("partitioned render time preserves twelve simulated replay frames"),
	          BattleFixture.Game->BattleState.FrameNumber, 12);
	TestEqual(TEXT("partitioned render time preserves independently expected modified hit"),
	          BattleFixture.Game->GetMainPlayer(false)->CurrentHealth, 964);
	const int32 EndFrame = BattleFixture.Game->BattleState.FrameNumber;
	BattleFixture.Game->FighterRunner->Update(OneFrame);
	TestEqual(TEXT("EOF does not append neutral gameplay"),
	          BattleFixture.Game->BattleState.FrameNumber, EndFrame);
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

#include "NightSkyEngine/UI/NightSkyBattleHudActor.h"
#include "NightSkyEngine/UI/NightSkyModifierWidget.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Internationalization/Regex.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace ModifierTests
{
// This reads actual visible widget text, without prescribing separators or pluralization.
bool VisibleDuration(const FString& Text, const FString& Name, int32 Frames)
{
	if (!Text.Contains(Name))
		return false;
	if (Frames < 0)
		return (Text.Contains(TEXT("round"), ESearchCase::IgnoreCase) &&
		        Text.Contains(TEXT("end"), ESearchCase::IgnoreCase)) ||
		       Text.Contains(TEXT("∞"));
	const int32 NameAt = Text.Find(Name);
	const FString AfterName = Text.Mid(NameAt + Name.Len());
	FRegexMatcher MarkedAfter(FRegexPattern(TEXT("([0-9]+)\\s*[Ff]rames?")), AfterName);
	if (MarkedAfter.FindNext())
		return FCString::Atoi(*MarkedAfter.GetCaptureGroup(1)) == Frames;
	FRegexMatcher MarkedBefore(FRegexPattern(TEXT("([0-9]+)\\s*[Ff]rames?")), Text.Left(NameAt));
	int32 Last = INDEX_NONE;
	while (MarkedBefore.FindNext())
		Last = FCString::Atoi(*MarkedBefore.GetCaptureGroup(1));
	if (Last != INDEX_NONE)
		return Last == Frames;
	// Unitless layouts remain supported; ignore explicitly labelled metadata.
	FString Values = AfterName;
	FRegexMatcher Metadata(FRegexPattern(TEXT("(?i)\\b(?:revision|rev|round|v)\\s*[:=#]?\\s*[0-9]+\\b")), Values);
	TArray<TPair<int32, int32>> Ranges;
	while (Metadata.FindNext())
		Ranges.Add({Metadata.GetMatchBeginning(), Metadata.GetMatchEnding()});
	for (int32 I = Ranges.Num() - 1; I >= 0; --I)
		Values.RemoveAt(Ranges[I].Key, Ranges[I].Value - Ranges[I].Key);
	FRegexMatcher After(FRegexPattern(TEXT("[0-9]+")), Values);
	if (After.FindNext())
		return FCString::Atoi(*After.GetCaptureGroup(0)) == Frames;
	FRegexMatcher Before(FRegexPattern(TEXT("[0-9]+")), Text.Left(NameAt));
	while (Before.FindNext())
		Last = FCString::Atoi(*Before.GetCaptureGroup(0));
	return Last == Frames;
}
} // namespace ModifierTests
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierHUD, "NightSkyIntegration.Modifiers.RenderedRuleDuration",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierHUD::RunTest(const FString&)
{
	FModifierDefinition D;
	D.Identifier = TEXT("Drain");
	D.DisplayName = TEXT("Resource drain");
	D.Drain = 0;
	auto C = Config({D});
	C.Schedule[0].Duration = 4;
	FPlayableBattle BattleFixture(C);
	BattleFixture.Step();
	BattleFixture.Step();
	BattleFixture.Game->SetPaused(true);
	auto* Widget = BattleFixture.Game->BattleHudActor->ModifierWidget;
	if (!TestNotNull(TEXT("actual battle HUD widget"), Widget))
		return false;
	const auto SlateWidget = Widget->TakeWidget();
	ModifierCapture::FullPaint(BattleFixture.ViewportOverlay.ToSharedRef());
	FWidgetRenderer Renderer(true);
	Renderer.DrawWidget(BattleFixture.ViewportOverlay.ToSharedRef(), FVector2D(768, 512));
	auto* Target =
	    Renderer.DrawWidget(BattleFixture.ViewportOverlay.ToSharedRef(), FVector2D(768, 512));
	if (!TestNotNull(TEXT("actual Slate render target"), Target))
		return false;
	FlushRenderingCommands();
	TestTrue(TEXT("visible rule name and remaining duration"),
	         VisibleDuration(Widget->GetRenderedRuleText().ToString(), TEXT("Resource drain"), 3) || ModifierVisualReview::Verify(Target, TEXT("Resource drain"), 3));
	TestEqual(TEXT("render capture observes second playable frame"),
	          BattleFixture.Game->GetPlayableRoundFrame(), 1);
	TArray<FColor> Pixels;
	TestTrue(TEXT("read actual rendered pixels"),
	         Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
	int32 Ink = 0;
	for (const FColor& P : Pixels)
		if (P.R > 20 || P.G > 20 || P.B > 20)
			++Ink;
	TestTrue(TEXT("text has rendered glyph pixels"), Ink > 30);
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Automation/ModifierHUD-frame1.png");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*Path));
	if (TestTrue(TEXT("open screenshot artifact"), File.IsValid()))
		TestTrue(TEXT("save actual HUD screenshot"),
		         FImageUtils::ExportRenderTarget2DAsPNG(Target, *File));
	TestEqual(TEXT("rendering does not advance simulation"),
	          BattleFixture.Game->GetPlayableRoundFrame(), 1);
	BattleFixture.Game->SetPaused(false);
	BattleFixture.Step();
	BattleFixture.Step();
	Renderer.DrawWidget(BattleFixture.ViewportOverlay.ToSharedRef(), FVector2D(768, 512));
	auto* Later =
	    Renderer.DrawWidget(BattleFixture.ViewportOverlay.ToSharedRef(), FVector2D(768, 512));
	FlushRenderingCommands();
	TestTrue(TEXT("visible duration advances with simulation"),
	         VisibleDuration(Widget->GetRenderedRuleText().ToString(), TEXT("Resource drain"), 1) || ModifierVisualReview::Verify(Later, TEXT("Resource drain"), 1));

	if (TestNotNull(TEXT("second actual HUD render target"), Later))
	{
		TUniquePtr<FArchive> LaterFile(IFileManager::Get().CreateFileWriter(
		    *(FPaths::ProjectSavedDir() / TEXT("Automation/ModifierHUD-frame3.png"))));
		if (TestTrue(TEXT("open advanced HUD artifact"), LaterFile.IsValid()))
			TestTrue(TEXT("save advanced actual HUD"),
			         FImageUtils::ExportRenderTarget2DAsPNG(Later, *LaterFile));
	}
	BattleFixture.Step();
	Renderer.DrawWidget(BattleFixture.ViewportOverlay.ToSharedRef(), FVector2D(768, 512));
	Renderer.DrawWidget(BattleFixture.ViewportOverlay.ToSharedRef(), FVector2D(768, 512));
	FlushRenderingCommands();
	TestFalse(TEXT("expired rule name disappears from actual widget"),
	          Widget->GetRenderedRuleText().ToString().Contains(TEXT("Resource drain")));
	TestTrue(TEXT("expired rule disappears from public display"),
	         BattleFixture.Game->GetActiveModifiers().IsEmpty());
	C.Schedule[0].Duration = -1;
	BattleFixture.Instance->BattleData.Modifiers = C;
	BattleFixture.Instance->AvailableModifiers = C.Definitions;
	BattleFixture.Instance->IsTraining = true;
	BattleFixture.Game->MatchInit();
	BattleFixture.Instance->IsTraining = false;
	BattleFixture.Step();
	BattleFixture.Game->SetPaused(true);
	const int32 RoundFrame = BattleFixture.Game->GetPlayableRoundFrame();
	Renderer.DrawWidget(BattleFixture.ViewportOverlay.ToSharedRef(), FVector2D(768, 512));
	auto* RoundEnd =
	    Renderer.DrawWidget(BattleFixture.ViewportOverlay.ToSharedRef(), FVector2D(768, 512));
	FlushRenderingCommands();
	TestTrue(TEXT("visible round-long rule describes round end"),
	         VisibleDuration(Widget->GetRenderedRuleText().ToString(), TEXT("Resource drain"), -1) || ModifierVisualReview::Verify(RoundEnd, TEXT("Resource drain"), -1));
	if (TestNotNull(TEXT("round-end label actual render target"), RoundEnd))
	{
		TUniquePtr<FArchive> RoundFile(IFileManager::Get().CreateFileWriter(
		    *(FPaths::ProjectSavedDir() / TEXT("Automation/ModifierHUD-round-end.png"))));
		if (TestTrue(TEXT("open round-end label artifact"), RoundFile.IsValid()))
			TestTrue(TEXT("save actual round-end label pixels"),
			         FImageUtils::ExportRenderTarget2DAsPNG(RoundEnd, *RoundFile));
	}
	BattleFixture.Step();
	TestEqual(TEXT("round-end label presentation does not advance paused simulation"),
	          BattleFixture.Game->GetPlayableRoundFrame(), RoundFrame);
	return true;
}

#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
namespace ModifierTests
{
FString ReplayLedger(const FPlayableBattle& BattleFixture)
{
	const auto* A = BattleFixture.Game->GetMainPlayer(true);
	const auto* B = BattleFixture.Game->GetMainPlayer(false);
	FString Out = FString::Printf(
	    TEXT("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u"),
	    BattleFixture.Game->BattleState.FrameNumber, BattleFixture.Game->BattleState.RoundCount,
	    BattleFixture.Game->GetPlayableRoundFrame(), A->CurrentHealth, B->CurrentHealth,
	    BattleFixture.Game->BattleState.Meter[0], BattleFixture.Game->BattleState.Meter[1], A->PosX,
	    B->PosX, A->PosY, B->PosY, A->Hitstop, B->Hitstop,
	    BattleFixture.Game->BattleState.P1RoundsWon,
	    BattleFixture.Game->BattleState.RandomManager.GetSeed());
	Out += FString::Printf(TEXT("|round:%d|velocity:%d:%d:%d:%d|action:%d:%d:%d:%d|facing:%d:%d"),
	                       BattleFixture.Game->BattleState.P2RoundsWon, A->SpeedX, A->SpeedY,
	                       B->SpeedX, B->SpeedY, A->ActionTime, B->ActionTime, A->ObjectReg1,
	                       B->ObjectReg1, int32(A->Direction), int32(B->Direction));
	TArray<FString> Rules, Effects;
	for (const auto& Rule : BattleFixture.Game->GetActiveModifiers())
		Rules.Add(FString::Printf(TEXT("|%s:%d:%d"), *Rule.Identifier, Rule.Revision,
		                          Rule.RemainingFrames));
	for (const auto* Object : BattleFixture.Game->Objects)
		if (Object && Object->IsActive)
			Effects.Add(FString::Printf(
			    TEXT("|object:%d:%s:%d:%d:%d:%d:%d:%d:%d"),
			    Object->Player ? Object->Player->PlayerIndex : -1,
			    *(Object->ObjectState ? Object->ObjectState->Name : FGameplayTag()).ToString(),
			    Object->PosX, Object->PosY, Object->SpeedX, Object->SpeedY, Object->ActionTime,
			    Object->Hitstop, int32(Object->Direction)));
	Rules.Sort();
	Effects.Sort();
	for (const auto& Item : Rules)
		Out += Item;
	for (const auto& Item : Effects)
		Out += Item;
	return Out;
}
} // namespace ModifierTests
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierColdReplay, "NightSkyIntegration.Modifiers.FreshProcessReplay",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierColdReplay::RunTest(const FString&)
{
	FString Slot, TracePath, ResultPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("NSE041ReplaySlot="), Slot))
	{
		FParse::Value(FCommandLine::Get(), TEXT("NSE041Trace="), TracePath);
		FParse::Value(FCommandLine::Get(), TEXT("NSE041Result="), ResultPath);
		auto* Saved = Cast<UReplaySaveInfo>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
		if (!TestNotNull(TEXT("cold process loads persisted replay"), Saved))
			return false;
		if (!TestTrue(TEXT("replay contains first fighter definition"),
		              Saved->BattleData.PlayerListP1.IsValidIndex(0)))
			return false;
		TestEqual(TEXT("persistent native character data reference"),
		          Saved->BattleData.PlayerListP1[0].LoadSynchronous(),
		          static_cast<UPrimaryCharaData*>(GetMutableDefault<UModifierFixtureCharaData>()));
		TArray<FString> Expected;
		if (!TestTrue(TEXT("expected ledger readable"),
		              FFileHelper::LoadFileToStringArray(Expected, *TracePath)) ||
		    Expected.IsEmpty())
			return false;
		FPlayableBattle BattleFixture(Saved->BattleData.Modifiers);
		BattleFixture.Instance->BattleData.Modifiers = {};
		BattleFixture.Instance->PlayReplayFromBP(Slot);
		if (!TestTrue(TEXT("replay content accepted in cold process"),
		              BattleFixture.Instance->IsReplay))
			return false;
		BattleFixture.Game->FighterRunner->Destroy();
		BattleFixture.Game->FighterRunner = nullptr;
		BattleFixture.Instance->IsTraining = true;
		BattleFixture.Game->MatchInit();
		BattleFixture.Instance->IsTraining = false;
		TestEqual(TEXT("cold initial ledger"), ReplayLedger(BattleFixture), Expected[0]);
		for (int32 Frame = 1; Frame < Expected.Num(); ++Frame)
		{
			BattleFixture.Game->FighterRunner->Update(OneFrame);
			TestEqual(FString::Printf(TEXT("cold replay frame %d"), Frame - 1),
			          ReplayLedger(BattleFixture), Expected[Frame]);
		}
		if (!HasAnyErrors())
			FFileHelper::SaveStringToFile(TEXT("passed"), *ResultPath);
		return true;
	}
	auto C = FModifierConfiguration::FourRulePreset();
	for (auto& Definition : C.Definitions)
	{
		if (Definition.Identifier == TEXT("Conversion"))
		{
			FModifierOperation Child;
			Child.Operation = EModifierOperation::ChildMeter;
			Child.Amount = 7;
			Child.ToAttacker = true;
			Definition.Operations.Add(Child);
		}
		if (Definition.Identifier == TEXT("Restriction"))
			Definition.OwnedAttack = State_ModifierFixture_ProjectileA;
	}
	for (auto& I : C.Schedule)
	{
		if (I.Identifier == TEXT("Conversion"))
			I.Duration = 12;
		if (I.Identifier == TEXT("Drain"))
			I.Duration = 24;
		if (I.Identifier == TEXT("Restriction"))
			I.Duration = 8;
		if (I.Identifier == TEXT("SuddenDeath"))
		{
			I.Start = 50;
			I.Duration = 12;
		}
	}
	FPlayableBattle BattleFixture(C);
	BattleFixture.Instance->RecordReplay();
	BattleFixture.Instance->IsReplay = false;
	TArray<FString> Trace{ReplayLedger(BattleFixture)};
	bool SawHealthDamage = false;
	for (int32 Frame = 0; Frame < 80; ++Frame)
	{
		BattleFixture.Step(Frame == 0 || Frame == 26 || Frame == 52 ? INP_A : 0, 0);
		Trace.Add(ReplayLedger(BattleFixture));
		SawHealthDamage |= BattleFixture.Game->GetMainPlayer(false)->CurrentHealth < 1000;
	}
	BattleFixture.Instance->IsReplay = true;
	TestTrue(TEXT("recorded match contains actual contact damage"), SawHealthDamage);
	if (!TestEqual(TEXT("cold replay requires the complete recorded 80-frame tape"),
	               BattleFixture.Instance->GetRecordedReplay()->LengthInFrames, 80))
		return false;
	Slot = TEXT("NSE041Cold_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (!TestTrue(TEXT("persist full input tape"),
	              BattleFixture.Instance->SaveRecordedReplay(Slot)))
		return false;
	TracePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") /
	                                              (Slot + TEXT(".trace")));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(TracePath), true);
	ResultPath = TracePath + TEXT(".result");
	TestTrue(TEXT("persist reference ledger"),
	         FFileHelper::SaveStringArrayToFile(Trace, *TracePath));
	// The child never sees the command line this editor was given, so the RHI
	// selection from spec.yaml would be lost on it. Forward only capability
	// flags: the child still picks -NullRHI for itself below.
	FString EditorArgs;
	{
		static const TCHAR* const Capability[] = {
		    TEXT("-d3d11"), TEXT("-d3d12"), TEXT("-vulkan"), TEXT("-opengl"),
		    TEXT("-sm5"),   TEXT("-sm6"),   TEXT("-AllowSoftwareRendering")};
		const FString Parent = FCommandLine::Get();
		for (const TCHAR* const Flag : Capability)
		{
			if (Parent.Contains(Flag, ESearchCase::IgnoreCase))
			{
				if (!EditorArgs.IsEmpty())
					EditorArgs += TEXT(" ");
				EditorArgs += Flag;
			}
		}
	}
	const FString Args = FString::Printf(
	    TEXT("\"%s\" -Unattended -NullRHI -NoSplash -NoSound -ExecCmds=\"Automation RunTests "
	         "NightSkyIntegration.Modifiers.FreshProcessReplay; Quit\" -NSE041ReplaySlot=%s "
	         "-NSE041Trace=\"%s\" -NSE041Result=\"%s\" -ABSLOG=\"%s.log\" %s"),
	    *FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()), *Slot, *TracePath,
	    *ResultPath, *TracePath, *EditorArgs);
	const FString Executable = FPlatformProcess::ExecutablePath();
	FProcHandle Child = FPlatformProcess::CreateProc(*Executable, *Args, false, true, true,
	                                                 nullptr, 0, nullptr, nullptr);
	if (!TestTrue(TEXT("launch independent replay process"), Child.IsValid()))
		return false;
	const double Deadline = FPlatformTime::Seconds() + 120;
	while (FPlatformProcess::IsProcRunning(Child) && FPlatformTime::Seconds() < Deadline)
		FPlatformProcess::Sleep(.05f);
	if (FPlatformProcess::IsProcRunning(Child))
	{
		FPlatformProcess::TerminateProc(Child, true);
		AddError(TEXT("cold replay process exceeded deadline"));
	}
	int32 ExitCode = -1;
	FPlatformProcess::GetProcReturnCode(Child, &ExitCode);
	FPlatformProcess::CloseProc(Child);
	TestEqual(TEXT("cold process exit"), ExitCode, 0);
	FString Result;
	TestTrue(TEXT("cold replay produced validated result"),
	         FFileHelper::LoadFileToString(Result, *ResultPath));
	TestEqual(TEXT("cold ledger matched"), Result, FString(TEXT("passed")));
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

#include "ModifierPythonAutomation.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierGGPO, "NightSkyIntegration.Modifiers.GGPOConfirmedReplay",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierGGPO::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FModifierPythonCommand(this, TEXT("modifier_ggpo_peers.py")));
	ADD_LATENT_AUTOMATION_COMMAND(
	    FModifierPythonCommand(this, TEXT("modifier_ggpo_peers.py"), TEXT("--lifecycle"), 600.));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierPeerMismatch, "NightSkyIntegration.Modifiers.PeerMismatchBlocksPlay",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierPeerMismatch::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(
	    FModifierPythonCommand(this, TEXT("modifier_ggpo_peers.py"), TEXT("--mismatch")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierStatefulFuzz,
                                 "NightSky.Modifiers.SeededBattleRollbackReplay",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierStatefulFuzz::RunTest(const FString&)
{
	for (int32 Seed = 51001; Seed <= 51012; ++Seed)
	{
		FRandomStream Random(Seed);
		auto C = FModifierConfiguration::FourRulePreset();
		for (auto& I : C.Schedule)
		{
			if (I.Identifier == TEXT("Drain"))
				I.Duration = Random.RandRange(25, 60);
			if (I.Identifier == TEXT("Conversion"))
				I.Duration = Random.RandRange(15, 35);
			if (I.Identifier == TEXT("Restriction"))
			{
				I.Start = Random.RandRange(2, 8);
				I.Duration = Random.RandRange(25, 40);
			}
			if (I.Identifier == TEXT("SuddenDeath"))
			{
				I.Start = 80;
				I.Duration = 20;
			}
		}
		for (auto& D : C.Definitions)
		{
			if (D.Identifier == TEXT("Drain"))
				D.Drain = Random.RandRange(1, 4);
			if (D.Identifier == TEXT("Restriction"))
				D.OwnedAttack = State_ModifierFixture_ProjectileA;
			if (D.Identifier == TEXT("Conversion"))
			{
				FModifierOperation O;
				O.Operation = EModifierOperation::ChildMeter;
				O.Amount = Random.RandRange(2, 9);
				O.ToAttacker = true;
				D.Operations.Add(O);
			}
		}
		auto Add = Rule(TEXT("A"), EModifierEvent::Damage, EModifierOperation::Add,
		                Random.RandRange(-5, 5));
		auto Scale = Rule(TEXT("a"), EModifierEvent::Damage, EModifierOperation::Multiply,
		                  Random.RandRange(1, 3), 2);
		C.Definitions.Add(Add);
		C.Definitions.Add(Scale);
		for (const FString& Id : {FString(TEXT("A")), FString(TEXT("a"))})
		{
			FModifierInterval I;
			I.Identifier = Id;
			I.Duration = 75;
			C.Schedule.Add(I);
		}
		auto Permuted = C;
		for (int32 I = 0; I < Permuted.Definitions.Num() / 2; ++I)
			Permuted.Definitions.Swap(I, Permuted.Definitions.Num() - 1 - I);
		for (int32 I = 0; I < Permuted.Schedule.Num() / 2; ++I)
			Permuted.Schedule.Swap(I, Permuted.Schedule.Num() - 1 - I);
		FPlayableBattle BattleFixture(C), P(Permuted);
		BattleFixture.Instance->RecordReplay();
		BattleFixture.Instance->IsReplay = false;
		TArray<FString> Trace{ReplayLedger(BattleFixture)};
		bool SawOwned = false, SawDamage = false;
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			if (Frame % 16 == 0)
			{
				const auto Before = ReplayLedger(BattleFixture);
				const int32 Length = BattleFixture.Instance->GetRecordedReplay()->LengthInFrames;
				BattleFixture.Game->SetPaused(true);
				P.Game->SetPaused(true);
				BattleFixture.Step(INP_A, INP_A);
				P.Step(INP_A, INP_A);
				TestEqual(FString::Printf(TEXT("seed %d pause excludes gameplay"), Seed),
				          ReplayLedger(BattleFixture), Before);
				TestEqual(TEXT("pause excludes replay input"),
				          BattleFixture.Instance->GetRecordedReplay()->LengthInFrames, Length);
				BattleFixture.Game->SetPaused(false);
				P.Game->SetPaused(false);
			}
			const int32 A = (Frame % 27 == 0 ? INP_A : 0) |
			                (Random.RandRange(0, 5) == 0 ? INP_Right : 0) |
			                (Frame == 10 ? INP_C : 0) | (Frame >= 12 && Frame <= 18 ? INP_D : 0);
			const int32 B = Frame % 29 == 2 ? INP_A : 0;
			BattleFixture.Step(A, B);
			P.Step(A, B);
			TestEqual(
			    FString::Printf(TEXT("seed %d frame %d registration permutation"), Seed, Frame),
			    ReplayLedger(BattleFixture), ReplayLedger(P));
			if (Frame == 0)
			{
				FRollbackData Checkpoint;
				int32 Checksum = 0;
				BattleFixture.Game->SaveGameState(Checkpoint, &Checksum);
				const auto Before = ReplayLedger(BattleFixture);
				for (int32 Extra = 0; Extra < 9; ++Extra)
					BattleFixture.Step(INP_A | INP_B, INP_Up);
				BattleFixture.Game->LoadGameState(Checkpoint);
				TestEqual(FString::Printf(TEXT("seed %d rollback before activation"), Seed),
				          ReplayLedger(BattleFixture), Before);
			}
			Trace.Add(ReplayLedger(BattleFixture));
			SawDamage |= BattleFixture.Game->GetMainPlayer(true)->CurrentHealth < 1000 ||
			             BattleFixture.Game->GetMainPlayer(false)->CurrentHealth < 1000;
			for (const auto* O : BattleFixture.Game->Objects)
				SawOwned |= O->IsActive && O->ObjectState &&
				            O->ObjectState->Name == State_ModifierFixture_ProjectileA;
		}
		TestTrue(FString::Printf(TEXT("seed %d live owned attacks"), Seed), SawOwned);
		TestTrue(FString::Printf(TEXT("seed %d actual contact health loss"), Seed), SawDamage);
		if (!TestEqual(FString::Printf(TEXT("seed %d recorded complete 120-frame tape"), Seed),
		               BattleFixture.Instance->GetRecordedReplay()->LengthInFrames, 120))
			return false;
		BattleFixture.Instance->IsReplay = true;
		const FString Slot = FString::Printf(TEXT("NSE041Seed%d_"), Seed) +
		                     FGuid::NewGuid().ToString(EGuidFormats::Digits);
		if (!TestTrue(TEXT("save seeded input replay"),
		              BattleFixture.Instance->SaveRecordedReplay(Slot)))
			return false;
		BattleFixture.Instance->BattleData.Modifiers = {};
		BattleFixture.Instance->PlayReplayFromBP(Slot);
		BattleFixture.Game->FighterRunner->Destroy();
		BattleFixture.Game->FighterRunner = nullptr;
		BattleFixture.Instance->IsTraining = true;
		BattleFixture.Game->MatchInit();
		BattleFixture.Instance->IsTraining = false;
		TestEqual(FString::Printf(TEXT("seed %d rematch resets initial modifier state"), Seed),
		          ReplayLedger(BattleFixture), Trace[0]);
		for (int32 Frame = 1; Frame < Trace.Num(); ++Frame)
		{
			BattleFixture.Game->FighterRunner->Update(OneFrame);
			TestEqual(FString::Printf(TEXT("seed %d replay frame %d"), Seed, Frame - 1),
			          ReplayLedger(BattleFixture), Trace[Frame]);
		}
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Automation") /
		                     FString::Printf(TEXT("modifier-seed-%d.trace"), Seed);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		TestTrue(TEXT("save deterministic seed trace artifact"),
		         FFileHelper::SaveStringArrayToFile(Trace, *Path));
		UGameplayStatics::DeleteGameInSlot(Slot, 0);
	}
	return true;
}

#include "NightSkyEngine/UI/ModifierSetupWidget.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierSetup, "NightSkyIntegration.Modifiers.SetupSelectionStartsBattle",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierSetup::RunTest(const FString&)
{
	auto C = FModifierConfiguration::FourRulePreset();
	FPlayableBattle BattleFixture(C);
	BattleFixture.Game->SetPaused(true);
	auto* Setup = CreateWidget<UModifierSetupWidget>(BattleFixture.Instance,
	                                                 UModifierSetupWidget::StaticClass());
	if (!TestNotNull(TEXT("native player setup"), Setup))
		return false;
	Setup->ConfigureForMatch(C);
	Setup->AddToViewport(100);
	Setup->TakeWidget();
	TestTrue(TEXT("disable restriction through player control"),
	         Setup->SetRuleEnabled(TEXT("Restriction"), false));
	TestTrue(TEXT("disable sudden death through player control"),
	         Setup->SetRuleEnabled(TEXT("SuddenDeath"), false));
	const int32 Conversion = C.Schedule.IndexOfByPredicate(
	    [](const auto& I) { return I.Identifier == TEXT("Conversion"); });
	TestTrue(TEXT("edit interval through setup control"), Setup->SetInterval(Conversion, 1, 0, 0));
	TestFalse(TEXT("invalid player setup cannot start match"), Setup->StartSelectedMatch());
	TestFalse(TEXT("invalid setup displays reason"), Setup->GetSetupError().IsEmpty());
	const int32 Before = BattleFixture.Game->BattleState.FrameNumber;
	BattleFixture.Step(INP_A, 0);
	TestEqual(TEXT("invalid setup keeps battle paused"),
	          BattleFixture.Game->BattleState.FrameNumber, Before);
	TestTrue(TEXT("correct interval through setup control"),
	         Setup->SetInterval(Conversion, 1, 0, 1));
	const FString Summary = Setup->GetSetupSummary().ToString();
	TestTrue(TEXT("actual setup text reflects edited interval"),
	         Summary.Contains(TEXT("Conversion")));
	FWidgetRenderer Renderer(true);
	auto* Target =
	    Renderer.DrawWidget(BattleFixture.ViewportOverlay.ToSharedRef(), FVector2D(1024, 768));
	FlushRenderingCommands();
	if (TestNotNull(TEXT("render actual player setup"), Target))
	{
		TArray<FColor> Pixels;
		TestTrue(TEXT("read rendered setup"),
		         Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
		int32 Ink = 0;
		for (const auto& P : Pixels)
			if (P.R > 20 || P.G > 20 || P.B > 20)
				++Ink;
		TestTrue(TEXT("setup controls have visible pixels"), Ink > 100);
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Automation/ModifierSetup.png");
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*Path));
		if (TestTrue(TEXT("open setup screenshot"), File.IsValid()))
			TestTrue(TEXT("save actual setup screenshot"),
			         FImageUtils::ExportRenderTarget2DAsPNG(Target, *File));
	}
	TestTrue(TEXT("start button submits valid selection to real match"),
	         Setup->StartSelectedMatch());
	TestTrue(TEXT("accepted selection clears error"), Setup->GetSetupError().IsEmpty());
	BattleFixture.Game->GetMainPlayer(true)->AddMeter(20);
	BattleFixture.Step();
	TestEqual(TEXT("selected drain changes actual fighter meter"),
	          BattleFixture.Game->BattleState.Meter[0], 15);
	TestTrue(TEXT("selected conversion appears in live match"),
	         BattleFixture.Game->GetActiveModifiers().ContainsByPredicate(
	             [](const auto& R) { return R.Identifier == TEXT("Conversion"); }));
	BattleFixture.Step();
	TestFalse(TEXT("edited duration expires in real simulation"),
	          BattleFixture.Game->GetActiveModifiers().ContainsByPredicate(
	              [](const auto& R) { return R.Identifier == TEXT("Conversion"); }));
	TestFalse(TEXT("disabled restriction never activates"),
	          BattleFixture.Game->GetActiveModifiers().ContainsByPredicate(
	              [](const auto& R) { return R.Identifier == TEXT("Restriction"); }));
	if (TestNotNull(TEXT("setup starts ordinary replay recording"),
	                BattleFixture.Instance->GetRecordedReplay()))
		TestEqual(TEXT("selected match records both actual input steps"),
		          BattleFixture.Instance->GetRecordedReplay()->LengthInFrames, 2);
	return true;
}

#include "ModifierOracle.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierDifferential, "NightSky.Modifiers.SeededEventInterpreter",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierDifferential::RunTest(const FString&)
{
	using namespace ModifierOracle;
	int32 ChangedEpisodes = 0, Contacts = 0;
	TArray<int32> Seeds;
	for (int32 S = 41001; S <= 41032; ++S)
		Seeds.Add(S);
	for (int32 S = 62001; S <= 62008; ++S)
		Seeds.Add(S);
	FBattle BattleFixture;
	for (int32 Seed : Seeds)
	{
		FRandomStream Random(Seed);
		for (int32 Episode = 0; Episode < 4; ++Episode)
		{
			FState Oracle;
			FModifierConfiguration C;
			for (int32 N = 0; N < 4; ++N)
			{
				FRule R;
				R.Id = FString::Printf(TEXT("%c"), N == 0 ? 'a' : 'A' + N);
				R.Priority = Random.RandRange(-1, 1);
				R.Kind = N < 2 ? EKind::Damage : EKind::Meter;
				R.Start = Random.RandRange(0, 1);
				R.End = Random.RandRange(3, 6);
				R.Convert = N == 0 && Random.RandRange(0, 1);
				R.Drain = N == 0 ? Random.RandRange(0, 2) : 0;
				FModifierDefinition D;
				D.Identifier = R.Id;
				D.DisplayName = R.Id;
				D.Priority = R.Priority;
				D.Subscription = N < 2 ? EModifierEvent::Damage : EModifierEvent::Meter;
				D.ConvertDamage = R.Convert;
				D.Drain = R.Drain;
				for (int32 K = 0; K < 2; ++K)
				{
					const int32 Choice = Random.RandRange(0, 7);
					FAction A;
					FModifierOperation O;
					A.Attacker = Random.RandRange(0, 1);
					O.ToAttacker = A.Attacker;
					if (Choice < 2)
					{
						A.Kind = EAction::Add;
						O.Operation = EModifierOperation::Add;
						A.Value = Random.RandRange(-3, 3);
					}
					else if (Choice < 4)
					{
						A.Kind = EAction::Scale;
						O.Operation = EModifierOperation::Multiply;
						A.Value = Random.RandRange(0, 3);
						A.Denominator = Random.RandRange(1, 3);
					}
					else if (Choice == 4)
					{
						A.Kind = EAction::Cancel;
						O.Operation = EModifierOperation::Cancel;
					}
					else if (Choice == 5)
					{
						A.Kind = EAction::DamageChild;
						O.Operation = EModifierOperation::ChildDamage;
						A.Value = Random.RandRange(0, 4);
					}
					else
					{
						A.Kind = EAction::MeterChild;
						O.Operation = EModifierOperation::ChildMeter;
						A.Value = Random.RandRange(-4, 4);
					}
					O.Amount = A.Value;
					O.Denominator = A.Denominator;
					R.Actions.Add(A);
					D.Operations.Add(O);
				}
				Oracle.Rules.Add(R);
				C.Definitions.Insert(D, 0);
				FModifierInterval I;
				I.Identifier = R.Id;
				I.Start = R.Start;
				I.Duration = R.End - R.Start;
				C.Schedule.Insert(I, 0);
			}
			const int32 Initial = Episode % 3 == 0 ? 0 : Episode % 3 == 1 ? 100 : 50;
			BattleFixture.Instance->BattleData.Random = FRandomManager(41001);
			BattleFixture.Game->BattleState.Meter[0] = Initial;
			BattleFixture.Game->BattleState.Meter[1] = Initial;
			Oracle.Meter[0] = Oracle.Meter[1] = Initial;
			const FString Prefix = FString::Printf(TEXT("seed %d episode %d"), Seed, Episode);
			if (!TestTrue(Prefix + TEXT(" accepted generated authoring"), BattleFixture.Start(C)))
				return false;
			for (int32 Frame = 0; Frame < 7; ++Frame)
			{
				if (Frame)
					BattleFixture.Step();
				if (Frame == 3)
					Oracle.Disabled.Add(TEXT("a"));
				Oracle.Step();
				if (Frame == 2)
					BattleFixture.Game->DeactivateModifier(TEXT("a"));
				const int32 Damage = Frame % 2 ? 10 : 21;
				BattleFixture.Hit(Damage);
				Oracle.Event(EKind::Damage, 1, 0, Damage);
				++Contacts;
				const int32 MeterDelta = Random.RandRange(-8, 8);
				BattleFixture.P[0]->AddMeter(MeterDelta);
				Oracle.Event(EKind::Meter, 0, 1, MeterDelta);
				for (int32 Team = 0; Team < 2; ++Team)
				{
					const FString At =
					    Prefix + FString::Printf(TEXT(" frame %d team %d"), Frame, Team);
					TestEqual(At + TEXT(" health against independent interpreter"),
					          BattleFixture.P[Team]->CurrentHealth, Oracle.Health[Team]);
					TestEqual(At + TEXT(" meter against independent interpreter"),
					          BattleFixture.Game->BattleState.Meter[Team], Oracle.Meter[Team]);
				}
			}
			if (Oracle.Meter[0] != Initial || Oracle.Meter[1] != Initial)
				++ChangedEpisodes;
			if (HasAnyErrors())
				return false;
		}
	}
	TestEqual(TEXT("all generated real contacts executed"), Contacts, 40 * 4 * 7);
	TestTrue(TEXT("positive modifier resource outcomes"), ChangedEpisodes > 32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierMovement,
                                 "NightSky.Modifiers.MovementCommandsAndMomentum",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierMovement::RunTest(const FString&)
{
	FModifierDefinition D;
	D.Identifier = TEXT("Restriction");
	D.RestrictMovement = true;
	{
		auto C = Config({D});
		C.Schedule[0].Duration = 3;
		FPlayableBattle BattleFixture(C);
		auto* P = BattleFixture.Game->GetMainPlayer(true);
		const int32 StartX = P->PosX, StartY = P->PosY;
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			BattleFixture.Step(INP_Right | INP_Up, 0);
			TestEqual(TEXT("restriction suppresses new walk from rest"), P->PosX, StartX);
			TestEqual(TEXT("restriction suppresses new jump from ground"), P->PosY, StartY);
		}
		BattleFixture.Step(INP_Right, 0);
		BattleFixture.Step(INP_Right, 0);
		TestEqual(TEXT("expired restriction permits disclosed 1000-unit walk step"), P->PosX,
		          StartX + 1000);
		BattleFixture.Step(INP_Up, 0);
		BattleFixture.Step();
		TestTrue(TEXT("expired restriction permits actual jump"), P->PosY > StartY);
	}
	{
		auto C = Config({D});
		C.Schedule[0].Duration = 2;
		FPlayableBattle BattleFixture(C);
		auto* P = BattleFixture.Game->GetMainPlayer(true);
		const int32 StartX = P->PosX;
		BattleFixture.Step(INP_Right | INP_E, 0);
		BattleFixture.Step(INP_Right | INP_E, 0);
		TestEqual(TEXT("restriction suppresses new dash from rest"), P->PosX, StartX);
		BattleFixture.Step(INP_Right | INP_E, 0);
		BattleFixture.Step(INP_Right | INP_E, 0);
		TestEqual(TEXT("expired restriction permits disclosed 3000-unit dash step"), P->PosX,
		          StartX + 3000);
	}
	{
		auto C = Config({D});
		C.Schedule[0].Start = 2;
		C.Schedule[0].Duration = 3;
		FPlayableBattle BattleFixture(C);
		auto* P = BattleFixture.Game->GetMainPlayer(true);
		const int32 StartX = P->PosX, StartY = P->PosY;
		BattleFixture.Step(INP_Right, 0);
		BattleFixture.Step(INP_Right, 0);
		TestEqual(TEXT("ordinary walking momentum positive control"), P->PosX, StartX + 1000);
		for (int32 Frame = 2; Frame < 5; ++Frame)
		{
			BattleFixture.Step(INP_Left | INP_Up, 0);
			TestEqual(TEXT("restriction preserves existing 1000-unit momentum"), P->PosX,
			          StartX + Frame * 1000);
			TestEqual(TEXT("new jump stays blocked while momentum continues"), P->PosY, StartY);
		}
	}
	{
		auto C = Config({D});
		C.Schedule[0].Start = 2;
		C.Schedule[0].Duration = 8;
		FPlayableBattle Restricted(C), Ordinary({});
		for (int32 Frame = 0; Frame < 9; ++Frame)
		{
			Restricted.Step(Frame == 0 ? INP_Up : 0, 0);
			Ordinary.Step(Frame == 0 ? INP_Up : 0, 0);
			const int32 T = Frame;
			const int32 ExpectedY = 10000 * T - 1000 * T * (T - 1) / 2;
			TestEqual(TEXT("ordinary disclosed jump follows integer gravity"),
			          Ordinary.Game->GetMainPlayer(true)->PosY, ExpectedY);
			TestEqual(TEXT("restriction preserves already-started jump trajectory"),
			          Restricted.Game->GetMainPlayer(true)->PosY, ExpectedY);
		}
	}
	{
		auto C = Config({D});
		C.Schedule[0].Start = 5;
		C.Schedule[0].Duration = 4;
		FPlayableBattle Restricted(C), Ordinary({});
		const int32 StartX = Ordinary.Game->GetMainPlayer(false)->PosX;
		bool SawStun = false, SawPushback = false, SawMovementDuringRestriction = false;
		for (int32 Frame = 0; Frame < 22; ++Frame)
		{
			const int32 Command = Frame == 0 ? INP_A | INP_F : 0;
			Restricted.Step(Command, 0);
			Ordinary.Step(Command, 0);
			auto* R = Restricted.Game->GetMainPlayer(false);
			auto* O = Ordinary.Game->GetMainPlayer(false);
			SawStun |= O->StunTime == 18;
			SawPushback |= O->Pushback == -10000;
			SawMovementDuringRestriction |= Frame >= 5 && Frame < 9 && O->PosX > StartX;
			TestEqual(TEXT("restriction preserves ordinary contact health"), R->CurrentHealth,
			          O->CurrentHealth);
			TestEqual(TEXT("restriction preserves exact ordinary knockback position"), R->PosX,
			          O->PosX);
			TestEqual(TEXT("restriction preserves ordinary hitstun clock"), R->StunTime,
			          O->StunTime);
			TestEqual(TEXT("restriction preserves ordinary decaying pushback"), R->Pushback,
			          O->Pushback);
		}
		TestEqual(TEXT("knockback attack deals disclosed 21 damage"),
		          Ordinary.Game->GetMainPlayer(false)->CurrentHealth, 979);
		TestTrue(TEXT("ordinary hit creates disclosed 18-frame stun"), SawStun);
		TestTrue(TEXT("ordinary hit creates disclosed initial pushback"), SawPushback);
		TestTrue(TEXT("ordinary knockback moves during the active restriction interval"),
		         SawMovementDuringRestriction);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierDrawLifecycle,
                                 "NightSky.Modifiers.SimultaneousVictoryRoundRematch",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierDrawLifecycle::RunTest(const FString&)
{
	for (bool ExpiresOnContact : {false, true})
	{
		FModifierDefinition Shot;
		Shot.Identifier = TEXT("Shots");
		Shot.OwnedAttack = State_ModifierFixture_ProjectileA;
		FModifierDefinition Sudden;
		Sudden.Identifier = TEXT("Sudden");
		Sudden.SuddenDeath = true;
		FModifierDefinition Later;
		Later.Identifier = TEXT("Later");
		Later.Drain = 7;
		auto C = Config({Shot, Sudden, Later});
		C.Schedule[0].Duration = 25;
		C.Schedule[1].Start = ExpiresOnContact ? 0 : 8;
		C.Schedule[1].Duration = ExpiresOnContact ? 8 : 1;
		C.Schedule[2].Round = 2;
		C.Schedule[2].Duration = 2;
		FPlayableBattle BattleFixture(C);
		for (int32 Frame = 0; Frame < 9; ++Frame)
			BattleFixture.Step();
		TestEqual(TEXT("P1 receives simultaneous ordinary projectile damage"),
		          BattleFixture.Game->GetMainPlayer(true)->CurrentHealth, 990);
		TestEqual(TEXT("P2 receives simultaneous ordinary projectile damage"),
		          BattleFixture.Game->GetMainPlayer(false)->CurrentHealth, 990);
		if (ExpiresOnContact)
		{
			TestEqual(TEXT("expiry on contact frame cannot award sudden-death round"),
			          BattleFixture.Game->GetCurrentRoundResult(), int32(WIN_None));
			TestEqual(TEXT("ordinary nonlethal match remains in battle"),
			          BattleFixture.Game->BattleState.BattlePhase, EBattlePhase::Battle);
			continue;
		}
		TestEqual(TEXT("both positive damage sides draw instead of first-processed win"),
		          BattleFixture.Game->GetCurrentRoundResult(), int32(WIN_Draw));
		TestTrue(TEXT("round end removes active rule displays"),
		         BattleFixture.Game->GetActiveModifiers().IsEmpty());
		for (const auto* O : BattleFixture.Game->Objects)
			TestFalse(TEXT("round end removes owned future attacks"), O->IsActive);
		for (int32 I = 0; I < 360 && BattleFixture.Game->BattleState.RoundCount < 2; ++I)
			BattleFixture.Step();
		TestEqual(TEXT("real result/fade lifecycle enters next numbered round"),
		          BattleFixture.Game->BattleState.RoundCount, 2);
		BattleFixture.Game->GetMainPlayer(true)->AddMeter(20);
		BattleFixture.Game->GetMainPlayer(false)->AddMeter(20);
		BattleFixture.Step();
		TestEqual(TEXT("round-two-only drain activates on its frame zero"),
		          BattleFixture.Game->BattleState.Meter[0], 13);
		TestEqual(TEXT("new round starts with restored ordinary health"),
		          BattleFixture.Game->GetMainPlayer(true)->CurrentHealth, 1000);
		BattleFixture.Step();
		BattleFixture.Step();
		TestEqual(TEXT("two-frame round-two drain has exactly two commits"),
		          BattleFixture.Game->BattleState.Meter[0], 6);
		TestTrue(TEXT("round-two expiry removes last rule"),
		         BattleFixture.Game->GetActiveModifiers().IsEmpty());
		BattleFixture.Instance->IsTraining = true;
		BattleFixture.Step(INP_Rematch, INP_Rematch);
		BattleFixture.Instance->IsTraining = false;
		TestEqual(TEXT("rematch returns to first round"),
		          BattleFixture.Game->BattleState.RoundCount, 1);
		FRollbackData RematchCheckpoint;
		int32 RematchChecksum = 0;
		BattleFixture.Game->SaveGameState(RematchCheckpoint, &RematchChecksum);
		for (int32 Held = 0; Held < 3; ++Held)
			BattleFixture.Step(INP_Rematch, INP_Rematch);
		const int32 HeldFrame = BattleFixture.Game->GetPlayableRoundFrame();
		TestEqual(TEXT("held rematch pair starts only once and advances three playable frames"),
		          HeldFrame, 3);
		BattleFixture.Game->LoadGameState(RematchCheckpoint);
		for (int32 Held = 0; Held < 3; ++Held)
			BattleFixture.Step(INP_Rematch, INP_Rematch);
		TestEqual(TEXT("rollback preserves rematch edge state for held corrected inputs"),
		          BattleFixture.Game->GetPlayableRoundFrame(), HeldFrame);
		TestEqual(TEXT("rematch clears prior draw"), BattleFixture.Game->GetCurrentRoundResult(),
		          int32(WIN_None));
		BattleFixture.Step();
		int32 Owned = 0;
		for (const auto* O : BattleFixture.Game->Objects)
			if (O->IsActive)
				++Owned;
		TestEqual(TEXT("rematch creates exactly the new two owned attacks"), Owned, 2);
		TestEqual(TEXT("ordinary training refill remains 100 without future-round drain leaking "
		               "into rematch"),
		          BattleFixture.Game->BattleState.Meter[0], 100);
	}
	for (bool Timeout : {false, true})
	{
		auto C = FModifierConfiguration::FourRulePreset();
		for (auto& Interval : C.Schedule)
		{
			Interval.Start = 0;
			Interval.Duration = 1;
		}
		FPlayableBattle BattleFixture(C);
		BattleFixture.Instance->BattleData.RoundCount = 1;
		BattleFixture.Instance->BattleData.StartRoundTimer = Timeout ? 1 : 999;
		BattleFixture.Instance->IsTraining = true;
		BattleFixture.Game->MatchInit();
		BattleFixture.Instance->IsTraining = false;
		int32 PreviousHealth = 1000, PositiveContacts = 0;
		for (int32 Frame = 0;
		     Frame < (Timeout ? 100 : 1800) && BattleFixture.Game->BattleState.P1RoundsWon == 0;
		     ++Frame)
		{
			BattleFixture.Step((Timeout ? Frame == 0 : Frame % 30 == 0) ? INP_A : 0, 0);
			const int32 Health = BattleFixture.Game->GetMainPlayer(false)->CurrentHealth;
			if (Health < PreviousHealth)
			{
				++PositiveContacts;
				TestEqual(TEXT("ordinary post-expiry attack has exact clamped 21 damage"), Health,
				          FMath::Max(0, PreviousHealth - 21));
				PreviousHealth = Health;
			}
		}
		TestTrue(TEXT("ordinary contacts actually occurred after one-frame rules expired"),
		         PositiveContacts > 0);
		TestTrue(TEXT("all four expired rule displays remain absent"),
		         BattleFixture.Game->GetActiveModifiers().IsEmpty());
		TestEqual(TEXT("ordinary post-expiry result awards the actual winner"),
		          BattleFixture.Game->GetCurrentRoundResult(), int32(WIN_P1));
		TestEqual(TEXT("ordinary KO or health-lead timeout awards P1"),
		          BattleFixture.Game->BattleState.P1RoundsWon, 1);
		TestEqual(TEXT("ordinary loser has exact health at result"),
		          BattleFixture.Game->GetMainPlayer(false)->CurrentHealth, Timeout ? 979 : 0);
		TestEqual(TEXT("ordinary KO requires exactly 48 positive 21-damage contacts"),
		          PositiveContacts, Timeout ? 1 : 48);
	}
	{
		auto C = FModifierConfiguration::FourRulePreset();
		for (auto& Interval : C.Schedule)
		{
			Interval.Start = 0;
			Interval.Duration = 1;
		}
		FPlayableBattle BattleFixture(C);
		BattleFixture.Instance->BattleData.StartRoundTimer = 1;
		BattleFixture.Instance->IsTraining = true;
		BattleFixture.Game->MatchInit();
		BattleFixture.Instance->IsTraining = false;
		int32 Steps = 0;
		while (Steps < 100 && BattleFixture.Game->BattleState.P1RoundsWon == 0)
		{
			BattleFixture.Step();
			++Steps;
		}
		TestEqual(TEXT("ordinary one-second timer expires after exactly 60 simulation steps"),
		          Steps, 60);
		TestEqual(TEXT("no-contact timeout preserves P1 health"),
		          BattleFixture.Game->GetMainPlayer(true)->CurrentHealth, 1000);
		TestEqual(TEXT("no-contact timeout preserves P2 health"),
		          BattleFixture.Game->GetMainPlayer(false)->CurrentHealth, 1000);
		TestEqual(TEXT("ordinary equal-health timeout awards first draw side"),
		          BattleFixture.Game->BattleState.P1RoundsWon, 1);
		TestEqual(TEXT("ordinary equal-health timeout awards second draw side"),
		          BattleFixture.Game->BattleState.P2RoundsWon, 1);
		TestEqual(TEXT("no-contact timeout remains an ordinary draw"),
		          BattleFixture.Game->GetCurrentRoundResult(), int32(WIN_Draw));
	}
	return true;
}

// Fixed expected values deliberately avoid evaluating wide intermediate arithmetic
// in the test process. Every authored field and final observation is representable.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierNumericDomain, "NightSky.Modifiers.NumericDomain",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierNumericDomain::RunTest(const FString&)
{
	for (EModifierEvent Kind : {EModifierEvent::Damage, EModifierEvent::Meter})
	{
		for (int32 Ending = 0; Ending < 3; ++Ending)
		{
			auto Wide = Rule(TEXT("Wide"), Kind, EModifierOperation::Multiply, MAX_int32);
			// 21 * (2^31-1)^3 exceeds both signed and unsigned 64-bit ranges.
			const FModifierOperation Grow = Wide.Operations[0];
			Wide.Operations.Add(Grow);
			Wide.Operations.Add(Grow);
			FModifierOperation Op;
			Op.Operation = EModifierOperation::Multiply;
			Op.Amount = 1;
			Op.Denominator = MAX_int32;
			for (int32 I = 0; I < 3; ++I)
				Wide.Operations.Add(Op);
			if (Ending)
			{
				Op.Operation = Ending == 1 ? EModifierOperation::Multiply : EModifierOperation::Cancel;
				Op.Amount = 0;
				Op.Denominator = 1;
				// Zero or cancellation may discard a value larger than any fixed word.
				Wide.Operations.SetNum(3);
				Wide.Operations.Add(Op);
			}
			FBattle B;
			if (!TestTrue(TEXT("valid wide arithmetic authoring accepted"), B.Start(Config({Wide}))))
				return false;
			if (Kind == EModifierEvent::Damage)
				TestEqual(TEXT("wide products divide exactly back before damage commit"), B.Hit(21), Ending ? 0 : 21);
			else
			{
				B.Game->BattleState.Meter[0] = 50;
				B.P[0]->AddMeter(-21);
				TestEqual(TEXT("signed wide meter arithmetic preserves exact result"), B.Game->BattleState.Meter[0], Ending ? 50 : 29);
			}
		}
	}
	{
		auto R = Rule(TEXT("AddOverflow"), EModifierEvent::Damage, EModifierOperation::Add, MAX_int32);
		FModifierOperation Back = R.Operations[0];
		Back.Amount = -MAX_int32;
		R.Operations.Add(Back);
		FBattle B;
		TestTrue(TEXT("signed add extremes accepted"), B.Start(Config({R})));
		TestEqual(TEXT("intermediate add cannot wrap or saturate"), B.Hit(21), 21);
	}
	{
		auto R = Rule(TEXT("Floor"), EModifierEvent::Meter, EModifierOperation::Multiply, 1, 2);
		FBattle B;
		TestTrue(TEXT("signed floor authoring accepted"), B.Start(Config({R})));
		B.Game->BattleState.Meter[0] = 50;
		B.P[0]->AddMeter(-3);
		TestEqual(TEXT("negative rational rounds down, not toward zero"), B.Game->BattleState.Meter[0], 48);
	}
	{
		auto R = Rule(TEXT("Clamp"), EModifierEvent::Damage, EModifierOperation::Add, -30);
		auto Recover = R.Operations[0];
		Recover.Amount = 7;
		R.Operations.Add(Recover);
		FBattle B;
		TestTrue(TEXT("clamp recovery authoring accepted"), B.Start(Config({R})));
		TestEqual(TEXT("zero clamp occurs between additions"), B.Hit(21), 7);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierAuthoringDomain, "NightSky.Modifiers.AuthoringDomain",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierAuthoringDomain::RunTest(const FString&)
{
	const auto Base = Config({Rule(TEXT("Boundary"), EModifierEvent::Damage, EModifierOperation::Multiply, 1)});
	auto Reject = [&](FModifierConfiguration C, const TCHAR* Label, const TCHAR* Id)
	{
		FString Reason;
		TestFalse(Label, C.Validate(Reason));
		TestTrue(TEXT("rejection names offending rule"), Reason.Contains(Id, ESearchCase::CaseSensitive));
		TestTrue(TEXT("rejection explains more than the rule name"), Reason.Len() > FCString::Strlen(Id));
	};
	for (int32 Kind = 0; Kind < 7; ++Kind)
	{
		auto C = Base;
		switch (Kind)
		{
		case 0: C.Definitions[0].Operations[0].Amount = -1; break;
		case 1: C.Definitions[0].Operations[0].Denominator = -1; break;
		case 2: C.Definitions[0].Drain = -1; break;
		case 3: C.Schedule[0].Round = 0; break;
		case 4: C.Schedule[0].Duration = -2; break;
		case 5: C.Definitions.Reset(); break;
		case 6: { const FModifierInterval Copy = C.Schedule[0]; C.Schedule.Add(Copy); C.Schedule[1].Start = 1; break; }
		}
		Reject(C, TEXT("invalid parameters or same-rule overlap rejected"), TEXT("Boundary"));
	}
	for (int32 Kind = 0; Kind < 4; ++Kind)
	{
		auto C = Base;
		switch (Kind)
		{
		case 0: C.Schedule[0].Start = MAX_int32; C.Schedule[0].Duration = MAX_int32; break;
		case 1: C.Definitions[0].Priority = MIN_int32; C.Definitions[0].Operations[0].Amount = MAX_int32; break;
		case 2: C.Definitions[0].Operations[0].Amount = 0; C.Definitions[0].Drain = MAX_int32; break;
		case 3: C.Schedule[0].Round = MAX_int32; C.Schedule[0].Duration = 1; break;
		}
		FString Reason;
		TestTrue(TEXT("valid full-field authoring domain accepted"), C.Validate(Reason));
	}
	{
		auto C = Base;
		C.Schedule[0].Duration = 2;
		const FModifierInterval Copy = C.Schedule[0];
		C.Schedule.Add(Copy);
		C.Schedule[1].Start = 2;
		FString Reason;
		TestTrue(TEXT("adjacent same-rule intervals accepted"), C.Validate(Reason));
		C.Schedule[1].Round = 2;
		C.Schedule[1].Start = 0;
		TestTrue(TEXT("same identifier across rounds accepted"), C.Validate(Reason));
	}
	{
		TArray<FModifierDefinition> Rules;
		for (int32 I = 0; I < 70; ++I)
			Rules.Add(Rule(*FString::Printf(TEXT("Rule%03d"), I), EModifierEvent::Damage, EModifierOperation::Add, 1));
		FBattle B;
		TestTrue(TEXT("independent authoring is not restricted to a word-sized rule mask"), B.Start(Config(Rules)));
		TestEqual(TEXT("all seventy selected rules contribute"), B.Hit(21), 91);
	}
	{
		auto Upper = Rule(TEXT("A"), EModifierEvent::Damage, EModifierOperation::Add, 3);
		auto Lower = Rule(TEXT("a"), EModifierEvent::Damage, EModifierOperation::Multiply, 2);
		FBattle B;
		TestTrue(TEXT("case-sensitive identifiers are distinct"), B.Start(Config({Lower, Upper})));
		TestEqual(TEXT("ASCII uppercase sorts before lowercase"), B.Hit(21), 48);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierParentCommitOrder, "NightSky.Modifiers.ParentCommitOrderAndOriginalAmount",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierParentCommitOrder::RunTest(const FString&)
{
	for (bool Convert : {false, true})
	{
		auto Parent = Rule(TEXT("Parent"), EModifierEvent::Damage, EModifierOperation::ChildMeter, Convert ? 7 : -20);
		Parent.ConvertDamage = Convert;
		FBattle B;
		TestTrue(TEXT("parent ordering setup accepted"), B.Start(Config({Parent})));
		B.P[0]->MeterPercentOnHit = 100;
		B.P[1]->MeterPercentOnReceiveHit = 100;
		B.Game->BattleState.Meter[0] = 90;
		B.Game->BattleState.Meter[1] = Convert ? 10 : 90;
		B.Hit(21);
		TestEqual(TEXT("ordinary gain or conversion debit commits before child"), B.Game->BattleState.Meter[1], Convert ? 7 : 80);
		TestEqual(TEXT("conversion suppresses attacker ordinary gain"), B.Game->BattleState.Meter[0], Convert ? 90 : 100);
		TestEqual(TEXT("conversion does not cancel authored children or spill into health"), B.P[1]->CurrentHealth, Convert ? 1000 : 979);
	}
	{
		auto Transform = Rule(TEXT("A"), EModifierEvent::Damage, EModifierOperation::Add, 9);
		auto Filter = Rule(TEXT("B"), EModifierEvent::Damage, EModifierOperation::ChildMeter, 7);
		Filter.MatchAmount = true;
		Filter.RequiredAmount = 21;
		FBattle B;
		TestTrue(TEXT("original-amount selector accepted"), B.Start(Config({Filter, Transform})));
		B.Hit(21);
		TestEqual(TEXT("selector uses incoming amount before preceding handlers"), B.Game->BattleState.Meter[1], 7);
		B.Hit(20);
		TestEqual(TEXT("nonmatching incoming amount emits no child"), B.Game->BattleState.Meter[1], 7);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierDefinitionAgreement, "NightSky.Modifiers.ExactDefinitionAgreement",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierDefinitionAgreement::RunTest(const FString&)
{
	auto D = Rule(TEXT("Exact"), EModifierEvent::Damage, EModifierOperation::Add, 2);
	const auto C = Config({D});
	for (int32 Mutation = 0; Mutation < 13; ++Mutation)
	{
		auto Other = C;
		auto& Rule = Other.Definitions[0];
		switch (Mutation)
		{
		case 0: Rule.Operations[0].Amount = 3; break;
		case 1: Rule.Priority = -1; break;
		case 2: Rule.Drain = 1; break;
		case 3: Rule.ConvertDamage = true; break;
		case 4: Rule.RestrictMovement = true; break;
		case 5: Rule.SuddenDeath = true; break;
		case 6: Rule.Subscription = EModifierEvent::Meter; break;
		case 7: Rule.MatchAmount = true; Rule.RequiredAmount = 21; break;
		case 8: Rule.ExclusiveGroups.Add(TEXT("Group")); break;
		case 9: Other.Schedule[0].Round = 2; break;
		case 10: Other.Schedule[0].Duration = 3; break;
		case 11: Rule.Operations[0].Operation = EModifierOperation::Multiply; break;
		case 12: Rule.Revision = 2; Other.Schedule[0].Revision = 2; break;
		}
		auto* GI = NewObject<UNightSkyGameInstance>();
		GI->AvailableModifiers = C.Definitions;
		GI->BattleData.Modifiers = C;
		TestTrue(TEXT("control peer agrees before mutation"), GI->AcceptModifierPeer(C));
		TestFalse(TEXT("same identifier cannot hide altered definitions or schedule"), GI->AcceptModifierPeer(Other));
		TestFalse(TEXT("definition mismatch reports a reason"), GI->ModifierSetupError.IsEmpty());
	}
	{
		auto* GI = NewObject<UNightSkyGameInstance>();
		GI->AvailableModifiers = C.Definitions;
		GI->AvailableModifiers[0].Operations[0].Amount = 3;
		FString Reason;
		TestFalse(TEXT("saved revision cannot silently use incompatible installed content"), GI->ValidateModifierContent(C, Reason));
		TestTrue(TEXT("incompatible content identifies the rule"), Reason.Contains(TEXT("Exact")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModifierAtomicDamageCommit, "NightSky.Modifiers.AtomicDamageCommit",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)
bool FModifierAtomicDamageCommit::RunTest(const FString&)
{
	{
		auto Damage = Rule(TEXT("Damage"), EModifierEvent::Damage, EModifierOperation::ChildMeter, 20);
		Damage.ConvertDamage = true;
		auto Debit = Rule(TEXT("Debit"), EModifierEvent::Meter, EModifierOperation::ChildMeter, -20);
		Debit.MatchAmount = true;
		Debit.RequiredAmount = -21;
		FBattle B;
		TestTrue(TEXT("conversion emission ordering accepted"), B.Start(Config({Damage, Debit})));
		B.Game->BattleState.Meter[1] = 100;
		B.Hit(21);
		// Debit 21 -> 79, earlier damage child +20 -> 99, debit child -20 -> 79.
		// Use a larger positive child to expose the clamp-sensitive ordering.
		TestEqual(TEXT("conversion preserves health"), B.P[1]->CurrentHealth, 1000);
		TestEqual(TEXT("conversion and both descendants commit"), B.Game->BattleState.Meter[1], 79);
	}
	{
		auto Damage = Rule(TEXT("Damage"), EModifierEvent::Damage, EModifierOperation::ChildMeter, 40);
		Damage.ConvertDamage = true;
		auto Debit = Rule(TEXT("Debit"), EModifierEvent::Meter, EModifierOperation::ChildMeter, -40);
		Debit.MatchAmount = true;
		Debit.RequiredAmount = -21;
		FBattle B;
		TestTrue(TEXT("clamp-sensitive conversion ordering accepted"), B.Start(Config({Damage, Debit})));
		B.Game->BattleState.Meter[1] = 100;
		B.Hit(21);
		TestEqual(TEXT("damage-authored child precedes later debit-authored child"), B.Game->BattleState.Meter[1], 60);
	}
	// Both children originate at the damage event, whose attacker and victim are
	// defined by the ordinary contact. This does not assign a source identity to
	// a meter event or to damage subsequently emitted by a meter handler.
	{
		auto Parent = Rule(TEXT("Parent"), EModifierEvent::Damage, EModifierOperation::ChildMeter, -100);
		auto AttackerDebit = Parent.Operations[0];
		AttackerDebit.ToAttacker = true;
		Parent.Operations.Add(AttackerDebit);
		FBattle B;
		TestTrue(TEXT("two-gain parent barrier setup accepted"), B.Start(Config({Parent})));
		B.P[0]->MeterPercentOnHit = 200;
		B.P[1]->MeterPercentOnReceiveHit = 100;
		B.Game->BattleState.Meter[0] = 0;
		B.Game->BattleState.Meter[1] = 0;
		B.Hit(21);
		TestEqual(TEXT("attacker ordinary gain precedes damage-authored children"), B.Game->BattleState.Meter[0], 0);
		TestEqual(TEXT("victim ordinary gain precedes damage-authored children"), B.Game->BattleState.Meter[1], 0);
		TestEqual(TEXT("parent health commits once"), B.P[1]->CurrentHealth, 979);
	}
	// Approved source semantics: both ordinary gains retain original source P0.
	// A child of the attacker gain therefore deals P0-to-P0 damage; it does not
	// turn the opponent into an attacker or redirect any descendant to P1.
	for (int32 SelectedGain : {21, 42})
	{
		auto Emit = Rule(TEXT("Emit"), EModifierEvent::Meter, EModifierOperation::ChildDamage, 1);
		Emit.MatchAmount = true;
		Emit.RequiredAmount = SelectedGain;
		auto One = Rule(TEXT("One"), EModifierEvent::Meter, EModifierOperation::Add, -100);
		One.MatchAmount = true;
		One.RequiredAmount = 1;
		auto Two = One;
		Two.Identifier = TEXT("Two");
		Two.RequiredAmount = 2;
		FBattle B;
		TestTrue(TEXT("inherited-source gain barrier setup accepted"), B.Start(Config({Emit, One, Two})));
		B.P[0]->MeterPercentOnHit = 200;
		B.P[0]->MeterPercentOnReceiveHit = 100;
		B.P[1]->MeterPercentOnHit = 100;
		B.P[1]->MeterPercentOnReceiveHit = 100;
		B.Game->BattleState.Meter[0] = 0;
		B.Game->BattleState.Meter[1] = 0;
		B.Hit(21);
		TestEqual(TEXT("ordinary attacker gain precedes authored descendant debits"), B.Game->BattleState.Meter[0], 0);
		TestEqual(TEXT("child target selection never changes inherited source"), B.Game->BattleState.Meter[1], SelectedGain == 21 ? 0 : 21);
		TestEqual(TEXT("attacker-gain child damages source itself"), B.P[0]->CurrentHealth, SelectedGain == 42 ? 999 : 1000);
		TestEqual(TEXT("victim-gain child retains ordinary damage target"), B.P[1]->CurrentHealth, SelectedGain == 21 ? 978 : 979);

		ModifierOracle::FState Oracle;
		Oracle.Frame = 0;
		Oracle.SimulateOrdinaryMeter = true;
		Oracle.PercentOnHit[0] = 200;
		for (const auto& D : {Emit, One, Two})
		{
			ModifierOracle::FRule R;
			R.Id = D.Identifier;
			R.Kind = ModifierOracle::EKind::Meter;
			R.MatchAmount = true;
			R.RequiredAmount = D.RequiredAmount;
			ModifierOracle::FAction Op;
			Op.Kind = D.Identifier == TEXT("Emit") ? ModifierOracle::EAction::DamageChild : ModifierOracle::EAction::Add;
			Op.Value = D.Operations[0].Amount;
			R.Actions.Add(Op);
			Oracle.Rules.Add(R);
		}
		Oracle.Event(ModifierOracle::EKind::Damage, 1, 0, 21);
		for (int32 Team = 0; Team < 2; ++Team)
		{
			TestEqual(TEXT("independent source-preserving oracle health"), Oracle.Health[Team], B.P[Team]->CurrentHealth);
			TestEqual(TEXT("independent atomic-gain oracle meter"), Oracle.Meter[Team], B.Game->BattleState.Meter[Team]);
		}
	}
	return true;
}

#endif
