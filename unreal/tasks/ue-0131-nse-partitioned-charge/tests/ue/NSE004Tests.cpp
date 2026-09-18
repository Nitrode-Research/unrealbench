// Hidden suite for partitioned charge commands (NSE-004). Every scenario drives the real battle through
// FNSE004Battle and observes move starts only. Expected entries are literals derived from the rules in the
// instruction by hand and by a scratch transducer; the fuzz test carries an independent transducer of its own.
#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Math/RandomStream.h"
#include "NightSkyEngine/Fixtures/NSE004Fixture.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"

namespace
{
constexpr int32 N = INP_Neutral;
constexpr int32 L = INP_Left;
constexpr int32 DL = INP_DownLeft;
constexpr int32 DR = INP_DownRight;

int32 SwapLR(int32 In)
{
	int32 Out = In & ~(INP_Left | INP_Right);
	if (In & INP_Left) Out |= INP_Right;
	if (In & INP_Right) Out |= INP_Left;
	return Out;
}

FString InputName(int32 In)
{
	FString Out;
	if (In & INP_Up) Out += TEXT("U");
	if (In & INP_Down) Out += TEXT("D");
	if (In & INP_Left) Out += TEXT("L");
	if (In & INP_Right) Out += TEXT("R");
	if (In & INP_A) Out += TEXT("a");
	if (In & INP_B) Out += TEXT("b");
	if (In & INP_C) Out += TEXT("c");
	return Out.IsEmpty() ? FString(TEXT("N")) : Out;
}

FChargeCommand Cmd(int32 X, int32 R = 6, int32 G = 2, int32 Life = 12,
				   EChargeTrigger Trigger = EChargeTrigger::Release, int32 Y = INP_None)
{
	FChargeCommand C;
	C.ChargeInput = X;
	C.RequiredFrames = R;
	C.MaxGapFrames = G;
	C.LifetimeFrames = Life;
	C.Trigger = Trigger;
	C.TriggerInput = Y;
	return C;
}

FChargeCommand Press(int32 X, int32 Y, int32 R = 6, int32 G = 2, int32 Life = 12)
{
	return Cmd(X, R, G, Life, EChargeTrigger::Press, Y);
}

// An ordinary "press this button" list, the shape a normal attack uses today.
FInputConditionList OnceList(int32 Button)
{
	FInputCondition Condition;
	FInputBitmask Mask;
	Mask.InputFlag = Button;
	Condition.Sequence.Add(Mask);
	Condition.Method = EInputMethod::Once;
	FInputConditionList List;
	List.InputConditions.Add(Condition);
	return List;
}

struct FMoveSpec
{
	int32 Recovery = 4;
	TArray<FChargeCommand> Commands;
	TArray<FInputConditionList> Lists;
	int32 Side = 0;
};

FMoveSpec Move(int32 Recovery, TArray<FChargeCommand> Commands, TArray<FInputConditionList> Lists = {},
			   int32 Side = 0)
{
	return {Recovery, MoveTemp(Commands), MoveTemp(Lists), Side};
}

FMoveSpec FiveCommandMove()
{
	return Move(2, {Cmd(INP_Left), Cmd(INP_Down), Cmd(INP_Right), Cmd(INP_Up), Cmd(INP_B, 4)});
}

enum class EOp : uint8
{
	Step,
	Hitstop,
	SuperFreeze,
	SwitchSides,
	ResetRound,
	Save,
	Restore,
	Enterable,
	ExpectClock,
	ExpectCurrent,
};

struct FOp
{
	EOp Kind = EOp::Step;
	int32 A = N;
	int32 B = N;
	int32 Side = 0;
};

struct FTrace
{
	TArray<FOp> Ops;
	FTrace& Hold(int32 In, int32 K = 1)
	{
		for (int32 I = 0; I < K; ++I) Ops.Add({EOp::Step, In, N});
		return *this;
	}
	FTrace& Neutral(int32 K = 1) { return Hold(N, K); }
	FTrace& Two(int32 I1, int32 I2, int32 K = 1)
	{
		for (int32 I = 0; I < K; ++I) Ops.Add({EOp::Step, I1, I2});
		return *this;
	}
	FTrace& Hitstop(int32 Frames, int32 Side = 0)
	{
		Ops.Add({EOp::Hitstop, Frames, 0, Side});
		return *this;
	}
	FTrace& Freeze(int32 Frames)
	{
		Ops.Add({EOp::SuperFreeze, Frames});
		return *this;
	}
	FTrace& Switch()
	{
		Ops.Add({EOp::SwitchSides});
		return *this;
	}
	FTrace& Reset()
	{
		Ops.Add({EOp::ResetRound});
		return *this;
	}
	FTrace& Save()
	{
		Ops.Add({EOp::Save});
		return *this;
	}
	FTrace& Restore()
	{
		Ops.Add({EOp::Restore});
		return *this;
	}
	FTrace& Enterable(int32 MoveIndex, bool bEnterable, int32 Side = 0)
	{
		Ops.Add({EOp::Enterable, MoveIndex, bEnterable ? 1 : 0, Side});
		return *this;
	}
	FTrace& Clock(int32 Expected, int32 Side = 0)
	{
		Ops.Add({EOp::ExpectClock, Expected, 0, Side});
		return *this;
	}
	FTrace& Current(int32 Expected, int32 Side = 0)
	{
		Ops.Add({EOp::ExpectCurrent, Expected, 0, Side});
		return *this;
	}
};

FTrace T()
{
	return FTrace();
}

struct FScenario
{
	FString Name;
	TArray<FMoveSpec> Moves;
	TArray<FOp> Ops;
	TArray<FNSE004Entry> Expected[2];
};

FScenario Scenario(const TCHAR* Name, TArray<FMoveSpec> Moves, const FTrace& Trace, TArray<FNSE004Entry> Expected0,
				   TArray<FNSE004Entry> Expected1 = {})
{
	FScenario S;
	S.Name = Name;
	S.Moves = MoveTemp(Moves);
	S.Ops = Trace.Ops;
	S.Expected[0] = MoveTemp(Expected0);
	S.Expected[1] = MoveTemp(Expected1);
	return S;
}

FString EntriesToString(const TArray<FNSE004Entry>& Entries)
{
	FString Out = TEXT("[");
	for (int32 I = 0; I < Entries.Num(); ++I)
	{
		Out += FString::Printf(TEXT("%s(%d,%d)"), I ? TEXT(" ") : TEXT(""), Entries[I].Frame, Entries[I].Move);
	}
	return Out + TEXT("]");
}

// Runs one scenario on a fresh battle. Mirrored runs switch sides before the first step and swap Left and
// Right in every physical input, so every facing-relative input is the same as in the plain run.
void RunScenario(FAutomationTestBase& Test, const FScenario& S, bool bMirror)
{
	const FString Label = S.Name + (bMirror ? TEXT(" [mirror]") : TEXT(""));
	FNSE004Battle Battle;
	for (const FMoveSpec& M : S.Moves)
	{
		Battle.AddMove(M.Recovery, M.Commands, M.Lists, M.Side);
	}
	if (bMirror)
	{
		Battle.SwitchSides();
	}
	FRollbackData Snapshot;
	int32 Steps = 0;
	for (const FOp& Op : S.Ops)
	{
		switch (Op.Kind)
		{
		case EOp::Step:
			++Steps;
			Battle.Step(bMirror ? SwapLR(Op.A) : Op.A, bMirror ? SwapLR(Op.B) : Op.B);
			break;
		case EOp::Hitstop:
			Battle.Hitstop(Op.A, Op.Side);
			break;
		case EOp::SuperFreeze:
			Battle.SuperFreeze(Op.A);
			break;
		case EOp::SwitchSides:
			Battle.SwitchSides();
			break;
		case EOp::ResetRound:
			Battle.ResetRound();
			break;
		case EOp::Save:
			Snapshot = Battle.Save();
			break;
		case EOp::Restore:
			Battle.Restore(Snapshot);
			break;
		case EOp::Enterable:
			Battle.SetMoveEnterable(Op.A, Op.B != 0, Op.Side);
			break;
		case EOp::ExpectClock:
			Test.TestEqual(FString::Printf(TEXT("%s: side %d ClockFrames after step %d"), *Label, Op.Side, Steps),
						   Battle.Sides[Op.Side].ClockFrames, Op.A);
			break;
		case EOp::ExpectCurrent:
			Test.TestEqual(FString::Printf(TEXT("%s: side %d CurrentMove after step %d"), *Label, Op.Side, Steps),
						   Battle.CurrentMove(Op.Side), Op.A);
			break;
		}
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FString Actual = EntriesToString(Battle.Sides[Side].Entries);
		Test.TestEqual(FString::Printf(TEXT("%s: side %d move starts (frame,move)"), *Label, Side), Actual,
					   EntriesToString(S.Expected[Side]));
	}
}

void RunAll(FAutomationTestBase& Test, const TArray<FScenario>& Scenarios, bool bMirror = false)
{
	for (const FScenario& S : Scenarios)
	{
		RunScenario(Test, S, bMirror);
	}
}

// Default command: charge back (Left) for 6 frames, gap 2, lifetime 12, release trigger; recovery 4.
TArray<FScenario> ScenariosR1()
{
	return {
		Scenario(TEXT("R1 hold 6 release"), {Move(4, {Cmd(L)})}, T().Hold(L, 6).Neutral(), {{7, 0}}),
		Scenario(TEXT("R1 hold 5 release"), {Move(4, {Cmd(L)})}, T().Hold(L, 5).Neutral(), {}),
		Scenario(TEXT("R1 hold 5, gap 1, hold 1, release"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 5).Neutral().Hold(L).Neutral(), {{8, 0}}),
		Scenario(TEXT("R1 hold 3, gap 2, hold 3, release"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 3).Neutral(2).Hold(L, 3).Neutral(), {{9, 0}}),
		Scenario(TEXT("R1 three segments L L N L L N N L L N"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 2).Neutral().Hold(L, 2).Neutral(2).Hold(L, 2).Neutral(), {{10, 0}}),
		Scenario(TEXT("R1 press trigger Right"), {Move(4, {Press(L, INP_Right)})}, T().Hold(L, 6).Hold(INP_Right),
				 {{7, 0}}),
		Scenario(TEXT("R1 press trigger DownRight superset of Right"), {Move(4, {Press(L, INP_Right)})},
				 T().Hold(L, 6).Hold(DR), {{7, 0}}),
		// Ordinary regression: the same on the unmodified tree.
		Scenario(TEXT("R1 ordinary Poke [[A Once]] Down N A, A held 8 frames"), {Move(6, {}, {OnceList(INP_A)})},
				 T().Hold(INP_Down).Neutral().Hold(INP_A, 8).Neutral(2), {{3, 0}}),
		Scenario(TEXT("R1 both kinds: ordinary C press"), {Move(6, {Cmd(L, 6, 12, 30)}, {OnceList(INP_C)})},
				 T().Hold(INP_Down).Neutral().Hold(INP_C), {{3, 0}}),
		Scenario(TEXT("R1 both kinds: charge release"), {Move(6, {Cmd(L, 6, 12, 30)}, {OnceList(INP_C)})},
				 T().Hold(L, 6).Neutral(), {{7, 0}}),
		Scenario(TEXT("R1 both kinds: entry at 7 consumes the charge, L 14..16 N 17 finds 3"),
				 {Move(6, {Cmd(L, 6, 12, 30)}, {OnceList(INP_C)})},
				 T().Hold(L, 6).Hold(INP_C).Neutral(6).Hold(L, 3).Neutral(), {{7, 0}}),
	};
}

TArray<FScenario> ScenariosR2R3()
{
	return {
		Scenario(TEXT("R2 gap of exactly G=2 kept"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 3).Neutral(2).Hold(L, 3).Neutral(), {{9, 0}}),
		Scenario(TEXT("R2 gap 3 clears at frame 6, nothing at 10, 3+3 since the clear enters at 14"),
				 {Move(4, {Cmd(L)})}, T().Hold(L, 3).Neutral(3).Hold(L, 3).Neutral().Hold(L, 3).Neutral(),
				 {{14, 0}}),
		Scenario(TEXT("R2 cleared charge restarts from zero at frame 7"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 3).Neutral(3).Hold(L, 6).Neutral(), {{13, 0}}),
		Scenario(TEXT("R2 G=0 hold 6 release (release frame is not a gap)"), {Move(4, {Cmd(L, 6, 0)})},
				 T().Hold(L, 6).Neutral(), {{7, 0}}),
		Scenario(TEXT("R2 G=0 hold 5 N L N (one neutral frame clears)"), {Move(4, {Cmd(L, 6, 0)})},
				 T().Hold(L, 5).Neutral().Hold(L).Neutral(), {}),
		Scenario(TEXT("R2 G=-3 hold 6 release"), {Move(4, {Cmd(L, 6, -3)})}, T().Hold(L, 6).Neutral(), {{7, 0}}),
		Scenario(TEXT("R2 G=-3 hold 5 N L N"), {Move(4, {Cmd(L, 6, -3)})},
				 T().Hold(L, 5).Neutral().Hold(L).Neutral(), {}),
		Scenario(TEXT("R3 L=6 hold 6 release"), {Move(4, {Cmd(L, 6, 2, 6)})}, T().Hold(L, 6).Neutral(), {{7, 0}}),
		Scenario(TEXT("R3 L=6 hold 7 release (age 7 cleared on frame 7)"), {Move(4, {Cmd(L, 6, 2, 6)})},
				 T().Hold(L, 7).Neutral(), {}),
		// Frame 7 expires the first charge and its hold is discarded: 8..13 give 6, so 14 enters; 8..12 give
		// only 5, so 13 does not.
		Scenario(TEXT("R3 L=6 hold 13 release at 14"), {Move(4, {Cmd(L, 6, 2, 6)})},
				 T().Hold(L, 13).Neutral(), {{14, 0}}),
		Scenario(TEXT("R3 L=6 hold 12 release at 13"), {Move(4, {Cmd(L, 6, 2, 6)})},
				 T().Hold(L, 12).Neutral(), {}),
		// The expiry frame never begins the next charge: with R=1 and L=2 the third held frame expires on frame 3
		// and leaves zero, so the release at 4 does not enter; a restart on frame 3 would enter at 4.
		Scenario(TEXT("R3 L=2 R=1 hold 3 release at 4 finds no restarted charge"), {Move(4, {Cmd(L, 1, 2, 2)})},
				 T().Hold(L, 3).Neutral(), {}),
		Scenario(TEXT("R3 L=8 hold 5 N N L N (age 8 through frame 8)"), {Move(4, {Cmd(L, 6, 2, 8)})},
				 T().Hold(L, 5).Neutral(2).Hold(L).Neutral(), {{9, 0}}),
		Scenario(TEXT("R3 L=8 hold 5 N N L L N (age 9 clears on frame 9)"), {Move(4, {Cmd(L, 6, 2, 8)})},
				 T().Hold(L, 5).Neutral(2).Hold(L, 2).Neutral(), {}),
		Scenario(TEXT("R3/R8 L=5 below R=6 never activates"), {Move(4, {Cmd(L, 6, 2, 5)})},
				 T().Hold(L, 6).Neutral().Hold(L, 5).Neutral().Hold(L, 4).Neutral().Hold(L, 3).Neutral(3), {}),
	};
}

TArray<FScenario> ScenariosR4()
{
	return {
		Scenario(TEXT("R4 hold 6 then 10 neutral frames: one entry"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 6).Neutral(10), {{7, 0}}),
		Scenario(TEXT("R4 second release finds charge 1"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 6).Neutral().Hold(L).Neutral(), {{7, 0}}),
		Scenario(TEXT("R4 hold 12: consumed, not reduced by 6"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 12).Neutral(4).Hold(L).Neutral(), {{13, 0}}),
		Scenario(TEXT("R4 held press trigger does not fire again, re-press finds 0"),
				 {Move(4, {Press(L, INP_Right)})}, T().Hold(L, 6).Hold(INP_Right, 10).Neutral().Hold(INP_Right),
				 {{7, 0}}),
		Scenario(TEXT("R4 press is an edge: Down 6, Down+A x14 (a level trigger would enter again at 14)"),
				 {Move(4, {Press(INP_Down, INP_A, 6, 2, 40)})}, T().Hold(INP_Down, 6).Hold(INP_Down | INP_A, 14),
				 {{7, 0}}),
		Scenario(TEXT("R4 press while holding: activation frame adds nothing, 8..13 give 6"),
				 {Move(4, {Press(INP_Down, INP_A)})},
				 T().Hold(INP_Down, 6).Hold(INP_Down | INP_A, 6).Hold(INP_Down).Hold(INP_Down | INP_A),
				 {{7, 0}, {14, 0}}),
		Scenario(TEXT("R4 press while holding: 8..12 give 5, no entry at 13"), {Move(4, {Press(INP_Down, INP_A)})},
				 T().Hold(INP_Down, 6).Hold(INP_Down | INP_A, 5).Hold(INP_Down).Hold(INP_Down | INP_A), {{7, 0}}),
		Scenario(TEXT("R4 diagonal DownLeft x6 then Down releases Left"), {Move(4, {Cmd(L)})},
				 T().Hold(DL, 6).Hold(INP_Down), {{7, 0}}),
		Scenario(TEXT("R4 hold 6 then DownLeft still holds Left, release at 8"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 6).Hold(DL).Neutral(), {{8, 0}}),
		Scenario(TEXT("R4/R8 invalid R=0 never activates"), {Move(4, {Cmd(L, 0)})}, T().Hold(L, 6).Neutral(), {}),
		Scenario(TEXT("R4/R8 invalid press trigger with no trigger input"), {Move(4, {Press(L, INP_None)})},
				 T().Hold(L, 6).Hold(INP_Right).Neutral(), {}),
		Scenario(TEXT("R4/R8 invalid empty charge input is not vacuously held"),
				 {Move(4, {Press(INP_None, INP_A, 1)})}, T().Neutral(3).Hold(INP_A).Neutral(), {}),
	};
}

TArray<FScenario> ScenariosR5()
{
	return {
		Scenario(TEXT("R5 L=6 hitstop(4): frozen frames count neither as held nor toward lifetime"),
				 {Move(4, {Cmd(L, 6, 2, 6)})},
				 T().Hold(L, 3).Hitstop(4).Hold(L, 3).Clock(3).Hold(L, 3).Neutral().Clock(7), {{10, 0}}),
		Scenario(TEXT("R5 release during hitstop(4) is seen on the first unfrozen frame 10"),
				 {Move(4, {Cmd(L)})}, T().Hold(L, 6).Hitstop(4).Neutral(3).Clock(6).Neutral(), {{10, 0}}),
		Scenario(TEXT("R5 release and re-press inside hitstop(4) is no edge, charge continues to 7"),
				 {Move(4, {Cmd(L)})}, T().Hold(L, 6).Hitstop(4).Neutral(2).Hold(L).Clock(6).Hold(L).Neutral(),
				 {{11, 0}}),
		Scenario(TEXT("R5 no move starts on a frozen frame"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 6).Hitstop(4).Neutral(3).Clock(6), {}),
		Scenario(TEXT("R5 L=6 super freeze(3): frozen frames count neither as held nor toward lifetime"),
				 {Move(4, {Cmd(L, 6, 2, 6)})},
				 T().Hold(L, 3).Freeze(3).Hold(L, 3).Clock(3).Hold(L, 3).Neutral().Clock(7), {{10, 0}}),
		Scenario(TEXT("R5 release during super freeze(3) is seen on the first unfrozen frame 10"),
				 {Move(4, {Cmd(L)})}, T().Hold(L, 6).Freeze(3).Neutral(3).Clock(6).Neutral(), {{10, 0}}),
		Scenario(TEXT("R5 release and re-press inside super freeze(3) is no edge"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 6).Freeze(3).Neutral(2).Hold(L).Clock(6).Hold(L).Neutral(), {{11, 0}}),
		Scenario(TEXT("R5 frozen neutral frames do not lengthen a gap of 2"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 3).Neutral(2).Hitstop(6).Neutral(5).Clock(5).Hold(L, 3).Neutral().Clock(9), {{14, 0}}),
	};
}

TArray<FScenario> ScenariosR6()
{
	TArray<FScenario> Out = {
		Scenario(TEXT("R6 recovery 8: blocked release keeps charge 6, gap 1, entry at 16"), {Move(8, {Cmd(L)})},
				 T().Hold(L, 6).Neutral().Hold(L, 6).Neutral().Hold(L).Neutral(), {{7, 0}, {16, 0}}),
		Scenario(TEXT("R6 recovery 8, G=0: blocked frame 14 is a gap and clears, 15..20 rebuild"),
				 {Move(8, {Cmd(L, 6, 0)})}, T().Hold(L, 6).Neutral().Hold(L, 6).Neutral().Hold(L, 6).Neutral(),
				 {{7, 0}, {21, 0}}),
		Scenario(TEXT("R6 recovery 8, G=0: charge 1 at 16, no second entry"), {Move(8, {Cmd(L, 6, 0)})},
				 T().Hold(L, 6).Neutral().Hold(L, 6).Neutral().Hold(L).Neutral(), {{7, 0}}),
		Scenario(TEXT("R6 blocked trigger is not remembered"), {Move(8, {Cmd(L)})},
				 T().Hold(L, 6).Neutral().Hold(L, 6).Neutral(6), {{7, 0}}),
		Scenario(TEXT("R6 competition: Move1 (R=6, added later) wins at 7, Move0 (R=4) keeps 6 and enters at 9"),
				 {Move(2, {Cmd(L, 4)}), Move(2, {Cmd(L, 6)})}, T().Hold(L, 6).Neutral().Hold(L).Neutral(),
				 {{7, 1}, {9, 0}}),
		Scenario(TEXT("R6 per input: Move0 charges Left, Move1 charges Down, DownLeft x6 Up L N"),
				 {Move(2, {Cmd(L)}), Move(2, {Cmd(INP_Down)})}, T().Hold(DL, 6).Hold(INP_Up).Hold(L).Neutral(),
				 {{7, 1}, {9, 0}}),
		Scenario(TEXT("R6 ordinary Poke added first, charge move wins at 7 by priority"),
				 {Move(6, {}, {OnceList(INP_A)}), Move(6, {Cmd(L)})}, T().Hold(L, 6).Hold(INP_A).Neutral(8),
				 {{7, 1}}),
		Scenario(TEXT("R6 charge move added first, ordinary Poke wins at 7 by priority"),
				 {Move(6, {Cmd(L)}), Move(6, {}, {OnceList(INP_A)})}, T().Hold(L, 6).Hold(INP_A).Neutral(8),
				 {{7, 1}}),
		Scenario(TEXT("R6 states past the walk accumulate on the entry frame (Move0 Down R=7 reaches 7)"),
				 {Move(1, {Cmd(INP_Down, 7)}), Move(1, {Cmd(L)})}, T().Hold(DL, 6).Hold(INP_Down).Hold(INP_Up),
				 {{7, 1}, {8, 0}}),
		Scenario(TEXT("R6 two commands on one move both fire at 7 and both are consumed"),
				 {Move(2, {Cmd(L), Cmd(INP_Down)})}, T().Hold(DL, 6).Hold(INP_Up).Hold(DL, 2).Neutral(), {{7, 0}}),
		Scenario(TEXT("R6 two commands: Down fires at 7, Left keeps 4 and accumulates on 7 and 8"),
				 {Move(2, {Cmd(L), Cmd(INP_Down)})}, T().Hold(DL, 4).Hold(INP_Down, 2).Hold(L, 2).Neutral(),
				 {{7, 0}, {9, 0}}),
		Scenario(TEXT("R6 five commands: the fifth (B, R=4) activates"), {FiveCommandMove()},
				 T().Hold(INP_B, 4).Neutral(), {{5, 0}}),
		Scenario(TEXT("R6 five commands: fourth (Up) then fifth (B)"), {FiveCommandMove()},
				 T().Hold(INP_Up, 6).Neutral().Hold(INP_B, 4).Neutral(), {{7, 0}, {12, 0}}),
		Scenario(TEXT("R6 five commands: B+Up x6, both satisfied commands consumed at 7"), {FiveCommandMove()},
				 T().Hold(INP_B | INP_Up, 6).Neutral().Hold(INP_B | INP_Up, 2).Neutral(), {{7, 0}}),
		Scenario(TEXT("R6 gate: release while not enterable is blocked, keeps 6, gap 1, entry at 9"),
				 {Move(4, {Cmd(L)})},
				 T().Hold(L, 6).Enterable(0, false).Neutral().Enterable(0, true).Hold(L).Neutral(), {{9, 0}}),
		Scenario(TEXT("R6 gate, G=0: blocked frame 7 clears, 8..13 rebuild, entry at 14"),
				 {Move(4, {Cmd(L, 6, 0)})},
				 T().Hold(L, 6).Enterable(0, false).Neutral().Enterable(0, true).Hold(L, 6).Neutral(), {{14, 0}}),
		Scenario(TEXT("R6 gate: Move1 not enterable is skipped although it has priority and charge"),
				 {Move(2, {Cmd(L, 4)}), Move(2, {Cmd(L, 6)})},
				 T().Enterable(1, false).Hold(L, 6).Neutral().Hold(L).Neutral(), {{7, 0}}),
		Scenario(TEXT("R6 gate: ordinary Poke not enterable never starts"), {Move(6, {}, {OnceList(INP_A)})},
				 T().Enterable(0, false).Hold(INP_Down).Neutral().Hold(INP_A).Neutral(3), {}),
		// Composed: W (added later) has an ordinary C list, a Down/A press command (R=6) and a Left release
		// command (R=10); Q has a Down/A press command (R=6). DownLeft x6, then DownLeft+A+C at 7: W enters
		// through C and its satisfied Down command is consumed, its Left command counts 7 and reaches 10 at 10,
		// Q keeps 6 and grows. A alone at 11: W's Left release wins over Q's satisfied press; W's Down command
		// has 3. W not enterable, DownLeft 12..14, A at 15: Q, idle since W's recovery ended, finds 13.
		Scenario(TEXT("R6 composed: W(C list, Down/A R=6, Left R=10) over Q(Down/A R=6)"),
				 {Move(2, {Press(INP_Down, INP_A, 6, 10, 40)}),
				  Move(4, {Press(INP_Down, INP_A, 6, 10, 40), Cmd(L, 10, 10, 40)}, {OnceList(INP_C)})},
				 T().Hold(DL, 6)
					 .Hold(DL | INP_A | INP_C)
					 .Hold(DL, 3)
					 .Hold(INP_A)
					 .Enterable(1, false)
					 .Hold(DL, 3)
					 .Hold(INP_A),
				 {{7, 1}, {11, 1}, {15, 0}}),
		// Same without W's Left command: at 11 W has 3 on its Down command and no C, so Q wins there and is
		// consumed; at 15 Q finds 3. A solver that consumes only the command that caused the entry at 7 keeps
		// 10 on W's Down command and enters W at 11.
		Scenario(TEXT("R6 composed companion: W(C list, Down/A R=6) over Q(Down/A R=6), Q wins at 11"),
				 {Move(2, {Press(INP_Down, INP_A, 6, 10, 40)}),
				  Move(4, {Press(INP_Down, INP_A, 6, 10, 40)}, {OnceList(INP_C)})},
				 T().Hold(DL, 6)
					 .Hold(DL | INP_A | INP_C)
					 .Hold(DL, 3)
					 .Hold(INP_A)
					 .Enterable(1, false)
					 .Hold(DL, 3)
					 .Hold(INP_A),
				 {{7, 1}, {11, 0}}),
		Scenario(TEXT("R6 two players with identical commands, Player hitstop, save and restore"),
				 {Move(4, {Cmd(L)}, {}, 0), Move(4, {Cmd(L)}, {}, 1)},
				 T().Two(L, INP_Right, 6)
					 .Two(N, INP_Right)
					 .Hitstop(4)
					 .Two(L, N, 3)
					 .Two(L, INP_Right, 6)
					 .Save()
					 .Two(N, N, 3)
					 .Restore()
					 .Two(N, N)
					 .Clock(17, 0)
					 .Clock(20, 1),
				 {{7, 0}, {17, 0}, {17, 0}}, {{8, 0}, {17, 0}, {17, 0}}),
	};
	// Fifteen commands on one player: each five-command move activates through its fifth command while the
	// other two are not enterable.
	for (int32 M = 0; M < 3; ++M)
	{
		FTrace Trace;
		for (int32 Other = 0; Other < 3; ++Other)
		{
			if (Other != M) Trace.Enterable(Other, false);
		}
		Trace.Hold(INP_B, 4).Neutral();
		Out.Add(Scenario(*FString::Printf(TEXT("R6 fifteen commands, move %d B x4 release"), M),
						 {FiveCommandMove(), FiveCommandMove(), FiveCommandMove()}, Trace, {{5, M}}));
	}
	return Out;
}

TArray<FScenario> ScenariosR7()
{
	return {
		Scenario(TEXT("R7 hold 4, switch, physical Right x2 continues back, physical Left releases"),
				 {Move(4, {Cmd(L)})}, T().Hold(L, 4).Switch().Hold(INP_Right, 2).Hold(L), {{7, 0}}),
		Scenario(TEXT("R7 hold 4, switch, physical Left x2 is forward: gap 3 clears"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 4).Switch().Hold(L, 2).Neutral(), {}),
		Scenario(TEXT("R7 R=4: a stick that stays put across the switch is no release, entry at 9"),
				 {Move(4, {Cmd(L, 4)})},
				 T().Hold(L, 4).Switch().Hold(L, 2).Hold(INP_Right, 2).Neutral(), {{9, 0}}),
		Scenario(TEXT("R7 R=4 press Right: no press on the switch frame, press fires at 7"),
				 {Move(4, {Press(L, INP_Right, 4)})}, T().Hold(L, 4).Switch().Hold(L).Neutral().Hold(L), {{7, 0}}),
	};
}

TArray<FScenario> ScenariosR8()
{
	return {
		Scenario(TEXT("R8 round reset clears charge and previous-frame memory"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 6).Reset().Neutral().Hold(L, 6).Neutral(), {{14, 0}}),
		Scenario(TEXT("R8 reset mid recovery returns to idle and clears the new charge"), {Move(6, {Cmd(L)})},
				 T().Hold(L, 6).Neutral().Hold(L, 2).Reset().Hold(L, 6).Neutral(), {{7, 0}, {16, 0}}),
		Scenario(TEXT("R8 ordinary Poke after a reset"), {Move(6, {}, {OnceList(INP_A)})},
				 T().Hold(INP_Down).Neutral().Hold(INP_A).Neutral(3).Reset().Hold(INP_Down).Neutral().Hold(INP_A),
				 {{3, 0}, {9, 0}}),
	};
}

TArray<FScenario> ScenariosR9()
{
	return {
		Scenario(TEXT("R9 save while holding, replay identical inputs reproduces the entry"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 6).Save().Neutral(6).Restore().Neutral(6), {{7, 0}, {7, 0}}),
		Scenario(TEXT("R9 divergent continuation undoes the consumption"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 6).Save().Neutral(6).Restore().Hold(L, 2).Neutral(), {{7, 0}, {9, 0}}),
		Scenario(TEXT("R9 clearing in the discarded future is undone"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 4).Save().Neutral(3).Restore().Hold(L, 2).Neutral(), {{7, 0}}),
		Scenario(TEXT("R9 restore mid recovery (a): L 10..15, N 16"), {Move(6, {Cmd(L)})},
				 T().Hold(L, 6).Neutral(3).Save().Neutral(6).Restore().Hold(L, 6).Neutral(), {{7, 0}, {16, 0}}),
		Scenario(TEXT("R9 restore mid recovery (b): L L, N at 12 blocked, L 13..16, N 17"), {Move(6, {Cmd(L)})},
				 T().Hold(L, 6).Neutral(3).Save().Neutral(6).Restore().Hold(L, 2).Neutral().Hold(L, 4).Neutral(),
				 {{7, 0}, {17, 0}}),
		Scenario(TEXT("R9 restore mid recovery (c): L 10..12, N 13 idle but charge 3"), {Move(6, {Cmd(L)})},
				 T().Hold(L, 6).Neutral(3).Save().Neutral(6).Restore().Hold(L, 3).Neutral(), {{7, 0}}),
		Scenario(TEXT("R9 same suffix replayed twice from one snapshot"), {Move(4, {Cmd(L)})},
				 T().Hold(L, 6).Save().Neutral(6).Restore().Neutral(6).Restore().Neutral(6),
				 {{7, 0}, {7, 0}, {7, 0}}),
		Scenario(TEXT("R9 fifth command's charge across rollback"), {FiveCommandMove()},
				 T().Hold(INP_B, 4).Save().Neutral(4).Restore().Hold(INP_B, 2).Neutral(), {{5, 0}, {7, 0}}),
		// Back charge with a forward press trigger (R=4, G=3). Physical Left x4 facing right, then hitstop(4)
		// and a side switch, so physical Left is forward from frame 5 on; frames 5..7 are frozen and the save
		// is taken after 6. Frame 8 re-reads frame 4's sample as forward, so forward held is no press; N at 9;
		// the press at 10 finds charge 4 with a gap of 2. The identical suffix from the snapshot enters at 10
		// again; a suffix holding back at 8 and pressing forward at 9 enters at 9 with charge 5.
		Scenario(TEXT("R9 save inside hitstop after a switch: identical suffix at 10, divergent suffix at 9"),
				 {Move(4, {Press(L, INP_Right, 4, 3, 40)})},
				 T().Hold(L, 4)
					 .Hitstop(4)
					 .Switch()
					 .Hold(L, 2)
					 .Save()
					 .Hold(L)
					 .Clock(4)
					 .Hold(L)
					 .Neutral()
					 .Hold(L)
					 .Restore()
					 .Hold(L)
					 .Clock(7)
					 .Hold(L)
					 .Neutral()
					 .Hold(L)
					 .Restore()
					 .Hold(L)
					 .Clock(10)
					 .Hold(INP_Right)
					 .Hold(L),
				 {{10, 0}, {10, 0}, {9, 0}}),
		// Save during recovery with a live charge on the move's other command: DownLeft x4, Down x2, L at 7
		// (Down fires with 6, entry, recovery 2), L at 8 (Left has 4 + 7 + 8 = 6), save. N at 9 releases Left
		// with 6 (entry, consumed), N N; restore to 8; L at 9 makes 7 and N at 10 enters.
		Scenario(TEXT("R9 save in recovery with a live charge on another command of the move"),
				 {Move(2, {Cmd(L), Cmd(INP_Down)})},
				 T().Hold(DL, 4).Hold(INP_Down, 2).Hold(L, 2).Save().Neutral(3).Restore().Hold(L).Neutral(),
				 {{7, 0}, {9, 0}, {10, 0}}),
		// Fixture self-check: the ordinary move's recovery is rolled back with the object. Passes on any tree.
		Scenario(TEXT("R9 fixture self-check: ordinary Poke recovery restored"), {Move(6, {}, {OnceList(INP_A)})},
				 T().Hold(INP_Down)
					 .Neutral()
					 .Hold(INP_A)
					 .Neutral(3)
					 .Save()
					 .Neutral(6)
					 .Restore()
					 .Neutral()
					 .Current(0)
					 .Clock(13)
					 .Neutral()
					 .Current(0)
					 .Clock(14)
					 .Neutral()
					 .Current(INDEX_NONE)
					 .Clock(15),
				 {{3, 0}}),
	};
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE004V1, "UnrealBench.NSE004.V1",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE004V1::RunTest(const FString&)
{
	RunAll(*this, ScenariosR1());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE004V2, "UnrealBench.NSE004.V2",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE004V2::RunTest(const FString&)
{
	RunAll(*this, ScenariosR2R3());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE004V3, "UnrealBench.NSE004.V3",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE004V3::RunTest(const FString&)
{
	RunAll(*this, ScenariosR4());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE004V4, "UnrealBench.NSE004.V4",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE004V4::RunTest(const FString&)
{
	RunAll(*this, ScenariosR5());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE004V5, "UnrealBench.NSE004.V5",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE004V5::RunTest(const FString&)
{
	RunAll(*this, ScenariosR6());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE004V6, "UnrealBench.NSE004.V6",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE004V6::RunTest(const FString&)
{
	RunAll(*this, ScenariosR7());
	RunAll(*this, ScenariosR8());
	// Mirror metamorphic: every V1-V5 scenario with Left and Right swapped and the sides switched.
	RunAll(*this, ScenariosR1(), true);
	RunAll(*this, ScenariosR2R3(), true);
	RunAll(*this, ScenariosR4(), true);
	RunAll(*this, ScenariosR5(), true);
	RunAll(*this, ScenariosR6(), true);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE004V7, "UnrealBench.NSE004.V7",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE004V7::RunTest(const FString&)
{
	RunAll(*this, ScenariosR9());
	return !HasAnyErrors();
}

// ---------------------------------------------------------------------------------------------------------
// V8: seeded stateful fuzz against an independent per-frame transducer over the raw input trace.
// ---------------------------------------------------------------------------------------------------------
namespace
{
bool Held(int32 Mask, int32 In)
{
	return In >= 0 && (In & Mask) == Mask;
}

struct FOracleCommand
{
	int32 X = INP_None, R = 0, G = 0, Life = 0, Y = INP_None;
	bool bPress = false;
	int32 Charge = 0, Age = 0, Gap = 0;
	const TCHAR* LastRule = TEXT("init");
	bool Valid() const { return X != INP_None && R >= 1 && Life >= R && (!bPress || Y != INP_None); }
	void Clear() { Charge = Age = Gap = 0; }
	FString Describe() const
	{
		return FString::Printf(TEXT("X=%s R=%d G=%d L=%d %s%s charge=%d age=%d gap=%d rule=%s"), *InputName(X), R,
							   G, Life, bPress ? TEXT("press ") : TEXT("release"), bPress ? *InputName(Y) : TEXT(""),
							   Charge, Age, Gap, LastRule);
	}
};

FOracleCommand ToOracle(const FChargeCommand& C)
{
	FOracleCommand O;
	O.X = C.ChargeInput;
	O.R = C.RequiredFrames;
	O.G = C.MaxGapFrames;
	O.Life = C.LifetimeFrames;
	O.bPress = C.Trigger == EChargeTrigger::Press;
	O.Y = C.TriggerInput;
	return O;
}

struct FOracleMove
{
	int32 Recovery = 1;
	TArray<FOracleCommand> Commands;
};

struct FOracleSnapshot
{
	TArray<int32> Counters;
	int32 Prev = -1;
	int32 Owed = 0;
};

// One player's transducer: previous unfrozen facing-relative input, unfrozen frame count, busy window.
struct FOraclePlayer
{
	TArray<FOracleMove> Moves;
	// A held frame that expires the charge is discarded (false) or is the first frame of the new charge (true).
	bool bRestartOnExpiry = false;
	int32 Prev = -1;
	int32 U = 0;
	int32 BusyUntil = 0;
	int32 ExpectedStarts = 0;

	void Reset()
	{
		for (FOracleMove& M : Moves)
		{
			for (FOracleCommand& C : M.Commands)
			{
				C.Clear();
				C.LastRule = TEXT("reset");
			}
		}
		Prev = -1;
		BusyUntil = 0;
	}

	// One unfrozen frame with facing-relative input E. Returns the move expected to start, or INDEX_NONE.
	int32 Advance(int32 E)
	{
		U++;
		const bool bIdle = U >= BusyUntil;
		TArray<TArray<bool>> Fires;
		Fires.SetNum(Moves.Num());
		for (int32 M = 0; M < Moves.Num(); ++M)
		{
			for (const FOracleCommand& C : Moves[M].Commands)
			{
				bool bFires = C.Valid() && C.Charge >= C.R;
				if (C.bPress) bFires = bFires && !Held(C.Y, Prev) && Held(C.Y, E);
				else bFires = bFires && Held(C.X, Prev) && !Held(C.X, E);
				Fires[M].Add(bFires);
			}
		}
		int32 Winner = INDEX_NONE;
		for (int32 M = Moves.Num() - 1; M >= 0 && bIdle; --M)
		{
			if (Fires[M].Contains(true))
			{
				Winner = M;
				break;
			}
		}
		if (Winner != INDEX_NONE)
		{
			BusyUntil = U + Moves[Winner].Recovery;
			ExpectedStarts++;
		}
		for (int32 M = 0; M < Moves.Num(); ++M)
		{
			for (int32 K = 0; K < Moves[M].Commands.Num(); ++K)
			{
				FOracleCommand& C = Moves[M].Commands[K];
				if (M == Winner && Fires[M][K])
				{
					C.Clear();
					C.LastRule = TEXT("consumed");
					continue;
				}
				if (Held(C.X, E))
				{
					C.Charge++;
					C.Age++;
					C.Gap = 0;
					C.LastRule = Fires[M][K] ? TEXT("trigger not started, held") : TEXT("held");
				}
				else if (C.Charge > 0)
				{
					C.Gap++;
					C.Age++;
					C.LastRule = Fires[M][K] ? TEXT("trigger not started, gap") : TEXT("gap");
					if (C.Gap > FMath::Max(C.G, 0))
					{
						C.Clear();
						C.LastRule = TEXT("gap exceeded, cleared");
					}
				}
				if (C.Charge > 0 && C.Age > C.Life)
				{
					C.Clear();
					C.LastRule = TEXT("lifetime exceeded, cleared");
					if (bRestartOnExpiry && Held(C.X, E))
					{
						C.Charge = 1;
						C.Age = 1;
						C.LastRule = TEXT("lifetime exceeded, restarted");
					}
				}
			}
		}
		Prev = E;
		return Winner;
	}

	void Switch()
	{
		if (Prev >= 0) Prev = SwapLR(Prev);
	}

	FOracleSnapshot Save() const
	{
		FOracleSnapshot S;
		for (const FOracleMove& M : Moves)
		{
			for (const FOracleCommand& C : M.Commands)
			{
				S.Counters.Append({C.Charge, C.Age, C.Gap});
			}
		}
		S.Prev = Prev;
		S.Owed = BusyUntil - U;
		return S;
	}

	void Restore(const FOracleSnapshot& S)
	{
		int32 I = 0;
		for (FOracleMove& M : Moves)
		{
			for (FOracleCommand& C : M.Commands)
			{
				C.Charge = S.Counters[I++];
				C.Age = S.Counters[I++];
				C.Gap = S.Counters[I++];
				C.LastRule = TEXT("restored");
			}
		}
		Prev = S.Prev;
		BusyUntil = U + S.Owed;
	}

	FString Describe() const
	{
		FString Out = FString::Printf(TEXT("prev=%s u=%d busyUntil=%d"), Prev < 0 ? TEXT("none") : *InputName(Prev),
									  U, BusyUntil);
		for (int32 M = 0; M < Moves.Num(); ++M)
		{
			for (int32 K = 0; K < Moves[M].Commands.Num(); ++K)
			{
				Out += FString::Printf(TEXT("\n    move %d cmd %d: %s"), M, K, *Moves[M].Commands[K].Describe());
			}
		}
		return Out;
	}
};

struct FRelEntry
{
	int32 Key = 0;
	int32 Move = 0;
	bool operator==(const FRelEntry& O) const { return Key == O.Key && Move == O.Move; }
};

FString RelEntriesToString(const TArray<FRelEntry>& Entries)
{
	FString Out = TEXT("[");
	for (int32 I = 0; I < Entries.Num(); ++I)
	{
		Out += FString::Printf(TEXT("%s(%d,%d)"), I ? TEXT(" ") : TEXT(""), Entries[I].Key, Entries[I].Move);
	}
	return Out + TEXT("]");
}

struct FFuzzRun
{
	FAutomationTestBase& Test;
	FNSE004Battle& Battle;
	// [side][candidate]: candidate 0 discards a held frame that expires the charge (the instruction's reading),
	// candidate 1 restarts the charge on it and remains an accepted fuzz variant; the deterministic scenarios
	// pin the discard reading. A trace passes while one candidate explains it.
	FOraclePlayer Oracle[2][2];
	bool Alive[2][2] = {{true, true}, {true, true}};
	int32 Seed = 0, Fixture = 0, Episode = 0;
	const TCHAR* Replay = TEXT("base");
	int64 Steps = 0;
	int32 Mismatches = 0;
	TArray<FString> Recent;

	FFuzzRun(FAutomationTestBase& InTest, FNSE004Battle& InBattle) : Test(InTest), Battle(InBattle) {}

	void ResetEpisode()
	{
		Battle.ResetRound();
		for (int32 Side = 0; Side < 2; ++Side)
		{
			for (int32 Cand = 0; Cand < 2; ++Cand)
			{
				Oracle[Side][Cand].Reset();
				Alive[Side][Cand] = true;
			}
		}
		Recent.Empty();
	}

	void SwitchOracles()
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			Oracle[Side][0].Switch();
			Oracle[Side][1].Switch();
		}
	}

	// Executes a trace, checking both sides against their oracles after every step. Records Player's move
	// starts as (frame - FrameBase) and (unfrozen index) pairs, and which op indices are boundaries at which no
	// freeze is active. Returns false on the first mismatch, which is reported with the full context.
	bool Execute(const TArray<FOp>& Ops, TArray<FRelEntry>& FrameEntries, TArray<FRelEntry>& ClockEntries,
				 TArray<bool>* Eligible, TArray<FNSE004Entry>* Middle, TArray<FNSE004Entry>* ReplayedMiddle)
	{
		const int32 FrameBase = Battle.Game->BattleState.FrameNumber;
		const int32 ClockBase = Battle.Sides[0].ClockFrames;
		FrameEntries.Empty();
		ClockEntries.Empty();
		if (Eligible) Eligible->Init(false, Ops.Num());
		FRollbackData Snapshot;
		FOracleSnapshot OracleSnapshot[2][2];
		// Player's move starts between Save and Restore, and those replayed on the same frames after Restore.
		int32 SaveCount = INDEX_NONE, MiddleEndFrame = INDEX_NONE;
		int32 StepIndex = 0;
		for (int32 OpIndex = 0; OpIndex < Ops.Num(); ++OpIndex)
		{
			const FOp& Op = Ops[OpIndex];
			switch (Op.Kind)
			{
			case EOp::Hitstop:
				Battle.Hitstop(Op.A, Op.Side);
				continue;
			case EOp::SuperFreeze:
				Battle.SuperFreeze(Op.A);
				continue;
			case EOp::SwitchSides:
				Battle.SwitchSides();
				SwitchOracles();
				continue;
			case EOp::Save:
				Snapshot = Battle.Save();
				for (int32 Side = 0; Side < 2; ++Side)
				{
					OracleSnapshot[Side][0] = Oracle[Side][0].Save();
					OracleSnapshot[Side][1] = Oracle[Side][1].Save();
				}
				SaveCount = Battle.Sides[0].Entries.Num();
				continue;
			case EOp::Restore:
				MiddleEndFrame = Battle.Game->BattleState.FrameNumber;
				if (Middle && SaveCount != INDEX_NONE)
				{
					Middle->Empty();
					for (int32 I = SaveCount; I < Battle.Sides[0].Entries.Num(); ++I)
					{
						Middle->Add(Battle.Sides[0].Entries[I]);
					}
				}
				Battle.Restore(Snapshot);
				for (int32 Side = 0; Side < 2; ++Side)
				{
					Oracle[Side][0].Restore(OracleSnapshot[Side][0]);
					Oracle[Side][1].Restore(OracleSnapshot[Side][1]);
				}
				continue;
			case EOp::Step:
				break;
			default:
				continue;
			}
			if (Eligible && OpIndex > 0 && Ops[OpIndex - 1].Kind == EOp::Step && Battle.Player->Hitstop == 0 &&
				Battle.Game->BattleState.SuperFreezeDuration == 0)
			{
				(*Eligible)[OpIndex] = true;
			}
			++StepIndex;
			++Steps;
			const int32 FrameBefore = Battle.Game->BattleState.FrameNumber;
			const int32 Inputs[2] = {Op.A, Op.B};
			int32 Relative[2], ClockBefore[2], CountBefore[2];
			bool FacingLeft[2];
			for (int32 Side = 0; Side < 2; ++Side)
			{
				FacingLeft[Side] = Battle.Game->Players[Side]->Direction == DIR_Left;
				Relative[Side] = FacingLeft[Side] ? SwapLR(Inputs[Side]) : Inputs[Side];
				ClockBefore[Side] = Battle.Sides[Side].ClockFrames;
				CountBefore[Side] = Battle.Sides[Side].Entries.Num();
			}
			Battle.Step(Inputs[0], Inputs[1]);
			bool Frozen[2];
			int32 Expected[2][2];
			for (int32 Side = 0; Side < 2; ++Side)
			{
				Frozen[Side] = Battle.Sides[Side].ClockFrames == ClockBefore[Side];
				for (int32 Cand = 0; Cand < 2; ++Cand)
				{
					Expected[Side][Cand] = Frozen[Side] ? INDEX_NONE : Oracle[Side][Cand].Advance(Relative[Side]);
				}
			}
			Recent.Add(FString::Printf(TEXT("f%d P:%s%s%s D:%s%s"), FrameBefore + 1, *InputName(Inputs[0]),
									   FacingLeft[0] ? TEXT("<") : TEXT(">"), Frozen[0] ? TEXT("*") : TEXT(""),
									   *InputName(Inputs[1]), Frozen[1] ? TEXT("*") : TEXT("")));
			if (Recent.Num() > 24) Recent.RemoveAt(0);
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const TArray<FNSE004Entry>& Entries = Battle.Sides[Side].Entries;
				const int32 Added = Entries.Num() - CountBefore[Side];
				// A candidate stays alive while every step of the episode has matched it.
				bool WasAlive[2];
				for (int32 Cand = 0; Cand < 2; ++Cand)
				{
					const int32 E = Expected[Side][Cand];
					const bool bOk = E == INDEX_NONE ? Added == 0
													 : Added == 1 && Entries.Last().Frame == FrameBefore + 1 &&
														   Entries.Last().Move == E;
					WasAlive[Cand] = Alive[Side][Cand];
					if (!bOk) Alive[Side][Cand] = false;
				}
				if (!Alive[Side][0] && !Alive[Side][1])
				{
					++Mismatches;
					FString Actual = Added == 0 ? FString(TEXT("no move start"))
												: FString::Printf(TEXT("%d start(s), last (%d,%d)"), Added,
																  Entries.Last().Frame, Entries.Last().Move);
					FString Report;
					for (int32 Cand = 0; Cand < 2; ++Cand)
					{
						const int32 E = Expected[Side][Cand];
						const FString ExpectedText = E == INDEX_NONE
														 ? FString(TEXT("no move start"))
														 : FString::Printf(TEXT("(%d,%d)"), FrameBefore + 1, E);
						Report += FString::Printf(TEXT("\n  candidate %s (%s before this step) expected %s: %s"),
												  Cand == 0 ? TEXT("discard") : TEXT("restart"),
												  WasAlive[Cand] ? TEXT("alive") : TEXT("dead"), *ExpectedText,
												  *Oracle[Side][Cand].Describe());
					}
					Test.AddError(FString::Printf(
						TEXT("fuzz mismatch: seed=%d fixture=%d episode=%d replay=%s step=%d op=%d side=%d facing=%s "
							 "frozen=%d actual %s, no expiry convention explains the trace%s\n  recent inputs (P "
							 "physical, > faces right, < faces left, * frozen): %s"),
						Seed, Fixture, Episode, Replay, StepIndex, OpIndex, Side,
						FacingLeft[Side] ? TEXT("left") : TEXT("right"), Frozen[Side] ? 1 : 0, *Actual, *Report,
						*FString::Join(Recent, TEXT(" "))));
					return false;
				}
				if (Side == 0 && Added == 1)
				{
					FrameEntries.Add({Entries.Last().Frame - FrameBase, Entries.Last().Move});
					ClockEntries.Add({Battle.Sides[0].ClockFrames - ClockBase, Entries.Last().Move});
					if (ReplayedMiddle && MiddleEndFrame != INDEX_NONE && Entries.Last().Frame <= MiddleEndFrame)
					{
						ReplayedMiddle->Add(Entries.Last());
					}
				}
			}
		}
		return true;
	}
};

int32 PickFrom(FRandomStream& R, const TArray<int32>& Values)
{
	return Values[R.RandRange(0, Values.Num() - 1)];
}

FChargeCommand RandomCommand(FRandomStream& R, bool bAllowInvalid)
{
	const int32 X = PickFrom(R, {INP_Left, INP_Down, INP_DownLeft, INP_B});
	int32 Req = R.FRand() < 0.7f ? PickFrom(R, {1, 2, 3, 6}) : R.RandRange(1, 10);
	const int32 G = PickFrom(R, {-1, 0, 1, 2, 4});
	const int32 Life = PickFrom(R, {Req - 1, Req, Req + 1, Req + G + 1, Req + 2 * G + 3, 40});
	const bool bPress = R.RandRange(0, 1) == 1;
	int32 Y = bPress ? PickFrom(R, {INP_Right, INP_UpRight, INP_A, INP_Down}) : INP_None;
	if (bAllowInvalid && R.FRand() < 0.2f)
	{
		if (bPress && R.RandRange(0, 1) == 1) Y = INP_None;
		else Req = 0;
	}
	return Cmd(X, Req, G, Life, bPress ? EChargeTrigger::Press : EChargeTrigger::Release, Y);
}

TArray<FMoveSpec> FixedMoveSet()
{
	return {
		Move(4, {Cmd(INP_Left, 6, 2, 12)}),
		Move(2, {Press(INP_Down, INP_A, 6, 2, 20)}),
		Move(3, {Cmd(INP_Left, 4, 1, 10), Cmd(INP_Down, 4, 1, 10)}),
		Move(1, {Cmd(INP_Left, 3, 0, 6), Cmd(INP_Down, 3, 2, 12), Cmd(INP_DownLeft, 2, 1, 8),
				 Press(INP_B, INP_Right, 5, 4, 40)}),
	};
}

TArray<FMoveSpec> RandomMoveSet(FRandomStream& R)
{
	TArray<FMoveSpec> Set;
	for (int32 M = 0, Count = R.RandRange(1, 3); M < Count; ++M)
	{
		TArray<FChargeCommand> Commands;
		for (int32 K = 0, Num = R.RandRange(1, 4); K < Num; ++K)
		{
			Commands.Add(RandomCommand(R, true));
		}
		Set.Add(Move(PickFrom(R, {1, 2, 3, 6}), Commands));
	}
	return Set;
}

FString DescribeMoveSet(const TArray<FMoveSpec>& Set)
{
	FString Out;
	for (int32 M = 0; M < Set.Num(); ++M)
	{
		Out += FString::Printf(TEXT(" move%d(rec=%d"), M, Set[M].Recovery);
		for (const FChargeCommand& C : Set[M].Commands)
		{
			Out += FString::Printf(TEXT(" [%s R=%d G=%d L=%d %s%s]"), *InputName(C.ChargeInput), C.RequiredFrames,
								   C.MaxGapFrames, C.LifetimeFrames,
								   C.Trigger == EChargeTrigger::Press ? TEXT("press ") : TEXT("release"),
								   C.Trigger == EChargeTrigger::Press ? *InputName(C.TriggerInput) : TEXT(""));
		}
		Out += TEXT(")");
	}
	return Out;
}

// Generates one player's input segments, biased to the boundaries of the commands in the set.
struct FSegmentGenerator
{
	FRandomStream& R;
	const TArray<FMoveSpec>& Set;
	TMap<FString, int32>& Histogram;
	int32 Last = N;

	const FChargeCommand& AnyCommand()
	{
		const FMoveSpec& M = Set[R.RandRange(0, Set.Num() - 1)];
		return M.Commands[R.RandRange(0, M.Commands.Num() - 1)];
	}

	void Count(const TCHAR* Kind) { Histogram.FindOrAdd(Kind)++; }

	int32 Superset(int32 X)
	{
		if (R.FRand() >= 0.25f) return X;
		TArray<int32> Extras;
		if (X & (INP_Left | INP_Right)) Extras.Append({INP_Up, INP_Down});
		if (X & (INP_Up | INP_Down)) Extras.Append({INP_Left, INP_Right});
		if ((X & 0xF) == 0) Extras.Append({INP_Left, INP_Down, INP_DownLeft, INP_Up});
		Extras.Append({INP_A, INP_C});
		return X | PickFrom(R, Extras);
	}

	// Appends the physical inputs of one segment; returns how many steps were added.
	int32 Segment(TArray<int32>& Out)
	{
		const int32 Kind = R.RandRange(0, 99);
		const FChargeCommand& C = AnyCommand();
		const int32 X = C.ChargeInput == INP_None ? INP_Left : C.ChargeInput;
		const int32 Req = FMath::Max(C.RequiredFrames, 1);
		const int32 Before = Out.Num();
		if (Kind < 35)
		{
			Count(TEXT("hold"));
			const int32 Len = FMath::Max(PickFrom(R, {Req - 1, Req, Req + 1, R.RandRange(1, Req + 3)}), 1);
			const int32 In = Superset(X);
			for (int32 I = 0; I < Len; ++I) Out.Add(In);
		}
		else if (Kind < 55)
		{
			Count(TEXT("gap"));
			const int32 G = C.MaxGapFrames;
			const int32 Len = FMath::Max(PickFrom(R, {G, G + 1, R.RandRange(0, FMath::Max(G + 2, 0))}), 0);
			int32 In = PickFrom(R, {N, N, INP_C, INP_Right, INP_Up, INP_UpRight, INP_Down});
			if ((In & X) == X) In = N;
			for (int32 I = 0; I < Len; ++I) Out.Add(In);
		}
		else if (Kind < 75)
		{
			Count(TEXT("trigger"));
			if (C.Trigger == EChargeTrigger::Press)
			{
				const int32 Y = C.TriggerInput == INP_None ? INP_A : C.TriggerInput;
				Out.Add(PickFrom(R, {Y, Y, X | Y, Y | INP_Up, Y | INP_Down}));
			}
			else
			{
				// A release: neutral, or the charged input minus one of its bits when it has several.
				int32 In = N;
				if (R.RandRange(0, 2) == 0 && FMath::CountBits(X) > 1)
				{
					const int32 Bit = 1 << FMath::FloorLog2(X);
					In = X & ~Bit;
				}
				Out.Add(In);
			}
		}
		else
		{
			Count(TEXT("noise"));
			Out.Add(PickFrom(R, {N, INP_Left, INP_Right, INP_Down, INP_DownLeft, INP_DownRight, INP_Up, INP_UpLeft,
								 INP_UpRight, INP_A, INP_B, INP_C}));
		}
		if (Out.Num() > Before) Last = Out.Last();
		return Out.Num() - Before;
	}
};

// Builds one episode: Player segments with freezes, side switches and an optional save/restore; Dummy gets its
// own independent input stream of the same length.
TArray<FOp> GenerateEpisode(FRandomStream& R, const TArray<FMoveSpec>& Set, TMap<FString, int32>& Histogram,
							bool& bHasRestore, bool& bIdentical)
{
	const int32 Target = R.RandRange(60, 200);
	TArray<FOp> Ops;
	FSegmentGenerator Player{R, Set, Histogram};
	int32 StepCount = 0;
	while (StepCount < Target)
	{
		const int32 Kind = R.RandRange(0, 99);
		if (Kind < 10)
		{
			const bool bHitstop = R.RandRange(0, 1) == 0;
			const int32 Frames = bHitstop ? R.RandRange(2, 6) : R.RandRange(1, 4);
			Ops.Add({bHitstop ? EOp::Hitstop : EOp::SuperFreeze, Frames});
			Histogram.FindOrAdd(bHitstop ? TEXT("hitstop") : TEXT("superfreeze"))++;
			const int32 FrozenSteps = bHitstop ? Frames - 1 : Frames;
			const int32 Mode = R.RandRange(0, 2);
			for (int32 I = 0; I < FrozenSteps; ++I)
			{
				const int32 In = Mode == 0 ? Player.Last : (Mode == 1 || I == 0) ? N : Player.Last;
				Ops.Add({EOp::Step, In, N});
				++StepCount;
			}
			continue;
		}
		if (Kind < 15)
		{
			Ops.Add({EOp::SwitchSides});
			Histogram.FindOrAdd(TEXT("switch"))++;
			continue;
		}
		TArray<int32> Inputs;
		Player.Segment(Inputs);
		for (int32 In : Inputs)
		{
			Ops.Add({EOp::Step, In, N});
			++StepCount;
		}
	}
	bHasRestore = false;
	bIdentical = false;
	if (R.RandRange(0, 1) == 0)
	{
		TArray<int32> StepOps;
		for (int32 I = 0; I < Ops.Num(); ++I)
		{
			if (Ops[I].Kind == EOp::Step) StepOps.Add(I);
		}
		const int32 SaveAt = R.RandRange(0, StepOps.Num() - 4);
		const int32 RestoreAt = FMath::Min(SaveAt + R.RandRange(3, 20), StepOps.Num() - 1);
		const int32 SaveIndex = StepOps[SaveAt];
		const int32 RestoreIndex = StepOps[RestoreAt] + 1;
		TArray<FOp> Middle;
		for (int32 I = SaveIndex; I < RestoreIndex; ++I) Middle.Add(Ops[I]);
		bIdentical = R.RandRange(0, 1) == 0;
		TArray<FOp> Rebuilt;
		for (int32 I = 0; I < SaveIndex; ++I) Rebuilt.Add(Ops[I]);
		Rebuilt.Add({EOp::Save});
		Rebuilt.Append(Middle);
		Rebuilt.Add({EOp::Restore});
		if (bIdentical) Rebuilt.Append(Middle);
		for (int32 I = RestoreIndex; I < Ops.Num(); ++I) Rebuilt.Add(Ops[I]);
		Ops = MoveTemp(Rebuilt);
		Histogram.FindOrAdd(bIdentical ? TEXT("save-identical") : TEXT("save-regenerated"))++;
		bHasRestore = true;
	}
	// Dummy's stream, drawn from the same generator but independent of Player's.
	FSegmentGenerator Dummy{R, Set, Histogram};
	TArray<int32> DummyInputs;
	int32 Needed = 0;
	for (const FOp& Op : Ops)
	{
		if (Op.Kind == EOp::Step) ++Needed;
	}
	while (DummyInputs.Num() < Needed) Dummy.Segment(DummyInputs);
	int32 Next = 0;
	for (FOp& Op : Ops)
	{
		if (Op.Kind == EOp::Step) Op.B = DummyInputs[Next++];
	}
	return Ops;
}

TArray<FOp> MirrorOps(const TArray<FOp>& Ops)
{
	TArray<FOp> Out = Ops;
	for (FOp& Op : Out)
	{
		if (Op.Kind == EOp::Step)
		{
			Op.A = SwapLR(Op.A);
			Op.B = SwapLR(Op.B);
		}
	}
	return Out;
}

TArray<FOp> InsertFreeze(const TArray<FOp>& Ops, int32 At)
{
	TArray<FOp> Out;
	for (int32 I = 0; I < Ops.Num(); ++I)
	{
		if (I == At)
		{
			Out.Add({EOp::Hitstop, 4});
			for (int32 K = 0; K < 3; ++K) Out.Add(Ops[I - 1]);
		}
		Out.Add(Ops[I]);
	}
	return Out;
}

bool SameIdenticalReplay(const TArray<FNSE004Entry>& A, const TArray<FNSE004Entry>& B)
{
	if (A.Num() != B.Num()) return false;
	for (int32 I = 0; I < A.Num(); ++I)
	{
		if (A[I].Frame != B[I].Frame || A[I].Move != B[I].Move) return false;
	}
	return true;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE004V8, "UnrealBench.NSE004.V8",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE004V8::RunTest(const FString&)
{
	int32 Seed = 20260131;
	int32 EpisodesPerFixture = 30;
	FParse::Value(FCommandLine::Get(), TEXT("-FuzzSeed="), Seed);
	FParse::Value(FCommandLine::Get(), TEXT("-FuzzIterations="), EpisodesPerFixture);
	constexpr double WallClockBudget = 240.0;
	constexpr int64 StepBudget = 60000;
	const double Start = FPlatformTime::Seconds();
	FRandomStream R(Seed);
	TMap<FString, int32> Histogram;
	int64 TotalSteps = 0;
	int32 Episodes = 0, MirroredReplays = 0, FreezeReplays = 0, IdenticalReplays = 0;
	int32 ExpectedStarts[2] = {0, 0};
	bool bStopped = false, bBudgetHit = false;
	for (int32 Fixture = 0; Fixture < 4 && !bStopped; ++Fixture)
	{
		const TArray<FMoveSpec> Set = Fixture == 0 ? FixedMoveSet() : RandomMoveSet(R);
		AddInfo(FString::Printf(TEXT("fuzz fixture %d move set:%s"), Fixture, *DescribeMoveSet(Set)));
		FNSE004Battle Battle;
		FFuzzRun Run(*this, Battle);
		Run.Seed = Seed;
		Run.Fixture = Fixture;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			for (const FMoveSpec& M : Set)
			{
				Battle.AddMove(M.Recovery, M.Commands, M.Lists, Side);
				FOracleMove OracleMove;
				OracleMove.Recovery = M.Recovery;
				for (const FChargeCommand& C : M.Commands) OracleMove.Commands.Add(ToOracle(C));
				Run.Oracle[Side][0].Moves.Add(OracleMove);
				Run.Oracle[Side][1].Moves.Add(OracleMove);
			}
			Run.Oracle[Side][1].bRestartOnExpiry = true;
		}
		for (int32 Episode = 0; Episode < EpisodesPerFixture; ++Episode)
		{
			if (FPlatformTime::Seconds() - Start > WallClockBudget || TotalSteps + Run.Steps > StepBudget)
			{
				bBudgetHit = true;
				bStopped = true;
				break;
			}
			Run.Episode = Episode;
			bool bHasRestore = false, bIdentical = false;
			const TArray<FOp> Ops = GenerateEpisode(R, Set, Histogram, bHasRestore, bIdentical);
			TArray<FRelEntry> FrameEntries, ClockEntries;
			TArray<bool> Eligible;
			TArray<FNSE004Entry> Middle, ReplayedMiddle;
			Run.Replay = TEXT("base");
			Run.ResetEpisode();
			++Episodes;
			if (!Run.Execute(Ops, FrameEntries, ClockEntries, &Eligible, &Middle, &ReplayedMiddle))
			{
				bStopped = true;
				break;
			}
			if (bIdentical)
			{
				// The same steps replayed after the restore land on the same frames as the discarded ones.
				++IdenticalReplays;
				if (!SameIdenticalReplay(Middle, ReplayedMiddle))
				{
					AddError(FString::Printf(
						TEXT("R9 identical suffix after restore: seed=%d fixture=%d episode=%d original %s replay %s"),
						Seed, Fixture, Episode, *EntriesToString(Middle), *EntriesToString(ReplayedMiddle)));
					bStopped = true;
					break;
				}
			}
			if (Episode % 3 == 0)
			{
				// Mirror metamorphic: sides switched and Left/Right swapped, same (relative frame, move) pairs.
				Run.Replay = TEXT("mirror");
				Run.ResetEpisode();
				Battle.SwitchSides();
				Run.SwitchOracles();
				TArray<FRelEntry> MirrorFrames, MirrorClocks;
				++MirroredReplays;
				if (!Run.Execute(MirrorOps(Ops), MirrorFrames, MirrorClocks, nullptr, nullptr, nullptr))
				{
					bStopped = true;
					break;
				}
				if (MirrorFrames != FrameEntries)
				{
					AddError(FString::Printf(
						TEXT("R7 mirrored trace: seed=%d fixture=%d episode=%d plain %s mirrored %s"), Seed, Fixture,
						Episode, *RelEntriesToString(FrameEntries), *RelEntriesToString(MirrorFrames)));
					bStopped = true;
					break;
				}
			}
			if (Episode % 4 == 0 && !bHasRestore)
			{
				// Freeze metamorphic: three hitstop frames inserted at a quiet boundary shift nothing in
				// Player's (unfrozen index, move) pairs.
				TArray<int32> Boundaries;
				for (int32 I = 0; I < Eligible.Num(); ++I)
				{
					if (Eligible[I]) Boundaries.Add(I);
				}
				if (Boundaries.Num() > 0)
				{
					const int32 At = Boundaries[R.RandRange(0, Boundaries.Num() - 1)];
					Run.Replay = TEXT("freeze-inserted");
					Run.ResetEpisode();
					TArray<FRelEntry> InsertFrames, InsertClocks;
					++FreezeReplays;
					if (!Run.Execute(InsertFreeze(Ops, At), InsertFrames, InsertClocks, nullptr, nullptr, nullptr))
					{
						bStopped = true;
						break;
					}
					if (InsertClocks != ClockEntries)
					{
						AddError(FString::Printf(TEXT("R5 freeze inserted at op %d: seed=%d fixture=%d episode=%d "
													  "plain %s inserted %s"),
												 At, Seed, Fixture, Episode, *RelEntriesToString(ClockEntries),
												 *RelEntriesToString(InsertClocks)));
						bStopped = true;
						break;
					}
				}
			}
		}
		TotalSteps += Run.Steps;
		ExpectedStarts[0] += Run.Oracle[0][0].ExpectedStarts;
		ExpectedStarts[1] += Run.Oracle[1][0].ExpectedStarts;
	}
	FString HistogramText;
	for (const auto& Pair : Histogram)
	{
		HistogramText += FString::Printf(TEXT(" %s=%d"), *Pair.Key, Pair.Value);
	}
	AddInfo(FString::Printf(TEXT("fuzz seed=%d episodes=%d steps=%lld seconds=%.1f mirrored=%d freeze=%d identical=%d "
								 "expected starts P=%d D=%d budget_hit=%d segments:%s"),
							Seed, Episodes, TotalSteps, FPlatformTime::Seconds() - Start, MirroredReplays,
							FreezeReplays, IdenticalReplays, ExpectedStarts[0], ExpectedStarts[1], bBudgetHit ? 1 : 0,
							*HistogramText));
	TestTrue(TEXT("fuzz: the oracle expected at least one move start on each side"),
			 ExpectedStarts[0] > 0 && ExpectedStarts[1] > 0);
	TestTrue(TEXT("fuzz: completed within the wall-clock budget"), FPlatformTime::Seconds() - Start < 300.0);
	return !HasAnyErrors();
}
