// Hidden tests for ue_task_0134: bounded exhaustive combo discovery and playable trials.
//
// Every expected route comes from a brute-force enumerator written in this file that never calls the
// feature: it probes real presses from prefix snapshots of the live battle and applies the published
// rules to what the engine reports. Trial verdicts come from a test-side tape player and judge.
// The domain includes juggles (air hit actions, launch arcs, untech and teching) and multi-hit moves:
// continuation is judged with the engine's stun-capability facts (CheckIsStunned is the cannot-act
// query, and an airborne defender whose untech expired can act by teching), a step's HitFrame is its
// first contact and its damage the sum of its contacts grouped until the next step's begin frame, and
// the tech lens replays tapes with the tech input held so exactly the could-act hits turn into
// escapes. -FuzzSeed=<int> and -FuzzIterations=<int> replay the seeded tests.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Math/RandomStream.h"
#include "HAL/PlatformTime.h"
#include "NightSkyEngine/Fixtures/NSE025Fixture.h"
#include "NightSkyEngine/Battle/Lab/ComboDiscovery.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"

namespace NSE025Test
{
// Reviewer allowances (verification.md), not published limits: the exactness budget sits far above any exhaustive
// design's cost, and the tape player's post-tape window only has to outlast the fixture's longest recovery.
constexpr int32 FullBudget = 20000000;
constexpr int32 SettleWindow = 300;
// Fighters stop 85000 inside the stage bounds at +-3200000.
constexpr int32 CornerX = 3115000;
constexpr double TestDeadlineSeconds = 240.0;

struct FCounters
{
	int64 Frames = 0;
	int64 Restores = 0;
};
static FCounters GCounters;

void StepBattle(FNSE025Battle& B, int32 In1, int32 In2)
{
	B.Step(In1, In2);
	++GCounters.Frames;
}

// SaveGameState appends into its argument, so every snapshot is a fresh object.
FRollbackData Snapshot(FNSE025Battle& B)
{
	FRollbackData Data;
	int32 Checksum = 0;
	B.Game->SaveGameState(Data, &Checksum);
	return Data;
}

void Restore(FNSE025Battle& B, FRollbackData& Data)
{
	B.Game->LoadGameState(Data);
	++GCounters.Restores;
}

// Reference move table (verification.md). Pushback is 0 so follow-ups connect with margin.
FNSE025MoveSpec MakeMove(int32 Input, int32 Startup, int32 Active, int32 Recovery, int32 Reach, int32 Damage,
						 int32 Hitstun, int32 Hitstop, int32 Forced, int32 MaxChain, const TArray<int32>& Chains)
{
	FNSE025MoveSpec S;
	S.Input = Input;
	S.Startup = Startup;
	S.Active = Active;
	S.Recovery = Recovery;
	S.Reach = Reach;
	S.Damage = Damage;
	S.Hitstun = Hitstun;
	S.Hitstop = Hitstop;
	S.Pushback = 0;
	S.InitialProration = 100;
	S.ForcedProration = Forced;
	S.MinimumDamagePercent = 0;
	S.MaxChain = MaxChain;
	S.bReverseBeat = true;
	S.bFollowup = false;
	S.Type = EStateType::NormalAttack;
	S.ChainCancels = Chains;
	S.CancelFrom = 0;
	return S;
}
FNSE025MoveSpec Jab() { return MakeMove(INP_A, 4, 2, 6, 160000, 300, 12, 4, 90, 1, {1, 2}); }
FNSE025MoveSpec Strong() { return MakeMove(INP_B, 8, 3, 12, 220000, 700, 18, 6, 80, 1, {2}); }
FNSE025MoveSpec Special() { return MakeMove(INP_C, 12, 3, 20, 300000, 1000, 24, 8, 70, 1, {}); }
FNSE025MoveSpec Poke() { return MakeMove(INP_D, 3, 2, 5, 120000, 200, 10, 3, 100, -1, {3}); }
FNSE025MoveSpec JabCopy() { auto S = Jab(); S.Input = INP_E; S.ChainCancels = {}; return S; }
FNSE025MoveSpec Slow() { return MakeMove(INP_F, 12, 2, 6, 160000, 320, 12, 4, 100, 1, {}); }
// Never begins from its button and could not reach anyway.
FNSE025MoveSpec Dummy(int32 Input)
{
	auto S = MakeMove(Input, 4, 2, 6, 1000, 100, 8, 2, 100, 1, {});
	S.bFollowup = true;
	return S;
}
// Launcher: launches on hit; the defender's hold is the authored untech.
FNSE025MoveSpec Launcher()
{
	auto S = MakeMove(INP_E, 6, 3, 14, 200000, 500, 18, 5, 80, 1, {0, 1});
	S.AirHitAction = HACT_AirNormal;
	S.AirPushbackY = 9000;
	S.Gravity = 800;
	S.Untech = 40;
	return S;
}
// Floater: a shallow, slow launch whose untech expires while the defender is still in the air.
FNSE025MoveSpec Floater()
{
	auto S = MakeMove(INP_F, 8, 4, 18, 180000, 400, 12, 4, 85, 1, {0});
	S.AirHitAction = HACT_AirNormal;
	S.AirPushbackY = 8000;
	S.Gravity = 300;
	S.Untech = 24;
	return S;
}
// MultiJab: re-arms its hitbox on action frame 8 and lands two contacts in one step.
FNSE025MoveSpec MultiJab()
{
	auto S = MakeMove(INP_G, 4, 8, 8, 160000, 150, 14, 3, 95, 1, {0});
	S.MultiHit = 8;
	return S;
}
constexpr int32 Buttons[FNSE025Battle::MaxMoves] = {INP_A, INP_B, INP_C, INP_D, INP_E, INP_F, INP_G, INP_H};

FString InputName(int32 Input)
{
	switch (Input)
	{
	case INP_A: return TEXT("A");
	case INP_B: return TEXT("B");
	case INP_C: return TEXT("C");
	case INP_D: return TEXT("D");
	case INP_E: return TEXT("E");
	case INP_F: return TEXT("F");
	case INP_G: return TEXT("G");
	case INP_H: return TEXT("H");
	case INP_Neutral: return TEXT(".");
	default: return FString::Printf(TEXT("<%d>"), Input);
	}
}

FString TapeStr(const TArray<int32>& Inputs)
{
	FString S;
	for (int32 I : Inputs)
	{
		S += InputName(I);
	}
	return S;
}

FComboSearchRequest MakeRequest(const FNSE025Battle& B, const TArray<int32>& Slots, int32 MaxSteps, int32 MaxFrames,
								int32 MaxResults, int32 Budget = FullBudget, int32 Opp = INP_Neutral)
{
	FComboSearchRequest Req;
	for (int32 Slot : Slots)
	{
		FComboMove M;
		M.State = FNSE025Battle::MoveTag(Slot);
		M.Input = B.Moves[Slot].Input;
		Req.Moves.Add(M);
	}
	Req.MaxSteps = MaxSteps;
	Req.MaxFrames = MaxFrames;
	Req.MaxResults = MaxResults;
	Req.MaxSimulatedFrames = Budget;
	Req.OpponentInput = Opp;
	return Req;
}

FString RequestStr(const FComboSearchRequest& Req)
{
	FString Moves;
	for (const FComboMove& M : Req.Moves)
	{
		Moves += FString::Printf(TEXT("%s/%s "), *M.State.ToString(), *InputName(M.Input));
	}
	return FString::Printf(TEXT("moves=[%s] steps=%d frames=%d results=%d budget=%d opp=%d"), *Moves, Req.MaxSteps,
						   Req.MaxFrames, Req.MaxResults, Req.MaxSimulatedFrames, Req.OpponentInput);
}

// What the test reads at the end of a frame.
struct FFrameObs
{
	bool bIdle = false;
	bool bHasHit = false;
	FGameplayTag AttState;
	int32 AttActionTime = 0;
	int32 AttX = 0;
	int32 Health = 0;
	bool bStunned = false;
	EStateType DefType = EStateType::Standing;
	int32 DefX = 0;
	int32 DefY = 0;
	int32 DefStunTime = 0;
	int32 DefHitstop = 0;
	bool bDowned = false;
};

FFrameObs Observe(FNSE025Battle& B)
{
	FFrameObs O;
	O.bIdle = B.Attacker->GetStateType() == EStateType::Standing;
	O.bHasHit = (B.Attacker->AttackFlags & ATK_HasHit) != 0;
	O.AttState = B.Attacker->PrimaryStateMachine.CurrentState->Name;
	O.AttActionTime = B.Attacker->ActionTime;
	O.AttX = B.Attacker->PosX;
	O.Health = B.Defender->CurrentHealth;
	O.bStunned = B.Defender->CheckIsStunned();
	O.DefType = B.Defender->GetStateType();
	O.DefX = B.Defender->PosX;
	O.DefY = B.Defender->PosY;
	O.DefStunTime = static_cast<int32>(B.Defender->StunTime);
	O.DefHitstop = B.Defender->Hitstop;
	O.bDowned = (B.Defender->PlayerFlags & PLF_IsKnockedDown) != 0;
	return O;
}

// The engine's stun-capability facts: CheckIsStunned is the cannot-act query, an airborne defender
// whose untech (StunTime) expired can act by teching even though it is still stunned, and a knocked-down
// defender can act on neither path.
bool CouldAct(const FFrameObs& O)
{
	return !O.bStunned || (O.DefY > 0 && O.DefStunTime <= 0 && !O.bDowned);
}

// Whether the defender can act during the frame that follows O: unless frozen, that frame's update
// counts the untech down, so a defender at untech one or less regains control or can tech on it unless
// it is knocked down.
bool CouldActDuringNext(const FFrameObs& O)
{
	return !O.bStunned || (O.DefHitstop < 1 && O.DefStunTime <= 1 && !O.bDowned);
}

// Tape player. Steps the tape, then neutral for up to ExtraFrames more frames; with bStopWhenSettled the extra
// frames end once the attacker is idle and the defender can act. Records every contact the attacker makes.
struct FContact
{
	int32 Frame = 0;
	bool bHit = false;
	int32 Damage = 0;
	FGameplayTag State;
	bool bEarlierHit = false;
	bool bContinues = false;
	bool bInProgress = false;
};
struct FEntry
{
	int32 Frame = 0;
	FGameplayTag State;
};
struct FSimResult
{
	TArray<FFrameObs> Frames;
	TArray<FContact> Contacts;
	TArray<FEntry> Entries;
	TArray<FEntry> DefEntries;
	int32 HealthLost = 0;
	int32 EndFrame = 0;
	bool bSettled = false;

	const FContact* HitAt(int32 Frame) const
	{
		for (const FContact& C : Contacts)
		{
			if (C.Frame == Frame && C.bHit) return &C;
		}
		return nullptr;
	}
	bool BeganAt(int32 Frame, FGameplayTag State) const
	{
		for (const FEntry& E : Entries)
		{
			if (E.Frame == Frame && E.State == State) return true;
		}
		return false;
	}
	int32 HitCount() const
	{
		int32 N = 0;
		for (const FContact& C : Contacts) N += C.bHit ? 1 : 0;
		return N;
	}
	FString ContactsStr() const
	{
		FString S;
		for (const FContact& C : Contacts)
		{
			S += FString::Printf(TEXT("%s@%d:%s:%d%s%s "), C.bHit ? TEXT("hit") : TEXT("block"), C.Frame,
								 *C.State.ToString(), C.Damage, C.bContinues ? TEXT(":cont") : TEXT(""),
								 C.bInProgress ? TEXT(":inprog") : TEXT(""));
		}
		return S;
	}
};

FSimResult Simulate(FNSE025Battle& B, const TArray<int32>& Inputs, int32 Opp, int32 ExtraFrames, bool bStopWhenSettled,
					bool bAnchored)
{
	FSimResult R;
	FFrameObs Prev = Observe(B);
	FGameplayTag PrevDefState = B.Defender->PrimaryStateMachine.CurrentState->Name;
	int32 H = bAnchored ? 0 : -1;
	bool bHeld = !(bAnchored && CouldAct(Prev));
	bool bInProgress = !Prev.bIdle;
	const int32 Last = Inputs.Num() + ExtraFrames;
	for (int32 F = 1; F <= Last; ++F)
	{
		const bool bTape = F <= Inputs.Num();
		StepBattle(B, bTape ? Inputs[F - 1] : INP_Neutral, Opp);
		const FFrameObs O = Observe(B);
		const FGameplayTag DefState = B.Defender->PrimaryStateMachine.CurrentState->Name;
		if (DefState != PrevDefState)
		{
			FEntry E;
			E.Frame = F;
			E.State = DefState;
			R.DefEntries.Add(E);
			PrevDefState = DefState;
		}
		if (O.AttState != Prev.AttState || O.AttActionTime < Prev.AttActionTime)
		{
			bInProgress = false;
			if (!O.bIdle)
			{
				FEntry E;
				E.Frame = F;
				E.State = O.AttState;
				R.Entries.Add(E);
			}
		}
		const bool bHit = O.Health < Prev.Health;
		if (bHit || (O.bHasHit && !Prev.bHasHit))
		{
			FContact C;
			C.Frame = F;
			C.bHit = bHit;
			C.Damage = Prev.Health - O.Health;
			C.State = O.AttState;
			C.bEarlierHit = H >= 0;
			C.bInProgress = bInProgress;
			C.bContinues = bHit && (H < 0 || (bHeld && !CouldActDuringNext(Prev)));
			if (bHit)
			{
				H = F;
				bHeld = true;
				R.HealthLost += C.Damage;
			}
			R.Contacts.Add(C);
		}
		else if (CouldAct(O))
		{
			bHeld = false;
		}
		R.Frames.Add(O);
		R.EndFrame = F;
		Prev = O;
		if (!bTape && bStopWhenSettled && O.bIdle && !O.bStunned)
		{
			R.bSettled = true;
			break;
		}
	}
	return R;
}

FSimResult Replay(FNSE025Battle& B, const TArray<int32>& Inputs, int32 Opp, bool bAnchored)
{
	return Simulate(B, Inputs, Opp, SettleWindow, true, bAnchored);
}

// Independent verdict for a trial from the tape player's contacts.
struct FVerdict
{
	bool bCompleted = false;
	int32 FailedStep = -1;
	// Health lost through the failing contact, and before it (equal when there is no failing contact).
	int32 ObservedDamage = 0;
	int32 DamageBefore = 0;
	FString ToString() const
	{
		return FString::Printf(TEXT("completed=%d failed=%d damage=%d (before the failing contact %d)"), bCompleted ? 1 : 0,
							   FailedStep, ObservedDamage, DamageBefore);
	}
};

// Independent verdict for a trial from the tape player's contacts. Contacts group into steps by the
// tape's presses: a step's contacts are its move's contacts until the next press, the first of them is
// the step's HitFrame, and the group must remove the step's damage.
FVerdict Judge(const FSimResult& R, const FComboTrial& T)
{
	FVerdict V;
	TArray<int32> Presses;
	for (int32 F = 1; F <= T.Inputs.Num(); ++F)
	{
		if (T.Inputs[F - 1] != INP_Neutral) Presses.Add(F);
	}
	int32 k = 0;
	int32 GroupDamage = 0;
	int32 GroupHits = 0;
		for (const FContact& C : R.Contacts)
	{
		// The contact's health counts before the checks: a failure it reveals runs through it, and the
		// tolerance pairs that with the amount before it.
		V.DamageBefore = V.ObservedDamage;
		if (C.bHit) V.ObservedDamage += C.Damage;
		// Close the steps whose window ends before this contact.
		while (k < T.Steps.Num() && Presses.IsValidIndex(k + 1) && C.Frame >= Presses[k + 1])
		{
			if (GroupHits == 0 || GroupDamage != T.Steps[k].Damage)
			{
				V.FailedStep = k;
				return V;
			}
			++k;
			GroupDamage = 0;
			GroupHits = 0;
		}
		if (C.bInProgress) continue;
		if (k >= T.Steps.Num())
		{
			V.FailedStep = T.Steps.Num();
			return V;
		}
		if (!Presses.IsValidIndex(k) || !C.bHit || C.State != T.Steps[k].State || !(C.bContinues || !C.bEarlierHit))
		{
			V.FailedStep = k;
			return V;
		}
		++GroupHits;
		GroupDamage += C.Damage;
	}
	V.DamageBefore = V.ObservedDamage;
	for (; k < T.Steps.Num(); ++k)
	{
		if (!Presses.IsValidIndex(k) || GroupHits == 0 || GroupDamage != T.Steps[k].Damage)
		{
			V.FailedStep = k;
			V.DamageBefore = V.ObservedDamage;
			return V;
		}
		GroupDamage = 0;
		GroupHits = 0;
	}
	V.bCompleted = true;
	return V;
}

FVerdict ReportVerdict(const FComboTrialReport& Rep)
{
	FVerdict V;
	V.bCompleted = Rep.bCompleted;
	V.FailedStep = Rep.FailedStep;
	V.ObservedDamage = Rep.ObservedDamage;
	return V;
}

// The report must agree with the judge on the verdict and the failing step; on failure the health report may stop
// before or after the failing contact (reviewer slack, verification.md), but never run on past it.
bool ReportMatches(const FVerdict& Report, const FVerdict& J)
{
	if (Report.bCompleted != J.bCompleted || Report.FailedStep != J.FailedStep) return false;
	if (J.bCompleted) return Report.ObservedDamage == J.ObservedDamage;
	return Report.ObservedDamage == J.ObservedDamage || Report.ObservedDamage == J.DamageBefore;
}

void ExpectDamageEither(FAutomationTestBase& Test, const FString& Label, int32 Actual, int32 A, int32 B)
{
	Test.TestTrue(FString::Printf(TEXT("%s: ObservedDamage %d is %d or %d"), *Label, Actual, A, B), Actual == A || Actual == B);
}

// Routes as the enumerator records them.
struct FStepRec
{
	int32 MoveIndex = 0;
	bool bCancel = false;
	int32 Begin = 0;
	int32 Hit = 0;
	int32 Damage = 0;
};
struct FRouteRec
{
	TArray<FStepRec> Steps;
	int32 Total = 0;
	TArray<int32> Inputs;
	int32 LastHit() const { return Steps.Num() ? Steps.Last().Hit : 0; }
};
struct FShape
{
	int32 MoveIndex;
	bool bCancel;
};

bool RouteLess(const FRouteRec& A, const FRouteRec& B)
{
	if (A.Total != B.Total) return A.Total > B.Total;
	if (A.Steps.Num() != B.Steps.Num()) return A.Steps.Num() < B.Steps.Num();
	if (A.LastHit() != B.LastHit()) return A.LastHit() < B.LastHit();
	for (int32 I = 0; I < A.Steps.Num(); ++I)
	{
		if (A.Steps[I].MoveIndex != B.Steps[I].MoveIndex) return A.Steps[I].MoveIndex < B.Steps[I].MoveIndex;
		if (A.Steps[I].bCancel != B.Steps[I].bCancel) return A.Steps[I].bCancel;
	}
	return false;
}

FString KindStr(bool bCancel) { return bCancel ? TEXT("cancel") : TEXT("link"); }

FString RouteStr(const FRouteRec& R)
{
	FString S = TEXT("[");
	for (const FStepRec& St : R.Steps)
	{
		S += FString::Printf(TEXT("m%d %s b%d h%d d%d; "), St.MoveIndex, *KindStr(St.bCancel), St.Begin, St.Hit, St.Damage);
	}
	return S + FString::Printf(TEXT("] total=%d tape=%s"), R.Total, *TapeStr(R.Inputs));
}

FString TrialStr(const FComboTrial& T)
{
	FString S = TEXT("[");
	for (const FComboStep& St : T.Steps)
	{
		S += FString::Printf(TEXT("m%d(%s) %s b%d h%d d%d; "), St.MoveIndex, *St.State.ToString(),
							 *KindStr(St.Kind == EComboStepKind::Cancel), St.BeginFrame, St.HitFrame, St.Damage);
	}
	return S + FString::Printf(TEXT("] total=%d tape=%s"), T.TotalDamage, *TapeStr(T.Inputs));
}

FString ListStr(const TArray<FRouteRec>& Routes)
{
	FString S;
	for (int32 I = 0; I < Routes.Num(); ++I)
	{
		S += FString::Printf(TEXT("#%d %s | "), I, *RouteStr(Routes[I]));
	}
	return S;
}

FString TrialListStr(const TArray<FComboTrial>& Trials)
{
	FString S;
	for (int32 I = 0; I < Trials.Num(); ++I)
	{
		S += FString::Printf(TEXT("#%d %s | "), I, *TrialStr(Trials[I]));
	}
	return S;
}

bool ShapeMatches(const FRouteRec& R, const TArray<FShape>& Shape)
{
	if (R.Steps.Num() != Shape.Num()) return false;
	for (int32 I = 0; I < Shape.Num(); ++I)
	{
		if (R.Steps[I].MoveIndex != Shape[I].MoveIndex || R.Steps[I].bCancel != Shape[I].bCancel) return false;
	}
	return true;
}

bool ShapeMatches(const FComboTrial& T, const TArray<FShape>& Shape)
{
	if (T.Steps.Num() != Shape.Num()) return false;
	for (int32 I = 0; I < Shape.Num(); ++I)
	{
		if (T.Steps[I].MoveIndex != Shape[I].MoveIndex
			|| (T.Steps[I].Kind == EComboStepKind::Cancel) != Shape[I].bCancel) return false;
	}
	return true;
}

int32 IndexOfShape(const TArray<FRouteRec>& Routes, const TArray<FShape>& Shape)
{
	for (int32 I = 0; I < Routes.Num(); ++I)
	{
		if (ShapeMatches(Routes[I], Shape)) return I;
	}
	return -1;
}

int32 IndexOfShape(const TArray<FComboTrial>& Trials, const TArray<FShape>& Shape)
{
	for (int32 I = 0; I < Trials.Num(); ++I)
	{
		if (ShapeMatches(Trials[I], Shape)) return I;
	}
	return -1;
}

bool TrialMatches(const FComboTrial& T, const FRouteRec& R, const FComboSearchRequest& Req)
{
	if (T.Steps.Num() != R.Steps.Num() || T.TotalDamage != R.Total || T.Inputs != R.Inputs) return false;
	for (int32 I = 0; I < R.Steps.Num(); ++I)
	{
		const FComboStep& S = T.Steps[I];
		const FStepRec& E = R.Steps[I];
		if (!Req.Moves.IsValidIndex(E.MoveIndex)) return false;
		if (S.State != Req.Moves[E.MoveIndex].State || S.MoveIndex != E.MoveIndex
			|| (S.Kind == EComboStepKind::Cancel) != E.bCancel || S.BeginFrame != E.Begin || S.HitFrame != E.Hit
			|| S.Damage != E.Damage) return false;
	}
	return true;
}

int32 IndexOfTrial(const TArray<FRouteRec>& Routes, const FComboTrial& T, const FComboSearchRequest& Req)
{
	for (int32 I = 0; I < Routes.Num(); ++I)
	{
		if (TrialMatches(T, Routes[I], Req)) return I;
	}
	return -1;
}

FComboTrial ToTrial(const FRouteRec& R, const FComboSearchRequest& Req)
{
	FComboTrial T;
	for (const FStepRec& E : R.Steps)
	{
		FComboStep S;
		S.State = Req.Moves[E.MoveIndex].State;
		S.MoveIndex = E.MoveIndex;
		S.Kind = E.bCancel ? EComboStepKind::Cancel : EComboStepKind::Link;
		S.BeginFrame = E.Begin;
		S.HitFrame = E.Hit;
		S.Damage = E.Damage;
		T.Steps.Add(S);
	}
	T.TotalDamage = R.Total;
	T.Inputs = R.Inputs;
	return T;
}

struct FEnumStats
{
	int64 Frames = 0;
	int32 Restores = 0;
	int32 Escapes = 0;
	int32 Blocks = 0;
	int32 Whiffs = 0;
};

// One contact of a step's group: the frame it registered and the health it removed.
struct FGroupContact
{
	int32 Frame = 0;
	int32 Damage = 0;
};

// Brute-force enumerator over (move, kind) sequences from the battle's current situation. It never calls the
// feature. bAnchored says whether an unblocked hit holds the defender at the situation. A step's contacts
// group until the next step's begin frame, so a cancel into the next move truncates the group.
struct FEnumerator
{
	FNSE025Battle& B;
	const FComboSearchRequest& Req;
	bool bAnchored;
	TArray<FRouteRec> Routes;
	int64 Frames = 0;
	int32 Restores = 0;
	int32 Escapes = 0;
	int32 Blocks = 0;
	int32 Whiffs = 0;

	FEnumerator(FNSE025Battle& InB, const FComboSearchRequest& InReq, bool bInAnchored)
		: B(InB), Req(InReq), bAnchored(bInAnchored)
	{
	}

	void Step(int32 In1)
	{
		StepBattle(B, In1, Req.OpponentInput);
		++Frames;
	}

	void Load(FRollbackData& Data)
	{
		Restore(B, Data);
		++Restores;
	}

	TArray<FRouteRec> Run()
	{
		Routes.Reset();
		FRollbackData S0 = Snapshot(B);
		if (Req.MaxSteps >= 1 && Req.MaxFrames >= 1)
		{
			Explore(S0, 0, bAnchored ? 0 : -1, FRouteRec());
		}
		Load(S0);
		Routes.Sort(RouteLess);
		return Routes;
	}

	// Whether the hit registering on frame X continues the combo from the most recent earlier hit at
	// LastEarlier, judged with the engine's stun-capability facts over Obs (Obs[I] is the end of frame
	// Fk + I, so Obs[0] is the situation). A step with no earlier hit is exempt.
	bool Continues(const TArray<FFrameObs>& Obs, int32 Fk, int32 LastEarlier, int32 X)
	{
		if (LastEarlier < 0) return true;
		if (LastEarlier <= Fk && CouldAct(Obs[0])) return false;
		for (int32 X2 = FMath::Max(LastEarlier + 1, Fk + 1); X2 < X; ++X2)
		{
			if (CouldAct(Obs[X2 - Fk])) return false;
		}
		return !CouldActDuringNext(Obs[X - 1 - Fk]);
	}

	enum class EStepOutcome
	{
		NotBegan,
		Began,
	};

	// Presses Moves[MoveI] on frame F, replayed from the snapshot at the end of frame SitFrame. On a
	// begin, follows the move to its end, records the route with the full contact group, and explores
	// cancel continuations from the first contact (each child truncates this step's group to the
	// contacts before its begin frame) and link continuations once the move has ended.
	EStepOutcome RunStep(FRollbackData& Sit, int32 SitFrame, int32 LastEarlier, int32 MoveI, bool bCancel,
						 int32 F, const FRouteRec& Prefix, int32 SitDeadAt = 0)
	{
		Load(Sit);
		TArray<FFrameObs> Obs;
		Obs.Add(Observe(B));
		// Earlier hits are exempt anchors: they update the most recent earlier hit but are never
		// themselves judged. A contact at or past SitDeadAt is the parent step's broken group, which
		// no continuation of this branch may include.
		for (int32 G = SitFrame + 1; G < F; ++G)
		{
			Step(INP_Neutral);
			const FFrameObs O = Observe(B);
			if (O.Health < Obs.Last().Health)
			{
				if (SitDeadAt > 0 && G >= SitDeadAt) return EStepOutcome::Began;
				LastEarlier = G;
			}
			Obs.Add(O);
		}
		Step(Req.Moves[MoveI].Input);
		Obs.Add(Observe(B));
		if (!(Obs.Last().AttState == Req.Moves[MoveI].State && Obs.Last().AttActionTime == 1))
		{
			return EStepOutcome::NotBegan;
		}
		int32 FirstContact = -1;
		int32 LeaveFrame = -1;
		int32 DeadAt = 0;
		FRollbackData S1;
		TArray<FGroupContact> Group;
		for (int32 G = F + 1; G <= Req.MaxFrames; ++G)
		{
			Step(INP_Neutral);
			const FFrameObs O = Observe(B);
			if (O.AttState != Req.Moves[MoveI].State)
			{
				LeaveFrame = G;
				Obs.Add(O);
				break;
			}
			if (O.Health < Obs.Last().Health)
			{
				if (!Continues(Obs, SitFrame, LastEarlier, G))
				{
					++Escapes;
					if (FirstContact < 0) return EStepOutcome::Began;
					DeadAt = G;
				}
				else
				{
					if (FirstContact < 0)
					{
						FirstContact = G;
						S1 = Snapshot(B);
					}
					FGroupContact Gc;
					Gc.Frame = G;
					Gc.Damage = Obs.Last().Health - O.Health;
					Group.Add(Gc);
					LastEarlier = G;
				}
			}
			else if (O.bHasHit && !Obs.Last().bHasHit)
			{
				++Blocks;
				if (FirstContact < 0) return EStepOutcome::Began;
				DeadAt = G;
			}
			Obs.Add(O);
			if (DeadAt > 0) break;
		}
		if (FirstContact < 0)
		{
			++Whiffs;
			return EStepOutcome::Began;
		}
		int32 FullDamage = 0;
		for (const FGroupContact& Gc : Group) FullDamage += Gc.Damage;
		FRouteRec Route = Prefix;
		FStepRec St;
		St.MoveIndex = MoveI;
		St.bCancel = bCancel;
		St.Begin = F;
		St.Hit = FirstContact;
		St.Damage = FullDamage;
		Route.Steps.Add(St);
		Route.Total += St.Damage;
		while (Route.Inputs.Num() < FirstContact) Route.Inputs.Add(INP_Neutral);
		Route.Inputs[F - 1] = Req.Moves[MoveI].Input;
		if (DeadAt == 0)
		{
			Routes.Add(Route);
		}
		if (Route.Steps.Num() < Req.MaxSteps)
		{
			// Cancels open after the first contact and run until the move ends; links begin once it has.
			// Each move and kind takes its earliest start, so probing a kind stops at its first begin.
			const int32 Recovered = LeaveFrame > 0 ? LeaveFrame : Req.MaxFrames + 1;
			for (int32 J = 0; J < Req.Moves.Num(); ++J)
			{
				for (int32 C = FirstContact + 1; C <= FMath::Min(Recovered - 1, Req.MaxFrames - 1); ++C)
				{
					if (RunChild(S1, FirstContact, C, J, true, Group, Route, DeadAt) != EStepOutcome::NotBegan) break;
				}
				for (int32 C = Recovered; C <= Req.MaxFrames - 1; ++C)
				{
					if (RunChild(S1, FirstContact, C, J, false, Group, Route, DeadAt) != EStepOutcome::NotBegan) break;
				}
			}
		}
		return EStepOutcome::Began;
	}

	EStepOutcome RunChild(FRollbackData& S1, int32 ParentHit, int32 C, int32 J, bool bCancelKind,
						  const TArray<FGroupContact>& Group, const FRouteRec& Route, int32 ParentDeadAt)
	{
		int32 Truncated = 0;
		for (const FGroupContact& Gc : Group)
		{
			if (Gc.Frame < C) Truncated += Gc.Damage;
		}
		FRouteRec Child = Route;
		Child.Total = Route.Total - Route.Steps.Last().Damage + Truncated;
		Child.Steps.Last().Damage = Truncated;
		return RunStep(S1, ParentHit, ParentHit, J, bCancelKind, C, Child, ParentDeadAt);
	}

	// Sk is the snapshot at the end of frame Fk (the situation for Fk 0); H is the most recent earlier hit
	// frame or -1 when there is none.
	void Explore(FRollbackData& Sk, int32 Fk, int32 H, const FRouteRec& Prefix)
	{
		if (Prefix.Steps.Num() >= Req.MaxSteps) return;
		Load(Sk);
		const bool bIdleAtSk = B.Attacker->GetStateType() == EStateType::Standing;
		// Scan under neutral: the first idle frame R and any hit by the action in progress.
		int32 R = bIdleAtSk ? Fk + 1 : -1;
		int32 Hp = -1;
		int32 PrevHealth = B.Defender->CurrentHealth;
		if (!bIdleAtSk)
		{
			for (int32 F = Fk + 1; F <= Req.MaxFrames; ++F)
			{
				Step(INP_Neutral);
				const FFrameObs O = Observe(B);
				if (O.Health < PrevHealth) Hp = F;
				PrevHealth = O.Health;
				if (O.bIdle)
				{
					R = F;
					break;
				}
			}
		}
		const int32 HEff = FMath::Max(H, Hp);
		TArray<int32> CancelFrames;
		TArray<int32> LinkFrames;
		if (!bIdleAtSk && HEff >= 0)
		{
			const int32 End = R >= 0 ? R - 1 : Req.MaxFrames - 1;
			for (int32 F = FMath::Max(HEff, Fk) + 1; F <= End; ++F) CancelFrames.Add(F);
		}
		if (R >= 0)
		{
			for (int32 F = R; F <= Req.MaxFrames - 1; ++F) LinkFrames.Add(F);
		}
		for (int32 I = 0; I < Req.Moves.Num(); ++I)
		{
			for (int32 KindIdx = 0; KindIdx < 2; ++KindIdx)
			{
				const bool bCancel = KindIdx == 0;
				for (int32 F : bCancel ? CancelFrames : LinkFrames)
				{
					if (RunStep(Sk, Fk, HEff, I, bCancel, F, Prefix) == EStepOutcome::NotBegan) continue;
					break;
				}
			}
		}
	}
};

TArray<FRouteRec> Enumerate(FNSE025Battle& B, const FComboSearchRequest& Req, bool bAnchored, FEnumStats* Stats = nullptr)
{
	FEnumerator E(B, Req, bAnchored);
	TArray<FRouteRec> Routes = E.Run();
	if (Stats)
	{
		Stats->Frames += E.Frames;
		Stats->Restores += E.Restores;
		Stats->Escapes += E.Escapes;
		Stats->Blocks += E.Blocks;
		Stats->Whiffs += E.Whiffs;
	}
	return Routes;
}

struct FDiscovery
{
	FComboSearchResult Result;
	int32 FrameDelta = 0;
};

FDiscovery Discover(FNSE025Battle& B, const FComboSearchRequest& Req)
{
	FDiscovery D;
	const int32 Before = B.Game->LocalFrame;
	D.Result = UComboDiscovery::DiscoverCombos(B.Game, Req);
	D.FrameDelta = B.Game->LocalFrame - Before;
	return D;
}

// The returned list must be exactly the first MaxResults entries of Expected.
void ExpectList(FAutomationTestBase& Test, const FString& Label, const FComboSearchResult& Actual,
				const TArray<FRouteRec>& Expected, const FComboSearchRequest& Req, bool bExpectComplete)
{
	const int32 N = FMath::Min(Expected.Num(), FMath::Max(Req.MaxResults, 0));
	Test.TestEqual(Label + TEXT(": trial count"), Actual.Trials.Num(), N);
	bool bMismatch = Actual.Trials.Num() != N;
	for (int32 I = 0; I < FMath::Min(N, Actual.Trials.Num()); ++I)
	{
		if (!TrialMatches(Actual.Trials[I], Expected[I], Req))
		{
			Test.AddError(FString::Printf(TEXT("%s: trial #%d differs: expected %s, actual %s"), *Label, I,
										  *RouteStr(Expected[I]), *TrialStr(Actual.Trials[I])));
			bMismatch = true;
			break;
		}
	}
	if (bMismatch)
	{
		Test.AddInfo(FString::Printf(TEXT("%s: expected list %s"), *Label, *ListStr(Expected)));
		Test.AddInfo(FString::Printf(TEXT("%s: actual list %s"), *Label, *TrialListStr(Actual.Trials)));
	}
	Test.TestEqual(Label + TEXT(": bComplete"), Actual.bComplete ? 1 : 0, bExpectComplete ? 1 : 0);
}

// Complete search with the full budget: exact list, and the frame delta bounded by the budget from above and
// by the best route's last hit from below.
FDiscovery CheckExact(FAutomationTestBase& Test, const FString& Label, FNSE025Battle& B, const FComboSearchRequest& Req,
					  const TArray<FRouteRec>& Expected)
{
	FDiscovery D = Discover(B, Req);
	ExpectList(Test, Label, D.Result, Expected, Req, true);
	Test.TestTrue(FString::Printf(TEXT("%s: LocalFrame delta %d within budget %d"), *Label, D.FrameDelta,
								  Req.MaxSimulatedFrames), D.FrameDelta <= Req.MaxSimulatedFrames);
	if (Expected.Num() > 0 && Req.MaxResults > 0)
	{
		Test.TestTrue(FString::Printf(TEXT("%s: LocalFrame delta %d at least the best route's last hit %d"), *Label,
									  D.FrameDelta, Expected[0].LastHit()), D.FrameDelta >= Expected[0].LastHit());
	}
	return D;
}

enum class EComplete
{
	Any,
	Yes,
	No,
};

// No trials; the completion flag is checked only where the instruction pins it (a searched domain that is exhausted).
void ExpectEmpty(FAutomationTestBase& Test, const FString& Label, FNSE025Battle& B, const FComboSearchRequest& Req,
				 EComplete Complete)
{
	const FDiscovery D = Discover(B, Req);
	Test.TestEqual(Label + TEXT(": no trials"), D.Result.Trials.Num(), 0);
	if (Complete != EComplete::Any)
	{
		Test.TestEqual(Label + TEXT(": bComplete"), D.Result.bComplete ? 1 : 0, Complete == EComplete::Yes ? 1 : 0);
	}
	if (Req.MaxSimulatedFrames >= 0)
	{
		Test.TestTrue(FString::Printf(TEXT("%s: LocalFrame delta %d within budget %d"), *Label, D.FrameDelta,
									  Req.MaxSimulatedFrames), D.FrameDelta <= Req.MaxSimulatedFrames);
	}
}

// Replays a trial's tape from the current situation and checks it reproduces the recorded begin
// frames, first contacts and grouped damage. Returns the tape player's result. Leaves the battle stepped.
FSimResult CheckTrialReplays(FAutomationTestBase& Test, const FString& Label, FNSE025Battle& B, const FComboTrial& T,
							 int32 Opp, bool bAnchored)
{
	const int32 LastHit = T.Steps.Num() ? T.Steps.Last().HitFrame : 0;
	Test.TestEqual(Label + TEXT(": Inputs length equals the last hit frame"), T.Inputs.Num(), LastHit);
	const FSimResult R = Simulate(B, T.Inputs, Opp, SettleWindow, false, bAnchored);
	for (int32 I = 0; I < T.Steps.Num(); ++I)
	{
		const FComboStep& S = T.Steps[I];
		Test.TestTrue(FString::Printf(TEXT("%s: step %d state %s began on frame %d"), *Label, I, *S.State.ToString(),
									  S.BeginFrame), R.BeganAt(S.BeginFrame, S.State));
		const FContact* C = R.HitAt(S.HitFrame);
		Test.TestTrue(FString::Printf(TEXT("%s: step %d first contact on frame %d"), *Label, I, S.HitFrame), C != nullptr);
		if (C)
		{
			Test.TestTrue(FString::Printf(TEXT("%s: step %d first contact on frame %d is by state %s"), *Label, I, S.HitFrame,
										  *S.State.ToString()), C->State == S.State);
		}
	}
	const FVerdict J = Judge(R, T);
	Test.TestTrue(Label + TEXT(": the replayed tape completes as the trial's steps"), J.bCompleted);
	return R;
}

// Checks the tape's entries: the move's input on begin frames and INP_Neutral elsewhere.
void CheckTape(FAutomationTestBase& Test, const FString& Label, const FComboTrial& T, const FComboSearchRequest& Req)
{
	for (int32 F = 1; F <= T.Inputs.Num(); ++F)
	{
		int32 Expected = INP_Neutral;
		for (const FComboStep& S : T.Steps)
		{
			if (S.BeginFrame == F && Req.Moves.IsValidIndex(S.MoveIndex)) Expected = Req.Moves[S.MoveIndex].Input;
		}
		if (T.Inputs[F - 1] != Expected)
		{
			Test.AddError(FString::Printf(TEXT("%s: tape entry for frame %d is %d, expected %d (%s)"), *Label, F,
										  T.Inputs[F - 1], Expected, *TrialStr(T)));
			return;
		}
	}
}

// Battle integrity: everything that decides later frames, read directly.
struct FObservables
{
	TArray<int64> Values;
	TArray<FString> Names;
	bool operator==(const FObservables& O) const { return Values == O.Values && Names == O.Names; }
	bool operator!=(const FObservables& O) const { return !(*this == O); }
	FString ToString() const
	{
		FString S;
		for (int64 V : Values) S += FString::Printf(TEXT("%lld "), V);
		for (const FString& N : Names) S += N + TEXT(" ");
		return S;
	}
};

FObservables ObserveAll(FNSE025Battle& B)
{
	FObservables O;
	O.Values.Add(B.Game->BattleState.FrameNumber);
	for (APlayerObject* P : {B.Attacker, B.Defender})
	{
		O.Values.Add(P->CurrentHealth);
		O.Values.Add(P->PosX);
		O.Values.Add(P->PosY);
		O.Values.Add(P->ActionTime);
		O.Values.Add(static_cast<int64>(P->StunTime));
		O.Values.Add(static_cast<int64>(P->Hitstop));
		O.Values.Add(P->ComboCounter);
		O.Values.Add(static_cast<int64>(P->PlayerFlags));
		O.Values.Add(static_cast<int64>(P->AttackFlags));
		O.Values.Add(P->Pushback);
		O.Values.Add(P->TotalProration);
		O.Values.Add(P->CanReverseBeat ? 1 : 0);
		for (int32 I = 0; I < InputBufferSize; ++I) O.Values.Add(P->StoredInputBuffer.InputBufferInternal[I]);
		O.Names.Add(P->PrimaryStateMachine.CurrentState->Name.ToString());
	}
	return O;
}

// A fixed 30-frame tape (Jab on frame 2, Strong on frame 12) with the per-frame observables it produces.
TArray<FObservables> TraceFrames(FNSE025Battle& B)
{
	TArray<FObservables> Trace;
	for (int32 F = 1; F <= 30; ++F)
	{
		StepBattle(B, F == 2 ? INP_A : F == 12 ? INP_B : INP_Neutral, INP_Neutral);
		Trace.Add(ObserveAll(B));
	}
	return Trace;
}

template <typename TAction>
void CheckIntegrity(FAutomationTestBase& Test, const FString& Label, FNSE025Battle& B, FRollbackData& Situation,
					TAction Action)
{
	Restore(B, Situation);
	const FObservables Before = ObserveAll(B);
	const TArray<FObservables> Control = TraceFrames(B);
	Restore(B, Situation);
	Action();
	const FObservables After = ObserveAll(B);
	if (After != Before)
	{
		Test.AddError(FString::Printf(TEXT("%s: situation observables changed: before %s after %s"), *Label,
									  *Before.ToString(), *After.ToString()));
	}
	const TArray<FObservables> Trace = TraceFrames(B);
	bool bSame = Trace.Num() == Control.Num();
	for (int32 I = 0; bSame && I < Trace.Num(); ++I)
	{
		if (Trace[I] != Control[I])
		{
			bSame = false;
			Test.AddError(FString::Printf(TEXT("%s: continued stepping differs from the untouched control on frame %d: control %s actual %s"),
										  *Label, I + 1, *Control[I].ToString(), *Trace[I].ToString()));
		}
	}
	Test.TestTrue(Label + TEXT(": 30-frame continuation matches the untouched control"), bSame);
	Restore(B, Situation);
}

int32 GetSeed(int32 Default)
{
	int32 Seed = Default;
	FParse::Value(FCommandLine::Get(), TEXT("-FuzzSeed="), Seed);
	return Seed;
}

int32 GetIterations(int32 Default)
{
	int32 Iterations = Default;
	FParse::Value(FCommandLine::Get(), TEXT("-FuzzIterations="), Iterations);
	return FMath::Max(1, Iterations);
}

FString HistogramStr(const TMap<FString, int32>& Counts)
{
	TArray<FString> Keys;
	Counts.GetKeys(Keys);
	Keys.Sort();
	FString S;
	for (const FString& K : Keys)
	{
		S += FString::Printf(TEXT("%s=%d "), *K, Counts[K]);
	}
	return S;
}

void Bump(TMap<FString, int32>& Counts, const FString& Key, int32 N = 1)
{
	Counts.FindOrAdd(Key) += N;
}

FString StatsStr(const FEnumStats& S, double Seconds)
{
	return FString::Printf(TEXT("enumerator frames=%lld restores=%d escapes=%d blocks=%d whiffs=%d; test frames=%lld restores=%lld seconds=%.1f fps=%.0f"),
						   S.Frames, S.Restores, S.Escapes, S.Blocks, S.Whiffs, GCounters.Frames, GCounters.Restores, Seconds,
						   Seconds > 0 ? GCounters.Frames / Seconds : 0.0);
}

struct FScopedCounters
{
	double Start;
	FScopedCounters() : Start(FPlatformTime::Seconds()) { GCounters = FCounters(); }
	double Seconds() const { return FPlatformTime::Seconds() - Start; }
};

void AddReferenceMoves(FNSE025Battle& B)
{
	B.AddMove(Jab());
	B.AddMove(Strong());
	B.AddMove(Special());
	B.AddMove(Poke());
}

struct FPress
{
	int32 Frame;
	int32 Input;
};

TArray<int32> Tape(int32 Length, const TArray<FPress>& Presses)
{
	TArray<int32> T;
	T.Init(INP_Neutral, Length);
	for (const FPress& P : Presses)
	{
		T[P.Frame - 1] = P.Input;
	}
	return T;
}
} // namespace NSE025Test

using namespace NSE025Test;

// V1: one-step domain, timing, tape shape, rejections and degenerate requests.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE025SingleStep, "UnrealBench.NSE025.SingleStep",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE025SingleStep::RunTest(const FString&)
{
	FScopedCounters Timer;
	FNSE025Battle B;
	B.Attacker->CanReverseBeat = false;
	AddReferenceMoves(B);
	B.AddMove(Launcher());
	B.Start();
	FEnumStats Stats;

	// Fixture timeline of Jab from idle: hit on frame 5 for 300.
	{
		const FSimResult R = Simulate(B, Tape(1, {{1, INP_A}}), INP_Neutral, 30, false, false);
		AddInfo(FString::Printf(TEXT("V1 Jab timeline: contacts %s entries=%d"), *R.ContactsStr(), R.Entries.Num()));
		TestTrue(TEXT("V1 fixture: Jab begins on frame 1"), R.BeganAt(1, FNSE025Battle::MoveTag(0)));
		TestTrue(TEXT("V1 fixture: Jab hits on frame 5"), R.HitAt(5) != nullptr);
		if (R.HitAt(5)) TestEqual(TEXT("V1 fixture: Jab damage 300"), R.HitAt(5)->Damage, 300);
		B.Reset();
	}

	struct FSingle
	{
		int32 Slot;
		int32 Hit;
		int32 Damage;
		const TCHAR* Name;
	};
	for (const FSingle& S : {FSingle{0, 5, 300, TEXT("Jab")}, FSingle{3, 4, 200, TEXT("Poke")}, FSingle{1, 9, 700, TEXT("Strong")}})
	{
		const FComboSearchRequest Req = MakeRequest(B, {S.Slot}, 1, 60, 10);
		const TArray<FRouteRec> E = Enumerate(B, Req, false, &Stats);
		const FString Label = FString::Printf(TEXT("V1 single %s"), S.Name);
		TestEqual(Label + TEXT(": enumerator finds one route"), E.Num(), 1);
		if (E.Num() == 1)
		{
			TestEqual(Label + TEXT(": enumerator begin frame 1"), E[0].Steps[0].Begin, 1);
			TestEqual(Label + TEXT(": enumerator hit frame"), E[0].Steps[0].Hit, S.Hit);
			TestEqual(Label + TEXT(": enumerator damage"), E[0].Steps[0].Damage, S.Damage);
		}
		const FDiscovery D = CheckExact(*this, Label, B, Req, E);
		if (D.Result.Trials.Num() == 1)
		{
			const FComboTrial& T = D.Result.Trials[0];
			TestEqual(Label + TEXT(": one step"), T.Steps.Num(), 1);
			if (T.Steps.Num() == 1)
			{
				TestEqual(Label + TEXT(": BeginFrame 1"), T.Steps[0].BeginFrame, 1);
				TestEqual(Label + TEXT(": HitFrame"), T.Steps[0].HitFrame, S.Hit);
				TestEqual(Label + TEXT(": Damage"), T.Steps[0].Damage, S.Damage);
				TestEqual(Label + TEXT(": MoveIndex 0"), T.Steps[0].MoveIndex, 0);
				TestTrue(Label + TEXT(": Kind is Link from idle"), T.Steps[0].Kind == EComboStepKind::Link);
				TestTrue(Label + TEXT(": State is the requested tag"), T.Steps[0].State == FNSE025Battle::MoveTag(S.Slot));
			}
			TestEqual(Label + TEXT(": TotalDamage"), T.TotalDamage, S.Damage);
			TestEqual(Label + TEXT(": Inputs length equals hit frame"), T.Inputs.Num(), S.Hit);
			CheckTape(*this, Label, T, Req);
			if (T.Inputs.Num() >= 1) TestEqual(Label + TEXT(": Inputs[0] is the move's button"), T.Inputs[0], B.Moves[S.Slot].Input);
			if (T.Inputs.Num() >= 2) TestEqual(Label + TEXT(": Inputs[1] is INP_Neutral"), T.Inputs[1], static_cast<int32>(INP_Neutral));
		}
	}

	// Launcher: the step's hit launches the defender, held by the authored untech.
	{
		const FSimResult R = Simulate(B, Tape(1, {{1, INP_E}}), INP_Neutral, 30, false, false);
		AddInfo(FString::Printf(TEXT("V1 Launcher timeline: contacts %s"), *R.ContactsStr()));
		TestTrue(TEXT("V1 fixture: Launcher hits on frame 7"), R.HitAt(7) != nullptr);
		if (R.HitAt(7)) TestEqual(TEXT("V1 fixture: Launcher damage 500"), R.HitAt(7)->Damage, 500);
		TestTrue(TEXT("V1 fixture: the defender is airborne and held after the launch"),
				 R.Frames.Num() >= 12 && R.Frames[11].DefY > 0 && R.Frames[11].bStunned);
		B.Reset();
		const FComboSearchRequest Req = MakeRequest(B, {4}, 1, 60, 10);
		const TArray<FRouteRec> E = Enumerate(B, Req, false, &Stats);
		const FString Label = TEXT("V1 single Launcher");
		TestEqual(Label + TEXT(": enumerator finds one route"), E.Num(), 1);
		if (E.Num() == 1)
		{
			TestEqual(Label + TEXT(": enumerator hit frame"), E[0].Steps[0].Hit, 7);
			TestEqual(Label + TEXT(": enumerator damage"), E[0].Steps[0].Damage, 500);
		}
		CheckExact(*this, Label, B, Req, E);
	}

	// Rejections: unknown states and duplicates give no trials; the completion flag is not pinned there.
	{
		FComboSearchRequest Req = MakeRequest(B, {0}, 1, 60, 10);
		FComboMove Unknown;
		Unknown.State = FNSE025Battle::MoveTag(7);
		Unknown.Input = INP_H;
		Req.Moves.Add(Unknown);
		ExpectEmpty(*this, TEXT("V1 reject: state the attacker lacks (MoveTag(7))"), B, Req, EComplete::Any);
	}
	{
		FComboSearchRequest Req = MakeRequest(B, {0}, 1, 60, 10);
		FComboMove Empty;
		Empty.Input = INP_H;
		Req.Moves.Add(Empty);
		ExpectEmpty(*this, TEXT("V1 reject: empty gameplay tag"), B, Req, EComplete::Any);
	}
	ExpectEmpty(*this, TEXT("V1 reject: Jab listed twice"), B, MakeRequest(B, {0, 0}, 1, 60, 10), EComplete::Any);
	ExpectEmpty(*this, TEXT("V1 budget -1"), B, MakeRequest(B, {0}, 1, 60, 10, -1), EComplete::Any);
	ExpectEmpty(*this, TEXT("V1 budget 0"), B, MakeRequest(B, {0}, 1, 60, 10, 0), EComplete::Any);
	// Degenerate limits give no trials; the completion flag is not pinned there either.
	ExpectEmpty(*this, TEXT("V1 empty move list"), B, MakeRequest(B, {}, 1, 60, 10), EComplete::Any);
	ExpectEmpty(*this, TEXT("V1 MaxSteps 0"), B, MakeRequest(B, {0}, 0, 60, 10), EComplete::Any);
	ExpectEmpty(*this, TEXT("V1 MaxResults 0"), B, MakeRequest(B, {0}, 1, 60, 0), EComplete::Any);
	ExpectEmpty(*this, TEXT("V1 MaxFrames 0"), B, MakeRequest(B, {0}, 1, 0, 10), EComplete::Any);
	// MaxFrames boundary: Jab hits on frame 5.
	ExpectEmpty(*this, TEXT("V1 MaxFrames 4 with Jab (hit on 5)"), B, MakeRequest(B, {0}, 1, 4, 10), EComplete::Yes);
	{
		const FComboSearchRequest Req = MakeRequest(B, {0}, 1, 5, 10);
		const TArray<FRouteRec> E = Enumerate(B, Req, false, &Stats);
		TestEqual(TEXT("V1 MaxFrames 5: enumerator keeps the Jab route"), E.Num(), 1);
		CheckExact(*this, TEXT("V1 MaxFrames 5 with Jab (hit on 5)"), B, Req, E);
	}
	AddInfo(FString::Printf(TEXT("V1 %s"), *StatsStr(Stats, Timer.Seconds())));
	return !HasAnyErrors();
}

// V2: cancels, links, chain limits, reverse beat, chain reset through links, late cancel windows.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE025CancelsAndLinks, "UnrealBench.NSE025.CancelsAndLinks",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE025CancelsAndLinks::RunTest(const FString&)
{
	FScopedCounters Timer;
	FNSE025Battle B;
	B.Attacker->CanReverseBeat = false;
	AddReferenceMoves(B);
	B.AddMove(MultiJab());
	B.Start();
	FEnumStats Stats;
	const int32 JabTag = 0, StrongTag = 1, PokeTag = 3;

	// Fixture replays of the hand-derived timeline.
	{
		const FSimResult R = Simulate(B, Tape(9, {{1, INP_A}, {9, INP_B}}), INP_Neutral, 30, false, false);
		AddInfo(FString::Printf(TEXT("V2 [Jab, Strong@9]: %s"), *R.ContactsStr()));
		TestTrue(TEXT("V2 fixture [Jab, Strong cancel]: Strong begins on frame 9"), R.BeganAt(9, FNSE025Battle::MoveTag(StrongTag)));
		TestTrue(TEXT("V2 fixture [Jab, Strong cancel]: Jab hits on 5"), R.HitAt(5) != nullptr);
		TestTrue(TEXT("V2 fixture [Jab, Strong cancel]: Strong hits on 17"), R.HitAt(17) != nullptr);
		if (R.HitAt(17)) TestEqual(TEXT("V2 fixture [Jab, Strong cancel]: Strong second-hit damage 302"), R.HitAt(17)->Damage, 302);
		if (R.HitAt(17)) TestTrue(TEXT("V2 fixture [Jab, Strong cancel]: Strong's hit continues the combo"), R.HitAt(17)->bContinues);
		B.Reset();
	}
	{
		const FSimResult R = Simulate(B, Tape(16, {{1, INP_A}, {16, INP_D}}), INP_Neutral, 30, false, false);
		AddInfo(FString::Printf(TEXT("V2 [Jab, Poke@16]: %s"), *R.ContactsStr()));
		TestTrue(TEXT("V2 fixture [Jab, Poke link]: Poke begins on frame 16"), R.BeganAt(16, FNSE025Battle::MoveTag(PokeTag)));
		TestTrue(TEXT("V2 fixture [Jab, Poke link]: Poke hits on 19"), R.HitAt(19) != nullptr);
		if (R.HitAt(19)) TestEqual(TEXT("V2 fixture [Jab, Poke link]: Poke second-hit damage 108"), R.HitAt(19)->Damage, 108);
		if (R.HitAt(19)) TestTrue(TEXT("V2 fixture [Jab, Poke link]: Poke's hit on 19 continues the combo"), R.HitAt(19)->bContinues);
		B.Reset();
	}
	{
		const FSimResult Neutral = Simulate(B, Tape(15, {{1, INP_A}}), INP_Neutral, 5, false, false);
		TestTrue(TEXT("V2 fixture: Jab in progress at the end of frame 15"), Neutral.Frames.Num() >= 16 && !Neutral.Frames[14].bIdle);
		TestTrue(TEXT("V2 fixture: Jab idle at the end of frame 16 under neutral"), Neutral.Frames.Num() >= 16 && Neutral.Frames[15].bIdle);
		B.Reset();
		const FSimResult R = Simulate(B, Tape(16, {{1, INP_A}, {16, INP_A}}), INP_Neutral, 30, false, false);
		AddInfo(FString::Printf(TEXT("V2 [Jab, Jab@16]: %s"), *R.ContactsStr()));
		TestTrue(TEXT("V2 fixture [Jab, Jab link]: second Jab begins on frame 16"), R.BeganAt(16, FNSE025Battle::MoveTag(JabTag)));
		TestTrue(TEXT("V2 fixture [Jab, Jab link]: second Jab connects on frame 20"), R.HitAt(20) != nullptr);
		if (R.HitAt(20))
		{
			TestFalse(TEXT("V2 fixture [Jab, Jab link]: the hit on frame 20 lands on the control frame (escape)"), R.HitAt(20)->bContinues);
		}
		B.Reset();
	}

	// Two-step domain without reverse beat.
	const FComboSearchRequest Req2 = MakeRequest(B, {0, 1, 2, 3}, 2, 60, 100);
	const TArray<FRouteRec> E1 = Enumerate(B, Req2, false, &Stats);
	AddInfo(FString::Printf(TEXT("V2 E1: %s"), *ListStr(E1)));
	{
		const int32 JS = IndexOfShape(E1, {{0, false}, {1, true}});
		TestTrue(TEXT("V2 E1: [Jab, Strong cancel] present"), JS >= 0);
		if (JS >= 0)
		{
			TestEqual(TEXT("V2 E1: [Jab, Strong cancel] begins 1"), E1[JS].Steps[0].Begin, 1);
			TestEqual(TEXT("V2 E1: [Jab, Strong cancel] cancel begins 9"), E1[JS].Steps[1].Begin, 9);
			TestEqual(TEXT("V2 E1: [Jab, Strong cancel] hits 5"), E1[JS].Steps[0].Hit, 5);
			TestEqual(TEXT("V2 E1: [Jab, Strong cancel] second hit 17"), E1[JS].Steps[1].Hit, 17);
			TestEqual(TEXT("V2 E1: [Jab, Strong cancel] total 602"), E1[JS].Total, 602);
		}
		const int32 JP = IndexOfShape(E1, {{0, false}, {3, false}});
		TestTrue(TEXT("V2 E1: [Jab, Poke link] present"), JP >= 0);
		if (JP >= 0)
		{
			TestEqual(TEXT("V2 E1: [Jab, Poke link] link begins 16"), E1[JP].Steps[1].Begin, 16);
			TestEqual(TEXT("V2 E1: [Jab, Poke link] second hit 19"), E1[JP].Steps[1].Hit, 19);
			TestEqual(TEXT("V2 E1: [Jab, Poke link] total 408"), E1[JP].Total, 408);
		}
		TestEqual(TEXT("V2 E1: [Jab, Jab link] absent (hit on the control frame)"), IndexOfShape(E1, {{0, false}, {0, false}}), -1);
		TestEqual(TEXT("V2 E1: [Jab, Jab cancel] absent (not in its chain list, no reverse beat)"), IndexOfShape(E1, {{0, false}, {0, true}}), -1);
		TestTrue(TEXT("V2 E1: [Poke, Poke cancel] present (self chain)"), IndexOfShape(E1, {{3, false}, {3, true}}) >= 0);
		TestEqual(TEXT("V2 E1: [Strong, Jab cancel] absent without reverse beat"), IndexOfShape(E1, {{1, false}, {0, true}}), -1);
		bool bAnyBegin17 = false;
		for (const FRouteRec& R : E1)
		{
			for (const FStepRec& S : R.Steps) bAnyBegin17 |= S.Begin == 17;
		}
		TestFalse(TEXT("V2 E1: no step begins on frame 17"), bAnyBegin17);
	}
	CheckExact(*this, TEXT("V2 two steps, no reverse beat"), B, Req2, E1);

	// Reverse beat with Jab's chain list emptied.
	B.Attacker->CanReverseBeat = true;
	{
		auto NoChainJab = Jab();
		NoChainJab.ChainCancels = {};
		B.SetMove(0, NoChainJab);
	}
	B.Start();
	const TArray<FRouteRec> E2 = Enumerate(B, Req2, false, &Stats);
	AddInfo(FString::Printf(TEXT("V2 E2 (reverse beat): %s"), *ListStr(E2)));
	TestTrue(TEXT("V2 E2: [Jab, Strong cancel] present through reverse beat"), IndexOfShape(E2, {{0, false}, {1, true}}) >= 0);
	TestTrue(TEXT("V2 E2: [Strong, Jab cancel] present through reverse beat"), IndexOfShape(E2, {{1, false}, {0, true}}) >= 0);
	TestEqual(TEXT("V2 E2: [Jab, Jab cancel] absent (MaxChain 1 caps the self chain reverse beat would allow)"), IndexOfShape(E2, {{0, false}, {0, true}}), -1);
	TestTrue(TEXT("V2 E2: [Poke, Poke cancel] present (MaxChain -1)"), IndexOfShape(E2, {{3, false}, {3, true}}) >= 0);
	CheckExact(*this, TEXT("V2 two steps, reverse beat"), B, Req2, E2);

	// Three steps: chain limits and the reset through a link.
	const FComboSearchRequest Req3 = MakeRequest(B, {0, 1, 2, 3}, 3, 60, 100);
	const TArray<FRouteRec> E3 = Enumerate(B, Req3, false, &Stats);
	AddInfo(FString::Printf(TEXT("V2 E3 (three steps): %s"), *ListStr(E3)));
	TestEqual(TEXT("V2 E3: [Jab, Strong cancel, Jab cancel] absent (a move used in the chain cannot return through reverse beat)"), IndexOfShape(E3, {{0, false}, {1, true}, {0, true}}), -1);
	TestTrue(TEXT("V2 E3: [Jab, Poke link, Jab cancel] present (the link resets the chain)"), IndexOfShape(E3, {{0, false}, {3, false}, {0, true}}) >= 0);
	TestTrue(TEXT("V2 E3: [Poke, Poke cancel, Poke cancel] present"), IndexOfShape(E3, {{3, false}, {3, true}, {3, true}}) >= 0);
	TestTrue(TEXT("V2 E3: [Jab, Poke link, Poke cancel] present"), IndexOfShape(E3, {{0, false}, {3, false}, {3, true}}) >= 0);
	CheckExact(*this, TEXT("V2 three steps, reverse beat"), B, Req3, E3);

	// Late cancel window: Jab offers its chain options from action frame 7.
	B.Attacker->CanReverseBeat = false;
	B.SetMove(0, Jab());
	B.SetMove(1, Strong());
	B.SetMove(2, Special());
	B.SetMove(3, Poke());
	B.Start();
	{
		auto LateJab = Jab();
		LateJab.CancelFrom = 7;
		B.SetMove(0, LateJab);
	}
	for (int32 Press : {9, 10})
	{
		const FSimResult R = Simulate(B, Tape(Press, {{1, INP_A}, {Press, INP_B}}), INP_Neutral, 2, false, false);
		TestFalse(FString::Printf(TEXT("V2 fixture late window: Strong pressed on %d does not begin on %d"), Press, Press),
				  R.BeganAt(Press, FNSE025Battle::MoveTag(StrongTag)));
		B.Reset();
	}
	{
		const FSimResult R = Simulate(B, Tape(11, {{1, INP_A}, {11, INP_B}}), INP_Neutral, 30, false, false);
		AddInfo(FString::Printf(TEXT("V2 late window [Jab, Strong@11]: %s"), *R.ContactsStr()));
		TestTrue(TEXT("V2 fixture late window: Strong pressed on 11 begins on 11"), R.BeganAt(11, FNSE025Battle::MoveTag(StrongTag)));
		TestTrue(TEXT("V2 fixture late window: Strong hits on 19"), R.HitAt(19) != nullptr);
		B.Reset();
	}
	const TArray<FRouteRec> E4 = Enumerate(B, Req2, false, &Stats);
	AddInfo(FString::Printf(TEXT("V2 E4 (late window): %s"), *ListStr(E4)));
	{
		const int32 JS = IndexOfShape(E4, {{0, false}, {1, true}});
		TestTrue(TEXT("V2 E4: [Jab, Strong cancel] present with the late window"), JS >= 0);
		if (JS >= 0)
		{
			TestEqual(TEXT("V2 E4: [Jab, Strong cancel] cancel begins 11"), E4[JS].Steps[1].Begin, 11);
			TestEqual(TEXT("V2 E4: [Jab, Strong cancel] second hit 19"), E4[JS].Steps[1].Hit, 19);
		}
	}
	CheckExact(*this, TEXT("V2 two steps, late cancel window"), B, Req2, E4);

	// Multi-hit: MultiJab re-arms its hitbox on action frame 8, so the step's contacts are two and its
	// damage their sum with HitFrame at the first; a cancel between the contacts truncates the group.
	B.Reset();
	const FSimResult RM = Simulate(B, Tape(1, {{1, INP_G}}), INP_Neutral, 30, false, false);
	AddInfo(FString::Printf(TEXT("V2 MultiJab timeline: contacts %s"), *RM.ContactsStr()));
	TestEqual(TEXT("V2 fixture MultiJab: two contacts"), RM.HitCount(), 2);
	const int32 MultiFirst = RM.HitCount() == 2 ? RM.Contacts[0].Frame : -1;
	const int32 MultiSum = RM.HealthLost;
	if (RM.HitCount() == 2)
	{
		TestEqual(TEXT("V2 fixture MultiJab: first contact on frame 5"), MultiFirst, 5);
		TestEqual(TEXT("V2 fixture MultiJab: second contact on frame 11"), RM.Contacts[1].Frame, 11);
		TestEqual(TEXT("V2 fixture MultiJab: the group removes 231"), MultiSum, 231);
		TestTrue(TEXT("V2 fixture MultiJab: both contacts continue"),
				 RM.Contacts[0].bContinues && RM.Contacts[1].bContinues);
		AddInfo(FString::Printf(TEXT("V2 MultiJab damages %d + %d = %d"), RM.Contacts[0].Damage,
								RM.Contacts[1].Damage, MultiSum));
	}
	B.Reset();
	const FComboSearchRequest ReqM = MakeRequest(B, {4}, 1, 60, 10);
	const TArray<FRouteRec> EM = Enumerate(B, ReqM, false, &Stats);
	TestEqual(TEXT("V2 MultiJab: enumerator finds one route"), EM.Num(), 1);
	if (EM.Num() == 1)
	{
		TestEqual(TEXT("V2 MultiJab: HitFrame is the first contact"), EM[0].Steps[0].Hit, MultiFirst);
		TestEqual(TEXT("V2 MultiJab: damage is the sum of the contacts"), EM[0].Steps[0].Damage, MultiSum);
		TestEqual(TEXT("V2 MultiJab: tape runs to the first contact"), EM[0].Inputs.Num(), MultiFirst);
	}
	CheckExact(*this, TEXT("V2 MultiJab single step"), B, ReqM, EM);
	// A cancel between the contacts: the press on frame 6 buffers through the freeze and the Jab
	// begins when it lifts, on frame 8; the group truncates to the first contact and the Jab follows.
	{
		const FSimResult RC = Simulate(B, Tape(20, {{1, INP_G}, {6, INP_A}}), INP_Neutral, 30, false, false);
		AddInfo(FString::Printf(TEXT("V2 [MultiJab, Jab cancel@6]: %s"), *RC.ContactsStr()));
		TestEqual(TEXT("V2 fixture [MultiJab, Jab cancel@6]: two hits"), RC.HitCount(), 2);
		const int32 CancelBegin = RC.Entries.Num() >= 2 ? RC.Entries[1].Frame : -1;
		TestEqual(TEXT("V2 fixture [MultiJab, Jab cancel@6]: the Jab begins on frame 8"), CancelBegin, 8);
		if (RC.HitCount() == 2)
		{
			TestEqual(TEXT("V2 fixture [MultiJab, Jab cancel@6]: the MultiJab group is only its first contact"),
					 RC.Contacts[0].Damage, RM.Contacts[0].Damage);
			TestTrue(TEXT("V2 fixture [MultiJab, Jab cancel@6]: the Jab's hit continues"), RC.Contacts[1].bContinues);
		}
		const FComboSearchRequest Req2M = MakeRequest(B, {4, 0}, 2, 60, 10);
		const TArray<FRouteRec> E2M = Enumerate(B, Req2M, false, &Stats);
		const int32 Trunc = IndexOfShape(E2M, {{0, false}, {1, true}});
		TestTrue(TEXT("V2 E2M: [MultiJab, Jab cancel] present"), Trunc >= 0);
		if (Trunc >= 0 && RC.HitCount() == 2)
		{
			TestEqual(TEXT("V2 E2M: [MultiJab, Jab cancel] first step damage is the truncated group"),
					 E2M[Trunc].Steps[0].Damage, RC.Contacts[0].Damage);
			TestEqual(TEXT("V2 E2M: [MultiJab, Jab cancel] second step damage is the Jab's hit"),
					 E2M[Trunc].Steps[1].Damage, RC.Contacts[1].Damage);
		}
		CheckExact(*this, TEXT("V2 [MultiJab, Jab] domain"), B, Req2M, E2M);
	}
	AddInfo(FString::Printf(TEXT("V2 %s"), *StatsStr(Stats, Timer.Seconds())));
	return !HasAnyErrors();
}

// V3: the escape rule from a mid-combo situation, validation of near-miss tapes, integrity.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE025EscapeRule, "UnrealBench.NSE025.EscapeRule",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE025EscapeRule::RunTest(const FString&)
{
	FScopedCounters Timer;
	FNSE025Battle B;
	B.Attacker->CanReverseBeat = false;
	AddReferenceMoves(B);
	B.AddMove(Launcher());
	B.AddMove(Floater());
	B.Start();
	FEnumStats Stats;

	// Situation S: the end of the frame on which Jab's hit registered (frame 5 from idle).
	{
		const FSimResult R = Simulate(B, Tape(5, {{1, INP_A}}), INP_Neutral, 0, false, false);
		TestTrue(TEXT("V3 situation: Jab hit on frame 5"), R.HitAt(5) != nullptr);
		TestTrue(TEXT("V3 situation: defender cannot act at the situation"), B.Defender->CheckIsStunned());
		TestFalse(TEXT("V3 situation: attacker still in Jab at the situation"), B.Attacker->GetStateType() == EStateType::Standing);
	}
	FRollbackData S = Snapshot(B);
	const int32 Guard = B.GuardInput();

	const FComboSearchRequest ReqN = MakeRequest(B, {0, 1, 2, 3}, 2, 60, 100, FullBudget, INP_Neutral);
	const FComboSearchRequest ReqG = MakeRequest(B, {0, 1, 2, 3}, 2, 60, 100, FullBudget, Guard);
	const TArray<FRouteRec> EN = Enumerate(B, ReqN, true, &Stats);
	const TArray<FRouteRec> EG = Enumerate(B, ReqG, true, &Stats);
	AddInfo(FString::Printf(TEXT("V3 EN: %s"), *ListStr(EN)));
	AddInfo(FString::Printf(TEXT("V3 EG: %s"), *ListStr(EG)));
	{
		bool bSame = EN.Num() == EG.Num();
		for (int32 I = 0; bSame && I < EN.Num(); ++I) bSame = RouteStr(EN[I]) == RouteStr(EG[I]);
		TestTrue(TEXT("V3 guard lens: enumerator lists under neutral and guard agree"), bSame);
	}
	TestEqual(TEXT("V3 EN: [Jab link] absent (its hit lands on the control frame)"), IndexOfShape(EN, {{0, false}}), -1);
	const int32 PokeLink = IndexOfShape(EN, {{3, false}});
	TestTrue(TEXT("V3 EN: [Poke link] present"), PokeLink >= 0);
	TestTrue(TEXT("V3 EN: [Strong cancel] present"), IndexOfShape(EN, {{1, true}}) >= 0);
	{
		const FDiscovery DN = CheckExact(*this, TEXT("V3 discovery under neutral"), B, ReqN, EN);
		const FDiscovery DG = CheckExact(*this, TEXT("V3 discovery under guard"), B, ReqG, EG);
		TestEqual(TEXT("V3 discovery: [Jab link] absent under neutral"), IndexOfShape(DN.Result.Trials, {{0, false}}), -1);
		TestEqual(TEXT("V3 discovery: [Jab link] absent under guard"), IndexOfShape(DG.Result.Trials, {{0, false}}), -1);
	}

	if (PokeLink >= 0)
	{
		const FComboTrial Base = ToTrial(EN[PokeLink], ReqN);
		const int32 Begin = EN[PokeLink].Steps[0].Begin;
		const int32 Hit = EN[PokeLink].Steps[0].Hit;
		AddInfo(FString::Printf(TEXT("V3 [Poke link] begins %d hits %d damage %d"), Begin, Hit, EN[PokeLink].Steps[0].Damage));

		// One frame late: the hit lands on the control frame.
		{
			FComboTrial Late = Base;
			Late.Inputs = Tape(Hit + 1, {{Begin + 1, INP_D}});
			Restore(B, S);
			const FSimResult RN = Replay(B, Late.Inputs, INP_Neutral, true);
			AddInfo(FString::Printf(TEXT("V3 late Poke under neutral: %s"), *RN.ContactsStr()));
			TestEqual(TEXT("V3 late Poke under neutral: exactly one hit"), RN.HitCount(), 1);
			if (RN.HitCount() == 1)
			{
				TestEqual(TEXT("V3 late Poke under neutral: the hit lands on the control frame (frame 15)"), RN.Contacts[0].Frame, 15);
				TestFalse(TEXT("V3 late Poke under neutral: the hit is an escape"), RN.Contacts[0].bContinues);
			}
			const FVerdict JN = Judge(RN, Late);
			Restore(B, S);
			const FVerdict VN = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, Late, INP_Neutral));
			TestFalse(TEXT("V3 late Poke under neutral: not completed"), VN.bCompleted);
			TestEqual(TEXT("V3 late Poke under neutral: FailedStep 0"), VN.FailedStep, 0);
			ExpectDamageEither(*this, TEXT("V3 late Poke under neutral"), VN.ObservedDamage, 0, RN.HealthLost);
			TestTrue(FString::Printf(TEXT("V3 late Poke under neutral: report %s equals judge %s"), *VN.ToString(), *JN.ToString()), ReportMatches(VN, JN));

			Restore(B, S);
			const FSimResult RG = Replay(B, Late.Inputs, Guard, true);
			AddInfo(FString::Printf(TEXT("V3 late Poke under guard: %s"), *RG.ContactsStr()));
			TestEqual(TEXT("V3 late Poke under guard: no hit"), RG.HitCount(), 0);
			TestTrue(TEXT("V3 late Poke under guard: the contact is a block"), RG.Contacts.Num() == 1 && !RG.Contacts[0].bHit);
			const FVerdict JG = Judge(RG, Late);
			Restore(B, S);
			const FVerdict VG = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, Late, Guard));
			TestFalse(TEXT("V3 late Poke under guard: not completed"), VG.bCompleted);
			TestEqual(TEXT("V3 late Poke under guard: FailedStep 0"), VG.FailedStep, 0);
			TestEqual(TEXT("V3 late Poke under guard: ObservedDamage 0"), VG.ObservedDamage, 0);
			TestTrue(FString::Printf(TEXT("V3 late Poke under guard: report %s equals judge %s"), *VG.ToString(), *JG.ToString()), ReportMatches(VG, JG));
		}
		// One frame early: whatever the engine does, the report equals the judge.
		if (Begin >= 2)
		{
			FComboTrial Early = Base;
			Early.Inputs = Tape(Hit, {{Begin - 1, INP_D}});
			Restore(B, S);
			const FSimResult R = Replay(B, Early.Inputs, INP_Neutral, true);
			AddInfo(FString::Printf(TEXT("V3 early Poke: %s"), *R.ContactsStr()));
			const FVerdict J = Judge(R, Early);
			Restore(B, S);
			const FVerdict V = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, Early, INP_Neutral));
			TestTrue(FString::Printf(TEXT("V3 early Poke: report %s equals judge %s"), *V.ToString(), *J.ToString()), ReportMatches(V, J));
		}
		// Extra press after the last hit, once the fighters have settled.
		{
			Restore(B, S);
			const FSimResult Settle = Replay(B, Base.Inputs, INP_Neutral, true);
			TestTrue(TEXT("V3 extra press: the base trial settles inside the window"), Settle.bSettled);
			const int32 Press = Settle.EndFrame + 1;
			FComboTrial Extra = Base;
			Extra.Inputs = Tape(Press, {{Begin, INP_D}, {Press, INP_A}});
			Restore(B, S);
			const FSimResult RN = Replay(B, Extra.Inputs, INP_Neutral, true);
			AddInfo(FString::Printf(TEXT("V3 extra press under neutral: %s"), *RN.ContactsStr()));
			TestEqual(TEXT("V3 extra press under neutral: two hits"), RN.HitCount(), 2);
			const FVerdict JN = Judge(RN, Extra);
			Restore(B, S);
			const FVerdict VN = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, Extra, INP_Neutral));
			TestFalse(TEXT("V3 extra press under neutral: not completed"), VN.bCompleted);
			TestEqual(TEXT("V3 extra press under neutral: FailedStep equals the step count"), VN.FailedStep, Extra.Steps.Num());
			ExpectDamageEither(*this, TEXT("V3 extra press under neutral"), VN.ObservedDamage, Extra.TotalDamage, RN.HealthLost);
			TestTrue(FString::Printf(TEXT("V3 extra press under neutral: report %s equals judge %s"), *VN.ToString(), *JN.ToString()), ReportMatches(VN, JN));

			Restore(B, S);
			const FSimResult RG = Replay(B, Extra.Inputs, Guard, true);
			AddInfo(FString::Printf(TEXT("V3 extra press under guard: %s"), *RG.ContactsStr()));
			TestEqual(TEXT("V3 extra press under guard: one hit and one block"), RG.Contacts.Num(), 2);
			TestEqual(TEXT("V3 extra press under guard: only the trial's hit removes health"), RG.HealthLost, Extra.TotalDamage);
			const FVerdict JG = Judge(RG, Extra);
			Restore(B, S);
			const FVerdict VG = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, Extra, Guard));
			TestFalse(TEXT("V3 extra press under guard: not completed"), VG.bCompleted);
			TestEqual(TEXT("V3 extra press under guard: FailedStep equals the step count"), VG.FailedStep, Extra.Steps.Num());
			TestEqual(TEXT("V3 extra press under guard: ObservedDamage equals the trial's total"), VG.ObservedDamage, Extra.TotalDamage);
			TestTrue(FString::Printf(TEXT("V3 extra press under guard: report %s equals judge %s"), *VG.ToString(), *JG.ToString()), ReportMatches(VG, JG));
		}
		// Truncated to the last press: the hit still lands inside the observation window.
		{
			FComboTrial Trunc = Base;
			Trunc.Inputs.SetNum(Begin);
			Restore(B, S);
			const FSimResult R = Replay(B, Trunc.Inputs, INP_Neutral, true);
			const FVerdict J = Judge(R, Trunc);
			TestTrue(TEXT("V3 truncated tape: judge completes"), J.bCompleted);
			Restore(B, S);
			const FVerdict V = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, Trunc, INP_Neutral));
			TestTrue(TEXT("V3 truncated tape: completed"), V.bCompleted);
			TestEqual(TEXT("V3 truncated tape: FailedStep -1"), V.FailedStep, -1);
			TestEqual(TEXT("V3 truncated tape: ObservedDamage equals the total"), V.ObservedDamage, Trunc.TotalDamage);
		}
		// Base trial validates under both opponent inputs.
		for (int32 Opp : {static_cast<int32>(INP_Neutral), Guard})
		{
			Restore(B, S);
			const FVerdict V = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, Base, Opp));
			TestTrue(FString::Printf(TEXT("V3 [Poke link] validates under opponent input %d"), Opp), V.bCompleted && V.FailedStep == -1);
			TestEqual(FString::Printf(TEXT("V3 [Poke link] ObservedDamage under opponent input %d"), Opp), V.ObservedDamage, Base.TotalDamage);
		}
	}

	// A trial built from the idle situation fails at step 0 from the mid-combo situation.
	{
		FComboTrial Idle;
		FComboStep St;
		St.State = FNSE025Battle::MoveTag(0);
		St.MoveIndex = 0;
		St.Kind = EComboStepKind::Link;
		St.BeginFrame = 1;
		St.HitFrame = 5;
		St.Damage = 300;
		Idle.Steps.Add(St);
		Idle.TotalDamage = 300;
		Idle.Inputs = Tape(5, {{1, INP_A}});
		Restore(B, S);
		const FSimResult R = Replay(B, Idle.Inputs, INP_Neutral, true);
		const FVerdict J = Judge(R, Idle);
		Restore(B, S);
		const FVerdict V = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, Idle, INP_Neutral));
		TestFalse(TEXT("V3 idle trial at the mid-combo situation: not completed"), V.bCompleted);
		TestEqual(TEXT("V3 idle trial at the mid-combo situation: FailedStep 0"), V.FailedStep, 0);
		TestTrue(FString::Printf(TEXT("V3 idle trial at the mid-combo situation: report %s equals judge %s"), *V.ToString(), *J.ToString()), ReportMatches(V, J));
	}

	// Integrity from the mid-combo situation.
	CheckIntegrity(*this, TEXT("V3 integrity after DiscoverCombos"), B, S, [&]() { UComboDiscovery::DiscoverCombos(B.Game, ReqN); });
	if (PokeLink >= 0)
	{
		const FComboTrial Base = ToTrial(EN[PokeLink], ReqN);
		CheckIntegrity(*this, TEXT("V3 integrity after ValidateTrial"), B, S, [&]() { UComboDiscovery::ValidateTrial(B.Game, Base, INP_Neutral); });
		// Interleaved: a search under guard cut off by its budget, a validation under guard that fails after a real
		// contact, then a full search under neutral.
		FComboTrial Wrong = Base;
		Wrong.Steps[0].Damage += 1;
		FComboSearchRequest Small = ReqG;
		Small.MaxSimulatedFrames = 30;
		CheckIntegrity(*this, TEXT("V3 integrity after an interrupted search, a failed validation and a full search"), B, S, [&]()
		{
			UComboDiscovery::DiscoverCombos(B.Game, Small);
			UComboDiscovery::ValidateTrial(B.Game, Wrong, Guard);
			UComboDiscovery::DiscoverCombos(B.Game, ReqN);
		});
	}

	// Whiffs at the far distance: contacts pair positionally.
	B.Start(-200000, 0);
	{
		FComboTrial PokeTrial;
		FComboStep St;
		St.State = FNSE025Battle::MoveTag(3);
		St.MoveIndex = 0;
		St.Kind = EComboStepKind::Link;
		St.BeginFrame = 1;
		St.HitFrame = 4;
		St.Damage = 200;
		PokeTrial.Steps.Add(St);
		PokeTrial.TotalDamage = 200;
		PokeTrial.Inputs = Tape(4, {{1, INP_D}});
		const FSimResult R = Replay(B, PokeTrial.Inputs, INP_Neutral, false);
		TestEqual(TEXT("V3 far distance: Poke whiffs"), R.Contacts.Num(), 0);
		B.Reset();
		const FVerdict V = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, PokeTrial, INP_Neutral));
		TestFalse(TEXT("V3 whiffed Poke: not completed"), V.bCompleted);
		TestEqual(TEXT("V3 whiffed Poke: FailedStep 0"), V.FailedStep, 0);
		TestEqual(TEXT("V3 whiffed Poke: ObservedDamage 0"), V.ObservedDamage, 0);

		// Poke whiffs, then a linked Jab lands: the Jab contact pairs with step 0.
		B.Reset();
		const FSimResult Idle = Simulate(B, Tape(1, {{1, INP_D}}), INP_Neutral, 30, false, false);
		int32 IdleFrame = -1;
		for (int32 I = 1; I < Idle.Frames.Num() && IdleFrame < 0; ++I)
		{
			if (Idle.Frames[I].bIdle) IdleFrame = I + 1;
		}
		TestTrue(TEXT("V3 whiff then Jab: Poke returns to idle"), IdleFrame > 0);
		if (IdleFrame > 0)
		{
			FComboTrial Two = PokeTrial;
			FComboStep JabStep;
			JabStep.State = FNSE025Battle::MoveTag(0);
			JabStep.MoveIndex = 1;
			JabStep.Kind = EComboStepKind::Link;
			JabStep.BeginFrame = IdleFrame;
			JabStep.HitFrame = IdleFrame + 4;
			JabStep.Damage = 300;
			Two.Steps.Add(JabStep);
			Two.TotalDamage = 500;
			Two.Inputs = Tape(IdleFrame + 4, {{1, INP_D}, {IdleFrame, INP_A}});
			B.Reset();
			const FSimResult R2 = Replay(B, Two.Inputs, INP_Neutral, false);
			AddInfo(FString::Printf(TEXT("V3 whiff then Jab: %s"), *R2.ContactsStr()));
			TestEqual(TEXT("V3 whiff then Jab: exactly one hit (the Jab)"), R2.HitCount(), 1);
			const FVerdict J = Judge(R2, Two);
			B.Reset();
			const FVerdict V2 = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, Two, INP_Neutral));
			TestFalse(TEXT("V3 whiff then Jab: not completed"), V2.bCompleted);
			TestEqual(TEXT("V3 whiff then Jab: FailedStep 0"), V2.FailedStep, 0);
			ExpectDamageEither(*this, TEXT("V3 whiff then Jab"), V2.ObservedDamage, 0, R2.HealthLost);
			TestTrue(FString::Printf(TEXT("V3 whiff then Jab: report %s equals judge %s"), *V2.ToString(), *J.ToString()), ReportMatches(V2, J));
		}
	}
	AddInfo(FString::Printf(TEXT("V3 %s"), *StatsStr(Stats, Timer.Seconds())));
	return !HasAnyErrors();
}

// V3b: the air situation after a launcher, the stun-capability oracle and the tech lens.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE025AirSituation, "UnrealBench.NSE025.AirSituation",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE025AirSituation::RunTest(const FString&)
{
	FScopedCounters Timer;
	FNSE025Battle B;
	B.Attacker->CanReverseBeat = false;
	AddReferenceMoves(B);
	B.AddMove(Launcher());
	B.AddMove(Floater());
	B.Start();
	FEnumStats Stats;
	const int32 Tech = FNSE025Battle::TechInput();

	// Situation: the end of the frame Floater's hit registered. The defender is launched and held by
	// the authored untech; once that expires in the air the defender can act by teching.
	FRollbackData SAir;
	{
		B.Reset();
		const FSimResult R = Simulate(B, Tape(9, {{1, INP_F}}), INP_Neutral, 0, false, false);
		AddInfo(FString::Printf(TEXT("V3b Floater timeline: contacts %s"), *R.ContactsStr()));
		TestTrue(TEXT("V3b situation: Floater's hit registers"), R.HitCount() == 1);
		TestTrue(TEXT("V3b situation: the defender cannot act at the situation"), B.Defender->CheckIsStunned());
		SAir = Snapshot(B);
	}

	const FComboSearchRequest ReqN = MakeRequest(B, {0, 1, 3, 5}, 2, 90, 100, FullBudget, INP_Neutral);
	const FComboSearchRequest ReqT = MakeRequest(B, {0, 1, 3, 5}, 2, 90, 100, FullBudget, Tech);
	const TArray<FRouteRec> EN = Enumerate(B, ReqN, true, &Stats);
	const TArray<FRouteRec> ET = Enumerate(B, ReqT, true, &Stats);
	AddInfo(FString::Printf(TEXT("V3b EN: %s"), *ListStr(EN)));
	AddInfo(FString::Printf(TEXT("V3b ET: %s"), *ListStr(ET)));
	TestTrue(TEXT("V3b air situation: juggle routes exist"), EN.Num() > 0);
	// Tech lens, the guard lens's analog: holding the tech button changes nothing while the defender
	// cannot act, so the enumerated domains agree.
	TestTrue(TEXT("V3b tech lens: enumerations under neutral and tech agree"), ListStr(EN) == ListStr(ET));
	const FDiscovery DN = CheckExact(*this, TEXT("V3b air discovery under neutral"), B, ReqN, EN);
	const FDiscovery DT = CheckExact(*this, TEXT("V3b air discovery under tech"), B, ReqT, ET);
	for (int32 Opp : {static_cast<int32>(INP_Neutral), Tech})
	{
		for (int32 I = 0; I < DN.Result.Trials.Num(); ++I)
		{
			const FComboTrial& T = DN.Result.Trials[I];
			const FString Label = FString::Printf(TEXT("V3b trial #%d under opponent input %d"), I, Opp);
			Restore(B, SAir);
			CheckTrialReplays(*this, Label, B, T, Opp, true);
			Restore(B, SAir);
			const FVerdict V = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, T, Opp));
			TestTrue(Label + TEXT(": validates"), V.bCompleted && V.FailedStep == -1);
			TestEqual(Label + TEXT(": ObservedDamage equals the total"), V.ObservedDamage, T.TotalDamage);
		}
	}

	// The floating situation: the defender's untech has expired in the air, so it could act and no hit
	// continues. A delayed juggle lands but is an escape; with the tech input held the defender techs
	// out before the hit.
	int32 FloatFrame = -1;
	{
		Restore(B, SAir);
		for (int32 F = 1; F <= 60 && FloatFrame < 0; ++F)
		{
			StepBattle(B, INP_Neutral, INP_Neutral);
			const FFrameObs O = Observe(B);
			if (CouldAct(O)) FloatFrame = F;
		}
		TestTrue(TEXT("V3b float: the defender could act while airborne"), FloatFrame > 0 && B.Defender->PosY > 0);
	}
	if (FloatFrame > 0)
	{
		AddInfo(FString::Printf(TEXT("V3b float: could-act frame %d"), FloatFrame));
		FRollbackData SFloat = Snapshot(B);
		const FComboSearchRequest ReqDel = MakeRequest(B, {0, 1, 3, 5}, 2, 60, 100, FullBudget, INP_Neutral);
		ExpectEmpty(*this, TEXT("V3b floating situation: no routes under neutral"), B, ReqDel, EComplete::Yes);
		FComboSearchRequest ReqDelT = ReqDel;
		ReqDelT.OpponentInput = Tech;
		ExpectEmpty(*this, TEXT("V3b floating situation: no routes under tech"), B, ReqDelT, EComplete::Yes);
		const TArray<int32> LateTape = Tape(1, {{1, INP_A}});
		Restore(B, SFloat);
		const FSimResult RN = Simulate(B, LateTape, INP_Neutral, 30, false, true);
		AddInfo(FString::Printf(TEXT("V3b delayed Jab under neutral: %s"), *RN.ContactsStr()));
		TestEqual(TEXT("V3b delayed Jab under neutral: one hit"), RN.HitCount(), 1);
		bool bNeutralTeched = false;
		for (const FEntry& E : RN.DefEntries) bNeutralTeched |= E.State == FNSE025Battle::TechTag();
		TestFalse(TEXT("V3b delayed Jab under neutral: the defender never techs"), bNeutralTeched);
		if (RN.HitCount() == 1)
		{
			TestFalse(TEXT("V3b delayed Jab under neutral: the hit is an escape"), RN.Contacts[0].bContinues);
		}
		Restore(B, SFloat);
		const FSimResult RT = Simulate(B, LateTape, Tech, 30, false, true);
		AddInfo(FString::Printf(TEXT("V3b delayed Jab under tech: %s"), *RT.ContactsStr()));
		bool bTechTeched = false;
		int32 TechFrame = -1;
		for (const FEntry& E : RT.DefEntries)
		{
			if (E.State == FNSE025Battle::TechTag() && TechFrame < 0) TechFrame = E.Frame;
			bTechTeched |= E.State == FNSE025Battle::TechTag();
		}
		TestTrue(TEXT("V3b delayed Jab under tech: the defender techs"), bTechTeched);
		if (RT.HitCount() == 1 && bTechTeched)
		{
			TestTrue(TEXT("V3b delayed Jab under tech: the tech precedes the hit"),
					 TechFrame <= RT.Contacts[0].Frame);
			TestFalse(TEXT("V3b delayed Jab under tech: the hit does not continue"), RT.Contacts[0].bContinues);
		}
	}

	// Integrity from the air situation: discovery under the tech input leaves the battle untouched.
	CheckIntegrity(*this, TEXT("V3b integrity after air discovery under tech"), B, SAir,
				   [&]() { UComboDiscovery::DiscoverCombos(B.Game, ReqT); });
	if (DN.Result.Trials.Num() > 0)
	{
		const FComboTrial T = DN.Result.Trials[0];
		CheckIntegrity(*this, TEXT("V3b integrity after air validation under neutral"), B, SAir,
					   [&]() { UComboDiscovery::ValidateTrial(B.Game, T, INP_Neutral); });
	}
	AddInfo(FString::Printf(TEXT("V3b %s"), *StatsStr(Stats, Timer.Seconds())));
	return !HasAnyErrors();
}

// V4: distance, pushback, the corner and mirroring all come from the live simulation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE025SpacingAndCorner, "UnrealBench.NSE025.SpacingAndCorner",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE025SpacingAndCorner::RunTest(const FString&)
{
	FScopedCounters Timer;
	FNSE025Battle B;
	B.Attacker->CanReverseBeat = false;
	AddReferenceMoves(B);
	FEnumStats Stats;
	const FComboSearchRequest Req = MakeRequest(B, {0, 1, 2, 3}, 2, 60, 100);

	auto CountPokeFirst = [](const TArray<FRouteRec>& E)
	{
		int32 N = 0;
		for (const FRouteRec& R : E) N += R.Steps[0].MoveIndex == 3 ? 1 : 0;
		return N;
	};

	// Distance 1: the hurtbox's front edge is 100000 from the attacker.
	B.Start(-200000, -50000);
	{
		const FSimResult P = Simulate(B, Tape(1, {{1, INP_D}}), INP_Neutral, 20, false, false);
		TestEqual(TEXT("V4 fixture distance 1: Poke hits"), P.HitCount(), 1);
		B.Reset();
		const FSimResult J = Simulate(B, Tape(1, {{1, INP_A}}), INP_Neutral, 20, false, false);
		TestEqual(TEXT("V4 fixture distance 1: Jab hits"), J.HitCount(), 1);
		B.Reset();
	}
	const TArray<FRouteRec> ED1 = Enumerate(B, Req, false, &Stats);
	AddInfo(FString::Printf(TEXT("V4 ED1: %s"), *ListStr(ED1)));
	TestTrue(TEXT("V4 ED1: Poke-first routes exist at distance 1"), CountPokeFirst(ED1) > 0);
	TestTrue(TEXT("V4 ED1: [Jab, Strong cancel] present"), IndexOfShape(ED1, {{0, false}, {1, true}}) >= 0);
	const int32 JabPoke = IndexOfShape(ED1, {{0, false}, {3, false}});
	TestTrue(TEXT("V4 ED1: [Jab, Poke link] present"), JabPoke >= 0);
	const FDiscovery DD1 = CheckExact(*this, TEXT("V4 distance 1"), B, Req, ED1);

	// Distance 2: the front edge is 150000 away, beyond Poke's reach and inside Jab's.
	B.Start(-200000, 0);
	{
		const FSimResult P = Simulate(B, Tape(1, {{1, INP_D}}), INP_Neutral, 30, false, false);
		TestEqual(TEXT("V4 fixture distance 2: Poke whiffs"), P.Contacts.Num(), 0);
		B.Reset();
		const FSimResult J = Simulate(B, Tape(1, {{1, INP_A}}), INP_Neutral, 20, false, false);
		TestEqual(TEXT("V4 fixture distance 2: Jab hits"), J.HitCount(), 1);
		B.Reset();
	}
	const TArray<FRouteRec> ED2 = Enumerate(B, Req, false, &Stats);
	AddInfo(FString::Printf(TEXT("V4 ED2: %s"), *ListStr(ED2)));
	TestEqual(TEXT("V4 ED2: no Poke-first route at distance 2"), CountPokeFirst(ED2), 0);
	TestTrue(TEXT("V4 ED2: [Jab, Strong cancel] present"), IndexOfShape(ED2, {{0, false}, {1, true}}) >= 0);
	TestTrue(TEXT("V4: the two distances give different route lists"), ListStr(ED1) != ListStr(ED2));
	CheckExact(*this, TEXT("V4 distance 2"), B, Req, ED2);

	// Pushback: Jab now pushes the defender out of Poke's reach.
	{
		auto PushJab = Jab();
		PushJab.Pushback = 40000;
		B.SetMove(0, PushJab);
	}
	B.Start(-200000, -50000);
	if (JabPoke >= 0)
	{
		const FSimResult R = Simulate(B, ED1[JabPoke].Inputs, INP_Neutral, 20, false, false);
		AddInfo(FString::Printf(TEXT("V4 pushback replay of [Jab, Poke link]: %s"), *R.ContactsStr()));
		TestTrue(TEXT("V4 fixture pushback: the linked Poke still begins"), R.BeganAt(ED1[JabPoke].Steps[1].Begin, FNSE025Battle::MoveTag(3)));
		TestEqual(TEXT("V4 fixture pushback: only the Jab connects"), R.HitCount(), 1);
		B.Reset();
	}
	const TArray<FRouteRec> EP = Enumerate(B, Req, false, &Stats);
	AddInfo(FString::Printf(TEXT("V4 EP (pushback): %s"), *ListStr(EP)));
	TestEqual(TEXT("V4 EP: [Jab, Poke link] absent with pushback"), IndexOfShape(EP, {{0, false}, {3, false}}), -1);
	CheckExact(*this, TEXT("V4 pushback"), B, Req, EP);

	// Corner: the defender is pinned on the wall and the pushback transfers to the attacker.
	B.Start(CornerX - 150000, CornerX);
	{
		const FSimResult R = Simulate(B, Tape(1, {{1, INP_A}}), INP_Neutral, 20, false, false);
		FString Trace;
		for (int32 I = 0; I < R.Frames.Num(); ++I) Trace += FString::Printf(TEXT("%d:%d/%d "), I + 1, R.Frames[I].AttX, R.Frames[I].DefX);
		AddInfo(FString::Printf(TEXT("V4 corner replay of [Jab] (frame:attackerX/defenderX): %s"), *Trace));
		TestEqual(TEXT("V4 fixture corner: Jab hits"), R.HitCount(), 1);
		bool bDefenderPinned = true;
		int32 MinAttX = CornerX - 150000;
		for (const FFrameObs& O : R.Frames)
		{
			bDefenderPinned &= O.DefX == CornerX;
			MinAttX = FMath::Min(MinAttX, O.AttX);
		}
		TestTrue(TEXT("V4 fixture corner: the defender stays on the wall at 3115000"), bDefenderPinned);
		TestTrue(TEXT("V4 fixture corner: the attacker slides back"), MinAttX < CornerX - 150000);
		B.Reset();
	}
	const TArray<FRouteRec> EC = Enumerate(B, Req, false, &Stats);
	AddInfo(FString::Printf(TEXT("V4 EC (corner): %s"), *ListStr(EC)));
	TestTrue(TEXT("V4 EC: the corner allows at least one route"), EC.Num() > 0);
	CheckExact(*this, TEXT("V4 corner"), B, Req, EC);

	// Mirror of distance 1: identical steps, damages and tapes.
	B.SetMove(0, Jab());
	B.Start(200000, 50000);
	TestTrue(TEXT("V4 mirror: attacker faces left"), B.Attacker->Direction == DIR_Left);
	const TArray<FRouteRec> EM = Enumerate(B, Req, false, &Stats);
	TestTrue(TEXT("V4 mirror: enumerator lists agree with distance 1"), ListStr(EM) == ListStr(ED1));
	const FDiscovery DM = CheckExact(*this, TEXT("V4 mirror"), B, Req, ED1);
	TestTrue(TEXT("V4 mirror: discovery lists agree with distance 1"), TrialListStr(DM.Result.Trials) == TrialListStr(DD1.Result.Trials));
	AddInfo(FString::Printf(TEXT("V4 %s"), *StatsStr(Stats, Timer.Seconds())));
	return !HasAnyErrors();
}

// V5: ranking keys, prefixes, MaxResults, budgets and request identity.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE025RankingAndCompleteness, "UnrealBench.NSE025.RankingAndCompleteness",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE025RankingAndCompleteness::RunTest(const FString&)
{
	FScopedCounters Timer;
	FNSE025Battle B;
	B.Attacker->CanReverseBeat = false;
	AddReferenceMoves(B);
	B.AddMove(JabCopy());
	B.AddMove(Slow());
	B.AddMove(Dummy(INP_G));
	B.AddMove(Dummy(INP_H));
	B.Start();
	FEnumStats Stats;

	// Main domain.
	const FComboSearchRequest Req6 = MakeRequest(B, {0, 1, 2, 3}, 3, 90, 6);
	FComboSearchRequest Req50 = Req6;
	Req50.MaxResults = 50;
	const TArray<FRouteRec> EMain = Enumerate(B, Req6, false, &Stats);
	AddInfo(FString::Printf(TEXT("V5 EMain (%d routes): %s"), EMain.Num(), *ListStr(EMain)));
	TestTrue(TEXT("V5 EMain: more than six routes so MaxResults 6 truncates"), EMain.Num() > 6);
	TestTrue(TEXT("V5 EMain: fewer than fifty routes so MaxResults 50 returns the whole domain"), EMain.Num() < 50);
	{
		const int32 JabOnly = IndexOfShape(EMain, {{0, false}});
		const int32 JabStrong = IndexOfShape(EMain, {{0, false}, {1, true}});
		TestTrue(TEXT("V5 EMain: prefix route [Jab] present"), JabOnly >= 0);
		TestTrue(TEXT("V5 EMain: [Jab] ranks below [Jab, Strong cancel]"), JabOnly > JabStrong && JabStrong >= 0);
	}
	CheckExact(*this, TEXT("V5 MaxResults 6"), B, Req6, EMain);
	const FDiscovery D50 = CheckExact(*this, TEXT("V5 MaxResults 50"), B, Req50, EMain);
	{
		const int32 JabOnly = IndexOfShape(D50.Result.Trials, {{0, false}});
		const int32 JabStrong = IndexOfShape(D50.Result.Trials, {{0, false}, {1, true}});
		TestTrue(TEXT("V5 discovery: prefix route [Jab] returned below [Jab, Strong cancel]"), JabOnly >= 0 && JabStrong >= 0 && JabOnly > JabStrong);
	}

	// Tie (a): index key. Slot 4 is a copy of Jab on another button.
	{
		const FComboSearchRequest ReqA = MakeRequest(B, {0, 4}, 1, 60, 10);
		const TArray<FRouteRec> EA = Enumerate(B, ReqA, false, &Stats);
		TestEqual(TEXT("V5 tie (a): two single-step routes"), EA.Num(), 2);
		if (EA.Num() == 2)
		{
			TestTrue(TEXT("V5 tie (a): equal damage and last hit"), EA[0].Total == EA[1].Total && EA[0].LastHit() == EA[1].LastHit());
			TestEqual(TEXT("V5 tie (a): move index 0 first"), EA[0].Steps[0].MoveIndex, 0);
		}
		const FDiscovery DA = CheckExact(*this, TEXT("V5 tie (a) [Jab, JabCopy]"), B, ReqA, EA);
		if (DA.Result.Trials.Num() == 2)
		{
			TestEqual(TEXT("V5 tie (a): discovery returns move index 0 first"), DA.Result.Trials[0].Steps[0].MoveIndex, 0);
		}
		const FComboSearchRequest ReqR = MakeRequest(B, {4, 0}, 1, 60, 10);
		const TArray<FRouteRec> ER = Enumerate(B, ReqR, false, &Stats);
		const FDiscovery DR = CheckExact(*this, TEXT("V5 tie (a) [JabCopy, Jab]"), B, ReqR, ER);
		if (DR.Result.Trials.Num() == 2)
		{
			TestTrue(TEXT("V5 tie (a) reversed: the copy (now index 0) comes first"),
					 DR.Result.Trials[0].Steps[0].MoveIndex == 0 && DR.Result.Trials[0].Steps[0].State == FNSE025Battle::MoveTag(4));
		}
	}

	// Tie (c): steps key. Slot 5 ties [Poke, Poke cancel] on damage and hits later.
	{
		const FComboSearchRequest ReqC = MakeRequest(B, {3, 5}, 2, 60, 10);
		const TArray<FRouteRec> EC = Enumerate(B, ReqC, false, &Stats);
		AddInfo(FString::Printf(TEXT("V5 EC (tie c): %s"), *ListStr(EC)));
		const int32 SlowOnly = IndexOfShape(EC, {{1, false}});
		const int32 PokePoke = IndexOfShape(EC, {{0, false}, {0, true}});
		TestTrue(TEXT("V5 tie (c): [Slow] and [Poke, Poke cancel] both present"), SlowOnly >= 0 && PokePoke >= 0);
		if (SlowOnly >= 0 && PokePoke >= 0)
		{
			TestEqual(TEXT("V5 tie (c): equal totals"), EC[SlowOnly].Total, EC[PokePoke].Total);
			TestTrue(TEXT("V5 tie (c): [Slow] hits later"), EC[SlowOnly].LastHit() > EC[PokePoke].LastHit());
			TestTrue(TEXT("V5 tie (c): [Slow] ranks first by fewer steps"), SlowOnly < PokePoke);
		}
		const FDiscovery DC = CheckExact(*this, TEXT("V5 tie (c)"), B, ReqC, EC);
		const int32 DSlow = IndexOfShape(DC.Result.Trials, {{1, false}});
		const int32 DPoke = IndexOfShape(DC.Result.Trials, {{0, false}, {0, true}});
		TestTrue(TEXT("V5 tie (c): discovery ranks [Slow] before [Poke, Poke cancel]"), DSlow >= 0 && DPoke >= 0 && DSlow < DPoke);
	}

	// Tie (b): kind key. A Poke with hitstun 12 allows cancel-then-link and link-then-cancel with the same last hit.
	{
		auto LongPoke = Poke();
		LongPoke.Hitstun = 12;
		B.SetMove(3, LongPoke);
		B.Start();
		const FComboSearchRequest ReqB = MakeRequest(B, {3}, 3, 90, 20);
		const TArray<FRouteRec> EB = Enumerate(B, ReqB, false, &Stats);
		AddInfo(FString::Printf(TEXT("V5 EB (tie b): %s"), *ListStr(EB)));
		const int32 CL = IndexOfShape(EB, {{0, false}, {0, true}, {0, false}});
		const int32 LC = IndexOfShape(EB, {{0, false}, {0, false}, {0, true}});
		TestTrue(TEXT("V5 tie (b): cancel-then-link and link-then-cancel both present"), CL >= 0 && LC >= 0);
		if (CL >= 0 && LC >= 0)
		{
			TestEqual(TEXT("V5 tie (b): equal totals"), EB[CL].Total, EB[LC].Total);
			TestEqual(TEXT("V5 tie (b): equal last hit"), EB[CL].LastHit(), EB[LC].LastHit());
			TestTrue(TEXT("V5 tie (b): cancel-first ranks first"), CL < LC);
		}
		const FDiscovery DB = CheckExact(*this, TEXT("V5 tie (b)"), B, ReqB, EB);
		const int32 DCL = IndexOfShape(DB.Result.Trials, {{0, false}, {0, true}, {0, false}});
		const int32 DLC = IndexOfShape(DB.Result.Trials, {{0, false}, {0, false}, {0, true}});
		TestTrue(TEXT("V5 tie (b): discovery ranks cancel-first before link-first"), DCL >= 0 && DLC >= 0 && DCL < DLC);
		B.SetMove(3, Poke());
		B.Start();
	}

	// Budgets: one-directional properties, then more budget never worsens a filled rank.
	const TArray<int32> BudgetValues = {1, 50, 200, 500, 1500, 4000};
	TArray<FComboSearchResult> BudgetResults;
	for (int32 Budget : BudgetValues)
	{
		FComboSearchRequest ReqBudget = Req50;
		ReqBudget.MaxSimulatedFrames = Budget;
		const FDiscovery D = Discover(B, ReqBudget);
		const FString Label = FString::Printf(TEXT("V5 budget %d"), Budget);
		TestTrue(FString::Printf(TEXT("%s: LocalFrame delta %d within budget"), *Label, D.FrameDelta), D.FrameDelta <= Budget);
		int32 Prev = -1;
		bool bOrdered = true;
		bool bAllValid = true;
		for (const FComboTrial& T : D.Result.Trials)
		{
			const int32 Index = IndexOfTrial(EMain, T, ReqBudget);
			if (Index < 0)
			{
				bAllValid = false;
				AddError(FString::Printf(TEXT("%s: returned trial is not a valid route: %s"), *Label, *TrialStr(T)));
				break;
			}
			if (Index <= Prev) bOrdered = false;
			Prev = Index;
		}
		TestTrue(Label + TEXT(": every returned trial is a valid route"), bAllValid);
		TestTrue(Label + TEXT(": returned trials keep the ranking's relative order"), bOrdered);
		bool bFullPrefix = D.Result.Trials.Num() == EMain.Num();
		for (int32 I = 0; bFullPrefix && I < EMain.Num(); ++I) bFullPrefix = TrialMatches(D.Result.Trials[I], EMain[I], ReqBudget);
		if (D.Result.bComplete) TestTrue(Label + TEXT(": bComplete only with the full ranking"), bFullPrefix);
		if (Budget == 1)
		{
			TestEqual(Label + TEXT(": no trials"), D.Result.Trials.Num(), 0);
			TestFalse(Label + TEXT(": incomplete"), D.Result.bComplete);
		}
		AddInfo(FString::Printf(TEXT("%s: %d trials, complete=%d, delta=%d"), *Label, D.Result.Trials.Num(), D.Result.bComplete ? 1 : 0, D.FrameDelta));
		BudgetResults.Add(D.Result);
	}
	for (int32 K = 1; K < BudgetResults.Num(); ++K)
	{
		const FComboSearchResult& Less = BudgetResults[K - 1];
		const FComboSearchResult& More = BudgetResults[K];
		const FString Label = FString::Printf(TEXT("V5 budget %d versus %d"), BudgetValues[K - 1], BudgetValues[K]);
		TestTrue(FString::Printf(TEXT("%s: at least as many trials (%d versus %d)"), *Label, More.Trials.Num(), Less.Trials.Num()),
				 More.Trials.Num() >= Less.Trials.Num());
		for (int32 Rank = 0; Rank < FMath::Min(Less.Trials.Num(), More.Trials.Num()); ++Rank)
		{
			const int32 IL = IndexOfTrial(EMain, Less.Trials[Rank], Req50);
			const int32 IM = IndexOfTrial(EMain, More.Trials[Rank], Req50);
			if (IL < 0 || IM < 0) continue;
			TestTrue(FString::Printf(TEXT("%s: rank %d is no worse with more budget (ranking index %d versus %d)"), *Label, Rank, IM, IL), IM <= IL);
		}
	}

	// Request identity: indices are positions in the request.
	{
		const FComboSearchRequest ReqRe = MakeRequest(B, {3, 1, 0, 2}, 3, 90, 50);
		const TArray<FRouteRec> ERe = Enumerate(B, ReqRe, false, &Stats);
		TestEqual(TEXT("V5 reordered request: same number of routes"), ERe.Num(), EMain.Num());
		TArray<FString> TapesMain, TapesRe;
		for (const FRouteRec& R : EMain) TapesMain.Add(TapeStr(R.Inputs) + FString::FromInt(R.Total));
		for (const FRouteRec& R : ERe) TapesRe.Add(TapeStr(R.Inputs) + FString::FromInt(R.Total));
		TapesMain.Sort();
		TapesRe.Sort();
		TestTrue(TEXT("V5 reordered request: same tapes and totals"), TapesMain == TapesRe);
		const int32 PokeFirst = IndexOfShape(ERe, {{0, false}});
		TestTrue(TEXT("V5 reordered request: [Poke] has move index 0"), PokeFirst >= 0 && ERe[PokeFirst].Inputs.Num() > 0 && ERe[PokeFirst].Inputs[0] == INP_D);
		CheckExact(*this, TEXT("V5 reordered request [Poke, Strong, Jab, Special]"), B, ReqRe, ERe);
	}
	{
		const FComboSearchRequest ReqS = MakeRequest(B, {1, 2}, 3, 90, 50);
		const TArray<FRouteRec> ES = Enumerate(B, ReqS, false, &Stats);
		TestTrue(TEXT("V5 sparse request: [Strong, Special cancel] present"), IndexOfShape(ES, {{0, false}, {1, true}}) >= 0);
		CheckExact(*this, TEXT("V5 sparse request [Strong, Special]"), B, ReqS, ES);
	}
	{
		FComboSearchRequest ReqWrong = MakeRequest(B, {0}, 2, 60, 10);
		ReqWrong.Moves[0].Input = INP_D;
		ExpectEmpty(*this, TEXT("V5 Jab's tag with Poke's button"), B, ReqWrong, EComplete::Any);
		FComboSearchRequest ReqPokeD = MakeRequest(B, {3}, 2, 60, 10);
		const TArray<FRouteRec> EPD = Enumerate(B, ReqPokeD, false, &Stats);
		TestTrue(TEXT("V5 Poke's tag with D: routes exist"), EPD.Num() > 0);
		CheckExact(*this, TEXT("V5 Poke's tag with D"), B, ReqPokeD, EPD);
	}
	AddInfo(FString::Printf(TEXT("V5 %s"), *StatsStr(Stats, Timer.Seconds())));
	return !HasAnyErrors();
}

// V6: trial tapes replay, validation reads only State, Damage and the tape, integrity from idle.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE025TrialsAndIntegrity, "UnrealBench.NSE025.TrialsAndIntegrity",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE025TrialsAndIntegrity::RunTest(const FString&)
{
	FScopedCounters Timer;
	FNSE025Battle B;
	B.Attacker->CanReverseBeat = false;
	AddReferenceMoves(B);
	B.AddMove(JabCopy());
	B.AddMove(MultiJab());
	B.Start();
	FEnumStats Stats;
	FRollbackData S = Snapshot(B);

	const FComboSearchRequest Req = MakeRequest(B, {0, 1, 2, 3}, 2, 60, 100);
	const TArray<FRouteRec> E = Enumerate(B, Req, false, &Stats);
	const FDiscovery D = CheckExact(*this, TEXT("V6 discovery"), B, Req, E);
	for (int32 I = 0; I < D.Result.Trials.Num(); ++I)
	{
		const FComboTrial& T = D.Result.Trials[I];
		const FString Label = FString::Printf(TEXT("V6 trial #%d %s"), I, *TrialStr(T));
		CheckTape(*this, Label, T, Req);
		Restore(B, S);
		CheckTrialReplays(*this, Label, B, T, INP_Neutral, false);
		Restore(B, S);
		const FVerdict V = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, T, INP_Neutral));
		TestTrue(Label + TEXT(": validates"), V.bCompleted);
		TestEqual(Label + TEXT(": FailedStep -1"), V.FailedStep, -1);
		TestEqual(Label + TEXT(": ObservedDamage equals TotalDamage"), V.ObservedDamage, T.TotalDamage);
	}

	// Isolated validator checks on the enumerator's [Jab, Strong cancel] record.
	const int32 JS = IndexOfShape(E, {{0, false}, {1, true}});
	TestTrue(TEXT("V6: enumerator has [Jab, Strong cancel]"), JS >= 0);
	if (JS >= 0)
	{
		const FComboTrial Base = ToTrial(E[JS], Req);
		const int32 D0 = Base.Steps[0].Damage;
		const int32 D1 = Base.Steps[1].Damage;
		auto Validate = [&](const FComboTrial& T)
		{
			Restore(B, S);
			return ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, T, INP_Neutral));
		};
		{
			FComboTrial T = Base;
			T.Steps[1].Damage += 1;
			const FVerdict V = Validate(T);
			TestFalse(TEXT("V6 (a) step 1 damage +1: not completed"), V.bCompleted);
			TestEqual(TEXT("V6 (a) step 1 damage +1: FailedStep 1"), V.FailedStep, 1);
			ExpectDamageEither(*this, TEXT("V6 (a) step 1 damage +1"), V.ObservedDamage, D0, D0 + D1);
		}
		{
			FComboTrial T = Base;
			T.Steps[0].Damage -= 1;
			const FVerdict V = Validate(T);
			TestFalse(TEXT("V6 (b) step 0 damage -1: not completed"), V.bCompleted);
			TestEqual(TEXT("V6 (b) step 0 damage -1: FailedStep 0"), V.FailedStep, 0);
			ExpectDamageEither(*this, TEXT("V6 (b) step 0 damage -1 (the later Strong contact never counts)"), V.ObservedDamage, 0, D0);
		}
		{
			FComboTrial T = Base;
			T.Steps[0].State = FNSE025Battle::MoveTag(4);
			const FVerdict V = Validate(T);
			TestFalse(TEXT("V6 (c) step 0 state replaced by the equal-damage copy: not completed"), V.bCompleted);
			TestEqual(TEXT("V6 (c) step 0 state replaced: FailedStep 0"), V.FailedStep, 0);
			ExpectDamageEither(*this, TEXT("V6 (c) step 0 state replaced"), V.ObservedDamage, 0, D0);
		}
		{
			FComboTrial T = Base;
			for (FComboStep& St : T.Steps)
			{
				St.BeginFrame += 7;
				St.HitFrame -= 3;
				St.Kind = St.Kind == EComboStepKind::Cancel ? EComboStepKind::Link : EComboStepKind::Cancel;
				St.MoveIndex = 3 - St.MoveIndex;
			}
			T.TotalDamage = 1;
			const FVerdict V = Validate(T);
			TestTrue(TEXT("V6 (d) wrong frames, kinds, indices and total: completed"), V.bCompleted);
			TestEqual(TEXT("V6 (d) wrong metadata: FailedStep -1"), V.FailedStep, -1);
			TestEqual(TEXT("V6 (d) wrong metadata: ObservedDamage"), V.ObservedDamage, D0 + D1);
		}
	}
	// A multi-hit step: validation pairs the move's whole contact group with the one step.
	{
		const FComboSearchRequest ReqM = MakeRequest(B, {5}, 1, 60, 10);
		const TArray<FRouteRec> EM = Enumerate(B, ReqM, false, &Stats);
		TestEqual(TEXT("V6 MultiJab: enumerator finds one route"), EM.Num(), 1);
		if (EM.Num() == 1)
		{
			const FComboTrial T = ToTrial(EM[0], ReqM);
			Restore(B, S);
			CheckTrialReplays(*this, TEXT("V6 MultiJab trial"), B, T, INP_Neutral, false);
			Restore(B, S);
			const FVerdict V = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, T, INP_Neutral));
			TestTrue(TEXT("V6 MultiJab: validates with FailedStep -1"), V.bCompleted && V.FailedStep == -1);
			TestEqual(TEXT("V6 MultiJab: ObservedDamage equals the group sum"), V.ObservedDamage, T.TotalDamage);
			// The group's contacts after the tape must resolve before the trial can complete: a tape
			// truncated to the first contact still judges by the whole group.
			FComboTrial Trunc = T;
			Trunc.Inputs.SetNum(T.Steps[0].HitFrame);
			Restore(B, S);
			const FSimResult R = Replay(B, Trunc.Inputs, INP_Neutral, false);
			const FVerdict J = Judge(R, Trunc);
			TestTrue(TEXT("V6 MultiJab truncated tape: judge completes"), J.bCompleted);
			Restore(B, S);
			const FVerdict V2 = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, Trunc, INP_Neutral));
			TestTrue(FString::Printf(TEXT("V6 MultiJab truncated tape: report %s equals judge %s (contacts %s)"),
									  *V2.ToString(), *J.ToString(), *R.ContactsStr()), ReportMatches(V2, J));
		}
	}
	// Integrity from idle.
	CheckIntegrity(*this, TEXT("V6 integrity after DiscoverCombos"), B, S, [&]() { UComboDiscovery::DiscoverCombos(B.Game, Req); });
	if (JS >= 0)
	{
		const FComboTrial Base = ToTrial(E[JS], Req);
		CheckIntegrity(*this, TEXT("V6 integrity after ValidateTrial"), B, S, [&]() { UComboDiscovery::ValidateTrial(B.Game, Base, INP_Neutral); });
		// Interrupted and failed calls, interleaved: a search that runs out of budget, a validation that fails after a
		// real contact, then a full search, with nothing restored in between.
		FComboTrial Wrong = Base;
		Wrong.Steps[0].Damage += 1;
		FComboSearchRequest Small = Req;
		Small.MaxSimulatedFrames = 40;
		CheckIntegrity(*this, TEXT("V6 integrity after an interrupted search, a failed validation and a full search"), B, S, [&]()
		{
			UComboDiscovery::DiscoverCombos(B.Game, Small);
			UComboDiscovery::ValidateTrial(B.Game, Wrong, INP_Neutral);
			UComboDiscovery::DiscoverCombos(B.Game, Req);
		});
	}
	AddInfo(FString::Printf(TEXT("V6 %s"), *StatsStr(Stats, Timer.Seconds())));
	return !HasAnyErrors();
}

// V7: seeded random domains against the enumerator, with metamorphic relations and validator checks.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE025SeededDomains, "UnrealBench.NSE025.SeededDomains",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE025SeededDomains::RunTest(const FString&)
{
	FScopedCounters Timer;
	const int32 Seed = GetSeed(2501);
	const int32 Iterations = GetIterations(6);
	constexpr int32 MinEpisodes = 2;
	FRandomStream Rand(Seed);
	TMap<FString, int32> Counts;

	FNSE025Battle B;
	B.Attacker->CanReverseBeat = false;
	for (int32 I = 0; I < FNSE025Battle::MaxMoves; ++I) B.AddMove(Dummy(Buttons[I]));
	B.Start();
	FEnumStats Stats;

	auto Pick = [&](const TArray<int32>& Values) { return Values[Rand.RandRange(0, Values.Num() - 1)]; };

	int32 Completed = 0;
	for (int32 Episode = 0; Episode < Iterations; ++Episode)
	{
		if (Timer.Seconds() > TestDeadlineSeconds)
		{
			AddInfo(FString::Printf(TEXT("V7 deadline reached after %d episodes"), Completed));
			break;
		}
		// Move set.
		const int32 NumMoves = Rand.RandRange(2, 3);
		TArray<int32> Order;
		for (int32 I = 0; I < FNSE025Battle::MaxMoves; ++I) Order.Add(I);
		for (int32 I = Order.Num() - 1; I > 0; --I) Order.Swap(I, Rand.RandRange(0, I));
		TArray<FNSE025MoveSpec> Specs;
		for (int32 I = 0; I < NumMoves; ++I)
		{
			FNSE025MoveSpec S = MakeMove(Buttons[Order[I]], Rand.RandRange(3, 10), Rand.RandRange(1, 3), Rand.RandRange(4, 14),
										 Pick({120000, 160000, 200000, 260000}), 100 + 50 * Rand.RandRange(0, 16),
										 Rand.RandRange(8, 24), Rand.RandRange(2, 8), Pick({60, 80, 90, 100}), Pick({-1, 1, 2}), {});
			S.Pushback = Pick({0, 10000, 20000, 30000, 40000});
			S.InitialProration = Pick({70, 100});
			S.MinimumDamagePercent = Pick({0, 20});
			S.bReverseBeat = Rand.RandRange(0, 1) == 1;
			S.CancelFrom = Pick({0, S.Startup + 2, S.Startup + 5});
			// Air authoring: half the moves carry an air hit action and a launch arc.
			if (Rand.RandRange(0, 1) == 1)
			{
				switch (Rand.RandRange(0, 4))
				{
				case 0: S.AirHitAction = HACT_AirNormal; break;
				case 1: S.AirHitAction = HACT_AirVertical; break;
				case 2: S.AirHitAction = HACT_AirFaceDown; break;
				case 3: S.AirHitAction = HACT_Tailspin; break;
				default: S.AirHitAction = HACT_Blowback; break;
			}
				S.AirPushbackY = Pick({4000, 6000, 8000, 10000});
				S.Gravity = Pick({300, 500, 800, 1200});
				S.Untech = Rand.RandRange(16, 40);
				Bump(Counts, TEXT("bias.air"));
			}
			// Multi-hit: re-arm inside the active window.
			if (S.Active >= 5 && Rand.RandRange(0, 2) == 0)
			{
				S.MultiHit = Rand.RandRange(S.Startup + 2, S.Startup + S.Active - 1);
				Bump(Counts, TEXT("bias.multihit"));
			}
			Specs.Add(S);
		}
		// Launcher bias: one move always launches with a long hold.
		{
			FNSE025MoveSpec& S = Specs[Rand.RandRange(0, NumMoves - 1)];
			S.AirHitAction = HACT_AirNormal;
			S.AirPushbackY = Pick({6000, 8000});
			S.Gravity = Pick({300, 500});
			S.Untech = Rand.RandRange(24, 40);
		}
		Bump(Counts, TEXT("bias.launcher"));
		for (int32 I = 0; I < NumMoves; ++I)
		{
			for (int32 J = 0; J < NumMoves; ++J)
			{
				if (Rand.RandRange(0, 2) != 0) Specs[I].ChainCancels.Add(J);
			}
			// Boundary bias: hitstun around the frame a cancelled follow-up lands.
			if (Rand.RandRange(0, 2) == 0)
			{
				const int32 Next = (I + 1) % NumMoves;
				Specs[I].Hitstun = FMath::Clamp(Specs[Next].Startup + Rand.RandRange(0, 4), 8, 24);
				Bump(Counts, TEXT("bias.hitstun"));
			}
		}
		const bool bCorner = Rand.RandRange(0, 3) == 0;
		const int32 Edge = Pick({100000, 130000, 160000, 200000});
		if (Rand.RandRange(0, 3) == 0)
		{
			Specs[Rand.RandRange(0, NumMoves - 1)].Reach = Edge;
			Bump(Counts, TEXT("bias.reach"));
		}
		const int32 AttackerX = bCorner ? CornerX - 50000 - Edge : -200000;
		const int32 DefenderX = bCorner ? CornerX : -200000 + 50000 + Edge;
		const bool bCanReverseBeat = Rand.RandRange(0, 1) == 1;
		const int32 MaxSteps = 2;
		const int32 MaxFrames = Pick({70, 90});

		B.Attacker->CanReverseBeat = bCanReverseBeat;
		for (int32 I = 0; I < FNSE025Battle::MaxMoves; ++I) B.SetMove(I, I < NumMoves ? Specs[I] : Dummy(Buttons[Order[I]]));
		B.Start(AttackerX, DefenderX);
		FRollbackData S = Snapshot(B);
		TArray<int32> Slots;
		for (int32 I = 0; I < NumMoves; ++I) Slots.Add(I);
		const FComboSearchRequest Req = MakeRequest(B, Slots, MaxSteps, MaxFrames, 6);
		FString Desc = FString::Printf(TEXT("seed %d episode %d: corner=%d edge=%d reverse=%d steps=%d frames=%d moves="), Seed,
									   Episode, bCorner ? 1 : 0, Edge, bCanReverseBeat ? 1 : 0, MaxSteps, MaxFrames);
		for (const FNSE025MoveSpec& S2 : Specs)
		{
			FString Chains;
			for (int32 C : S2.ChainCancels) Chains += FString::FromInt(C);
			Desc += FString::Printf(TEXT("{%s su%d ac%d rc%d re%d dm%d hs%d hp%d pb%d ip%d fp%d md%d mc%d rb%d cf%d ah%d ap%d gr%d ut%d mh%d ch%s} "),
									*InputName(S2.Input), S2.Startup, S2.Active, S2.Recovery, S2.Reach, S2.Damage, S2.Hitstun,
									S2.Hitstop, S2.Pushback, S2.InitialProration, S2.ForcedProration, S2.MinimumDamagePercent,
									S2.MaxChain, S2.bReverseBeat ? 1 : 0, S2.CancelFrom,
										static_cast<int32>(S2.AirHitAction), S2.AirPushbackY, S2.Gravity, S2.Untech, S2.MultiHit, *Chains);
		}
		Bump(Counts, FString::Printf(TEXT("moves.%d"), NumMoves));
		Bump(Counts, bCorner ? TEXT("situation.corner") : TEXT("situation.midscreen"));

		const int64 FramesBefore = Stats.Frames;
		const int32 EscapesBefore = Stats.Escapes, BlocksBefore = Stats.Blocks, WhiffsBefore = Stats.Whiffs;
		const TArray<FRouteRec> E = Enumerate(B, Req, false, &Stats);
		const int64 EnumFrames = Stats.Frames - FramesBefore;
		Bump(Counts, TEXT("escapes"), Stats.Escapes - EscapesBefore);
		Bump(Counts, TEXT("blocks"), Stats.Blocks - BlocksBefore);
		Bump(Counts, TEXT("whiffs"), Stats.Whiffs - WhiffsBefore);
		Bump(Counts, TEXT("routes"), E.Num());
		if (E.Num() == 0) Bump(Counts, TEXT("episodes.empty"));
		for (const FRouteRec& R : E)
		{
			Bump(Counts, FString::Printf(TEXT("steps.%d"), R.Steps.Num()));
			for (int32 I = 0; I < R.Steps.Num(); ++I)
			{
				const FStepRec& St = R.Steps[I];
				Bump(Counts, St.bCancel ? TEXT("kind.cancel") : TEXT("kind.link"));
				if (St.bCancel && I > 0)
				{
					const FNSE025MoveSpec& PrevSpec = Specs[R.Steps[I - 1].MoveIndex];
					if (!PrevSpec.ChainCancels.Contains(St.MoveIndex)) Bump(Counts, TEXT("cancel.reversebeat"));
					if (PrevSpec.CancelFrom > 0) Bump(Counts, TEXT("cancel.latewindow"));
				}
			}
		}
		AddInfo(FString::Printf(TEXT("V7 %s -> %d routes, enumerator frames %lld: %s"), *Desc, E.Num(), EnumFrames, *ListStr(E)));

		if (Timer.Seconds() > TestDeadlineSeconds) break;
		const FDiscovery D = CheckExact(*this, FString::Printf(TEXT("V7 %s exact"), *Desc), B, Req, E);
		if (HasAnyErrors())
		{
			AddInfo(FString::Printf(TEXT("V7 first failing episode: %s actual %s"), *Desc, *TrialListStr(D.Result.Trials)));
		}

		// MaxFrames boundary: the best route's last hit frame.
		if (E.Num() > 0 && Rand.RandRange(0, 2) == 0)
		{
			FComboSearchRequest ReqEdge = Req;
			ReqEdge.MaxFrames = E[0].LastHit();
			const TArray<FRouteRec> EEdge = Enumerate(B, ReqEdge, false, &Stats);
			TestTrue(FString::Printf(TEXT("V7 %s MaxFrames boundary keeps the best route"), *Desc), EEdge.Num() > 0);
			if (Timer.Seconds() > TestDeadlineSeconds) break;
			CheckExact(*this, FString::Printf(TEXT("V7 %s MaxFrames %d"), *Desc, ReqEdge.MaxFrames), B, ReqEdge, EEdge);
			Bump(Counts, TEXT("bias.maxframes"));
		}

		// Boundary budget: one frame below the enumerator's own count.
		{
			FComboSearchRequest ReqBudget = Req;
			ReqBudget.MaxSimulatedFrames = static_cast<int32>(FMath::Max<int64>(1, EnumFrames - 1));
			if (Timer.Seconds() > TestDeadlineSeconds) break;
			const FDiscovery DB = Discover(B, ReqBudget);
			const FString Label = FString::Printf(TEXT("V7 %s budget %d"), *Desc, ReqBudget.MaxSimulatedFrames);
			TestTrue(FString::Printf(TEXT("%s: LocalFrame delta %d within budget"), *Label, DB.FrameDelta), DB.FrameDelta <= ReqBudget.MaxSimulatedFrames);
			int32 Prev = -1;
			bool bOk = true;
			for (const FComboTrial& T : DB.Result.Trials)
			{
				const int32 Index = IndexOfTrial(E, T, ReqBudget);
				if (Index < 0 || Index <= Prev)
				{
					bOk = false;
					AddError(FString::Printf(TEXT("%s: returned trial invalid or out of order: %s"), *Label, *TrialStr(T)));
					break;
				}
				Prev = Index;
			}
			bool bFullPrefix = DB.Result.Trials.Num() == FMath::Min(E.Num(), Req.MaxResults);
			for (int32 I = 0; bFullPrefix && I < DB.Result.Trials.Num(); ++I) bFullPrefix = TrialMatches(DB.Result.Trials[I], E[I], ReqBudget);
			if (DB.Result.bComplete) TestTrue(Label + TEXT(": bComplete only with the exact prefix"), bFullPrefix);
			Bump(Counts, DB.Result.bComplete ? TEXT("budget.complete") : TEXT("budget.incomplete"));
			// More budget never worsens a filled rank.
			FComboSearchRequest ReqLess = ReqBudget;
			ReqLess.MaxSimulatedFrames = FMath::Max(1, ReqBudget.MaxSimulatedFrames / 3);
			if (Timer.Seconds() > TestDeadlineSeconds) break;
			const FDiscovery DL = Discover(B, ReqLess);
			TestTrue(FString::Printf(TEXT("%s: LocalFrame delta %d within the smaller budget %d"), *Label, DL.FrameDelta, ReqLess.MaxSimulatedFrames),
					 DL.FrameDelta <= ReqLess.MaxSimulatedFrames);
			TestTrue(FString::Printf(TEXT("%s: budget %d returns at least as many trials as budget %d (%d versus %d)"), *Label,
									 ReqBudget.MaxSimulatedFrames, ReqLess.MaxSimulatedFrames, DB.Result.Trials.Num(), DL.Result.Trials.Num()),
					 DB.Result.Trials.Num() >= DL.Result.Trials.Num());
			for (int32 Rank = 0; Rank < FMath::Min(DL.Result.Trials.Num(), DB.Result.Trials.Num()); ++Rank)
			{
				const int32 IL = IndexOfTrial(E, DL.Result.Trials[Rank], ReqLess);
				const int32 IM = IndexOfTrial(E, DB.Result.Trials[Rank], ReqBudget);
				TestTrue(FString::Printf(TEXT("%s: rank %d is valid and no worse with more budget (ranking index %d versus %d)"), *Label, Rank, IM, IL),
						 IL >= 0 && IM >= 0 && IM <= IL);
			}
			Bump(Counts, DL.Result.Trials.Num() > 0 ? TEXT("budget.less.nonempty") : TEXT("budget.less.empty"));
		}

		// Every trial replays and validates; one perturbed tape per episode is judged independently.
		for (int32 I = 0; I < D.Result.Trials.Num(); ++I)
		{
			const FComboTrial& T = D.Result.Trials[I];
			const FString Label = FString::Printf(TEXT("V7 %s trial #%d"), *Desc, I);
			CheckTape(*this, Label, T, Req);
			Restore(B, S);
			CheckTrialReplays(*this, Label, B, T, INP_Neutral, false);
			Restore(B, S);
			const FVerdict V = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, T, INP_Neutral));
			TestTrue(Label + TEXT(": validates with FailedStep -1"), V.bCompleted && V.FailedStep == -1);
			TestEqual(Label + TEXT(": ObservedDamage equals TotalDamage"), V.ObservedDamage, T.TotalDamage);
			Bump(Counts, TEXT("validated"));
		}
		if (D.Result.Trials.Num() > 0)
		{
			const FComboTrial& T = D.Result.Trials[Rand.RandRange(0, D.Result.Trials.Num() - 1)];
			if (T.Steps.Num() > 0 && T.Inputs.Num() == T.Steps.Last().HitFrame)
			{
				FComboTrial Shifted = T;
				const int32 Last = T.Steps.Last().BeginFrame;
				Shifted.Inputs.Add(INP_Neutral);
				Shifted.Inputs[Last] = Shifted.Inputs[Last - 1];
				Shifted.Inputs[Last - 1] = INP_Neutral;
				Restore(B, S);
				const FSimResult R = Replay(B, Shifted.Inputs, INP_Neutral, false);
				const FVerdict J = Judge(R, Shifted);
				Restore(B, S);
				const FVerdict V = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, Shifted, INP_Neutral));
				TestTrue(FString::Printf(TEXT("V7 %s shifted last press of %s: report %s equals judge %s (contacts %s)"), *Desc,
										 *TrialStr(T), *V.ToString(), *J.ToString(), *R.ContactsStr()), ReportMatches(V, J));
				Bump(Counts, J.bCompleted ? TEXT("shifted.completes") : TEXT("shifted.fails"));
			}
		}

		// Tech lens, the guard lens's analog: while no hit of the domain lands on a defender that could
		// act, holding the tech button changes nothing, and a route replayed under it is unchanged.
		if (Timer.Seconds() <= TestDeadlineSeconds)
		{
			FComboSearchRequest ReqTech = Req;
			ReqTech.OpponentInput = FNSE025Battle::TechInput();
			const TArray<FRouteRec> ET = Enumerate(B, ReqTech, false, &Stats);
			TestTrue(FString::Printf(TEXT("V7 %s tech lens: enumerations under neutral and tech agree"), *Desc),
					 ListStr(ET) == ListStr(E));
			CheckExact(*this, FString::Printf(TEXT("V7 %s under tech"), *Desc), B, ReqTech, ET);
			for (int32 I = 0; I < ET.Num() && I < 2; ++I)
			{
				const FComboTrial T = ToTrial(ET[I], ReqTech);
				Restore(B, S);
				CheckTrialReplays(*this, FString::Printf(TEXT("V7 %s tech lens trial #%d"), *Desc, I), B, T,
								ReqTech.OpponentInput, false);
			}
			// CheckTrialReplays leaves the battle stepped; later metamorphic
			// searches must compare the same initial situation used by E.
			Restore(B, S);
			Bump(Counts, TEXT("lens.tech"));
		}

		// Metamorphic: a dominated move declared last changes nothing.
		if (NumMoves < FNSE025Battle::MaxMoves)
		{
			TArray<int32> SlotsPlus = Slots;
			SlotsPlus.Add(NumMoves);
			const FComboSearchRequest ReqPlus = MakeRequest(B, SlotsPlus, MaxSteps, MaxFrames, 6);
			if (Timer.Seconds() > TestDeadlineSeconds) break;
			CheckExact(*this, FString::Printf(TEXT("V7 %s with a dominated move appended"), *Desc), B, ReqPlus, E);
		}
		// Metamorphic: more results keep the previous list as a prefix.
		{
			FComboSearchRequest ReqMore = Req;
			ReqMore.MaxResults = 12;
			if (Timer.Seconds() > TestDeadlineSeconds) break;
			CheckExact(*this, FString::Printf(TEXT("V7 %s with MaxResults 12"), *Desc), B, ReqMore, E);
		}
		// Metamorphic: the mirrored situation gives the same steps, damages and tapes.
		if (!bCorner)
		{
			B.Start(-AttackerX, -DefenderX);
			if (Timer.Seconds() > TestDeadlineSeconds) break;
			CheckExact(*this, FString::Printf(TEXT("V7 %s mirrored"), *Desc), B, Req, E);
		}
		++Completed;
		if (HasAnyErrors())
		{
			AddInfo(FString::Printf(TEXT("V7 stopping at the first failing episode: %s"), *Desc));
			break;
		}
	}
	TestTrue(FString::Printf(TEXT("V7 at least %d episodes completed (%d)"), MinEpisodes, Completed), Completed >= MinEpisodes);
	AddInfo(FString::Printf(TEXT("V7 fuzz finished: seed %d, %d/%d episodes, histogram: %s"), Seed, Completed, Iterations, *HistogramStr(Counts)));
	AddInfo(FString::Printf(TEXT("V7 %s"), *StatsStr(Stats, Timer.Seconds())));
	return !HasAnyErrors();
}

// V8: stateful mid-combo situations: hit prefixes, a move in progress before its hit, a blocked prefix.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE025MidComboSituations, "UnrealBench.NSE025.MidComboSituations",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE025MidComboSituations::RunTest(const FString&)
{
	FScopedCounters Timer;
	const int32 Seed = GetSeed(2601);
	const int32 Iterations = GetIterations(3);
	FRandomStream Rand(Seed);
	TMap<FString, int32> Counts;
	FNSE025Battle B;
	B.Attacker->CanReverseBeat = false;
	AddReferenceMoves(B);
	B.AddMove(Launcher());
	B.AddMove(Floater());
	B.AddMove(MultiJab());
	B.Start();
	FEnumStats Stats;
	const int32 Guard = B.GuardInput();

	const FComboSearchRequest ReqIdle = MakeRequest(B, {0, 1, 2, 3}, 2, 60, 100);
	const TArray<FRouteRec> EIdle = Enumerate(B, ReqIdle, false, &Stats);
	TestTrue(TEXT("V8: routes from idle exist"), EIdle.Num() > 0);
	FRollbackData FirstPrefixSituation;
	bool bHavePrefixSituation = false;

	for (int32 Episode = 0; Episode < Iterations && EIdle.Num() > 0; ++Episode)
	{
		if (Timer.Seconds() > TestDeadlineSeconds) break;
		const FRouteRec& Prefix = EIdle[Rand.RandRange(0, EIdle.Num() - 1)];
		B.Reset();
		const FSimResult R = Simulate(B, Prefix.Inputs, INP_Neutral, 0, false, false);
		const FString Desc = FString::Printf(TEXT("seed %d episode %d prefix %s"), Seed, Episode, *RouteStr(Prefix));
		TestEqual(FString::Printf(TEXT("V8 %s: prefix replays its hits"), *Desc), R.HitCount(), Prefix.Steps.Num());
		TestTrue(FString::Printf(TEXT("V8 %s: defender stunned at the situation"), *Desc), B.Defender->CheckIsStunned());
		FRollbackData S = Snapshot(B);
		if (!bHavePrefixSituation)
		{
			FirstPrefixSituation = Snapshot(B);
			bHavePrefixSituation = true;
		}
		Bump(Counts, FString::Printf(TEXT("prefix.steps.%d"), Prefix.Steps.Num()));
		const FComboSearchRequest ReqG = MakeRequest(B, {0, 1, 2, 3}, 2, 60, 100, FullBudget, Guard);
		const TArray<FRouteRec> E = Enumerate(B, ReqG, true, &Stats);
		Bump(Counts, TEXT("routes"), E.Num());
		AddInfo(FString::Printf(TEXT("V8 %s -> %s"), *Desc, *ListStr(E)));
		const FDiscovery D = CheckExact(*this, FString::Printf(TEXT("V8 %s under guard"), *Desc), B, ReqG, E);
		for (int32 I = 0; I < D.Result.Trials.Num(); ++I)
		{
			const FComboTrial& T = D.Result.Trials[I];
			const FString Label = FString::Printf(TEXT("V8 %s trial #%d"), *Desc, I);
			Restore(B, S);
			CheckTrialReplays(*this, Label, B, T, Guard, true);
			for (int32 Opp : {static_cast<int32>(INP_Neutral), Guard})
			{
				Restore(B, S);
				const FVerdict V = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, T, Opp));
				TestTrue(FString::Printf(TEXT("%s validates under opponent input %d"), *Label, Opp), V.bCompleted && V.FailedStep == -1);
				TestEqual(FString::Printf(TEXT("%s ObservedDamage under opponent input %d"), *Label, Opp), V.ObservedDamage, T.TotalDamage);
			}
			Bump(Counts, TEXT("validated"));
		}
		if (HasAnyErrors()) break;
	}

	// The rest of a two-step route is found again from the situation after its first hit, with frames counted from
	// there, and that tail validates from the new situation. Nothing here is computed by the feature.
	{
		int32 Checked = 0;
		for (const FRouteRec& R : EIdle)
		{
			if (R.Steps.Num() != 2) continue;
			const int32 H1 = R.Steps[0].Hit;
			const FString Desc = FString::Printf(TEXT("V8 tail of %s"), *RouteStr(R));
			B.Reset();
			TArray<int32> Head = R.Inputs;
			Head.SetNum(H1);
			const FSimResult RH = Simulate(B, Head, INP_Neutral, 0, false, false);
			TestEqual(Desc + TEXT(": the head replays its hit"), RH.HitCount(), 1);
			FRollbackData S = Snapshot(B);
			const FComboSearchRequest ReqT = MakeRequest(B, {0, 1, 2, 3}, 1, ReqIdle.MaxFrames - H1, 100);
			FRouteRec Tail;
			Tail.Steps.Add(R.Steps[1]);
			Tail.Steps[0].Begin -= H1;
			Tail.Steps[0].Hit -= H1;
			Tail.Total = Tail.Steps[0].Damage;
			for (int32 F = H1; F < R.Inputs.Num(); ++F) Tail.Inputs.Add(R.Inputs[F]);
			const TArray<FRouteRec> ET = Enumerate(B, ReqT, true, &Stats);
			TestTrue(Desc + TEXT(": the enumerator lists the tail from the new situation"), IndexOfTrial(ET, ToTrial(Tail, ReqT), ReqT) >= 0);
			const FDiscovery D = Discover(B, ReqT);
			TestTrue(Desc + TEXT(": bComplete"), D.Result.bComplete);
			int32 Found = -1;
			for (int32 I = 0; I < D.Result.Trials.Num() && Found < 0; ++I)
			{
				if (TrialMatches(D.Result.Trials[I], Tail, ReqT)) Found = I;
			}
			TestTrue(FString::Printf(TEXT("%s: discovery returns the tail %s (actual %s)"), *Desc, *RouteStr(Tail),
									 *TrialListStr(D.Result.Trials)), Found >= 0);
			if (Found >= 0)
			{
				Restore(B, S);
				const FVerdict V = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, D.Result.Trials[Found], INP_Neutral));
				TestTrue(Desc + TEXT(": the tail validates from the new situation"), V.bCompleted && V.FailedStep == -1);
				TestEqual(Desc + TEXT(": ObservedDamage equals the tail's damage"), V.ObservedDamage, Tail.Total);
			}
			++Checked;
			if (HasAnyErrors()) break;
		}
		TestTrue(FString::Printf(TEXT("V8 tails: at least one two-step route checked (%d)"), Checked), Checked >= 1);
		Bump(Counts, TEXT("tails"), Checked);
	}

	// A move in progress before its hit: the end of its third frame, where the engine's kara window has closed.
	// From Strong (hit on 6, hitstop 6, recovers by itself on 27 while the defender regains control on 29) no link
	// can continue and the only cancel is its chain option into Special; from Jab (hit on 2, recovers on 13,
	// control frame 17) a linked Poke lands on 16 and continues while a linked Jab lands on the control frame.
	struct FMidMove
	{
		int32 Slot;
		const TCHAR* Name;
		int32 OwnHitFrame;
		int32 OwnDamage;
		TArray<int32> CancelTargets;
		bool bLinkExists;
	};
	for (const FMidMove& M : {FMidMove{1, TEXT("Strong"), 6, 700, {2}, false}, FMidMove{0, TEXT("Jab"), 2, 300, {1, 2}, true}})
	{
		const FString P = FString::Printf(TEXT("V8 mid-%s"), M.Name);
		B.Reset();
		Simulate(B, Tape(3, {{1, B.Moves[M.Slot].Input}}), INP_Neutral, 0, false, false);
		TestTrue(P + TEXT(": attacker in the move at the situation"), B.Attacker->PrimaryStateMachine.CurrentState->Name == FNSE025Battle::MoveTag(M.Slot));
		TestFalse(P + TEXT(": defender free at the situation"), B.Defender->CheckIsStunned());
		FRollbackData S = Snapshot(B);
		const FSimResult Scan = Simulate(B, {}, INP_Neutral, 40, false, false);
		TestEqual(P + TEXT(": the move lands its own hit during the scan"), Scan.HitCount(), 1);
		const int32 OwnHit = Scan.Contacts.Num() ? Scan.Contacts[0].Frame : -1;
		const int32 OwnDamage = Scan.Contacts.Num() ? Scan.Contacts[0].Damage : 0;
		TestEqual(P + FString::Printf(TEXT(": the own hit lands on frame %d"), M.OwnHitFrame), OwnHit, M.OwnHitFrame);
		TestEqual(P + FString::Printf(TEXT(": the own damage is %d"), M.OwnDamage), OwnDamage, M.OwnDamage);
		Restore(B, S);
		const FComboSearchRequest Req = MakeRequest(B, {0, 1, 2, 3}, 2, 60, 100);
		const TArray<FRouteRec> E = Enumerate(B, Req, false, &Stats);
		AddInfo(FString::Printf(TEXT("%s: %s"), *P, *ListStr(E)));
		TestTrue(P + TEXT(": routes exist"), E.Num() > 0);
		bool bLinkFirst = false, bAllFirstCancelsLegal = true;
		for (const FRouteRec& R : E)
		{
			const FStepRec& St = R.Steps[0];
			if (St.bCancel)
			{
				if (!M.CancelTargets.Contains(St.MoveIndex) || St.Begin <= OwnHit) bAllFirstCancelsLegal = false;
			}
			else
			{
				bLinkFirst = true;
			}
		}
		TestTrue(P + TEXT(": a cancel through the chain option after the own hit exists"), IndexOfShape(E, {{M.CancelTargets[0], true}}) >= 0);
		TestTrue(P + TEXT(": every first cancel is a chain option and begins after the own hit"), bAllFirstCancelsLegal);
		TestEqual(P + TEXT(": [Jab link] absent (lands on or after the control frame)"), IndexOfShape(E, {{0, false}}), -1);
		if (M.bLinkExists)
		{
			TestTrue(P + TEXT(": [Poke link] present (lands before the control frame)"), IndexOfShape(E, {{3, false}}) >= 0);
		}
		else
		{
			TestFalse(P + TEXT(": no linked first step (the move recovers after the defender's control frame)"), bLinkFirst);
		}
		const FDiscovery D = CheckExact(*this, P, B, Req, E);
		for (int32 I = 0; I < D.Result.Trials.Num(); ++I)
		{
			const FComboTrial& T = D.Result.Trials[I];
			const FString Label = FString::Printf(TEXT("%s trial #%d %s"), *P, I, *TrialStr(T));
			Restore(B, S);
			const FSimResult R = Replay(B, T.Inputs, INP_Neutral, false);
			const FVerdict J = Judge(R, T);
			TestTrue(Label + TEXT(": judge completes"), J.bCompleted);
			Restore(B, S);
			const FVerdict V = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, T, INP_Neutral));
			TestTrue(Label + TEXT(": validates with FailedStep -1"), V.bCompleted && V.FailedStep == -1);
			TestEqual(Label + TEXT(": ObservedDamage includes the own hit"), V.ObservedDamage, T.TotalDamage + OwnDamage);
		}
		Bump(Counts, FString::Printf(TEXT("midmove.%s.routes"), M.Name), E.Num());
	}

	// After-launcher situation: the end of the frame Floater's hit registered. Juggle steps continue
	// while the untech holds, and the tech lens replays the best trial with the tech button held.
	{
		B.Reset();
		const FSimResult RL = Simulate(B, Tape(9, {{1, INP_F}}), INP_Neutral, 0, false, false);
		const FString P = TEXT("V8 after-launcher");
		TestTrue(P + TEXT(": Floater's hit registers"), RL.HitCount() == 1);
		TestTrue(P + TEXT(": the defender cannot act at the situation"), B.Defender->CheckIsStunned());
		FRollbackData S = Snapshot(B);
		Restore(B, S);
		const FSimResult Arc = Simulate(B, {}, INP_Neutral, 60, false, false);
		bool bAirborneHeld = false;
		for (const FFrameObs& O : Arc.Frames) bAirborneHeld |= O.DefY > 0 && O.bStunned;
		TestTrue(P + TEXT(": the defender is airborne and held during the arc"), bAirborneHeld);
		Restore(B, S);
		const FComboSearchRequest Req = MakeRequest(B, {0, 1, 4, 5, 6}, 2, 90, 100);
		const TArray<FRouteRec> E = Enumerate(B, Req, true, &Stats);
		AddInfo(FString::Printf(TEXT("%s: %s"), *P, *ListStr(E)));
		TestTrue(P + TEXT(": juggle routes exist"), E.Num() > 0);
		const FDiscovery D = CheckExact(*this, P, B, Req, E);
		if (D.Result.Trials.Num() > 0)
		{
			const FComboTrial& T = D.Result.Trials[0];
			const FString Label = P + TEXT(" best trial");
			Restore(B, S);
			CheckTrialReplays(*this, Label, B, T, INP_Neutral, true);
			Restore(B, S);
			const FVerdict V = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, T, INP_Neutral));
			TestTrue(Label + TEXT(": validates"), V.bCompleted && V.FailedStep == -1);
			TestEqual(Label + TEXT(": ObservedDamage equals the total"), V.ObservedDamage, T.TotalDamage);
			// The tech input cannot break a route whose every hit lands while the defender cannot act.
			Restore(B, S);
			CheckTrialReplays(*this, Label + TEXT(" under tech"), B, T, FNSE025Battle::TechInput(), true);
		}
		Bump(Counts, TEXT("afterlauncher.routes"), E.Num());
	}

	// OTG: the defender is knocked down, so the next hit carries the engine's OTG proration and still
	// continues the combo (a downed defender cannot act).
	{
		const FString P = TEXT("V8 OTG");
		// The combo damage of a second hit, without knockdown.
		B.Reset();
		const FSimResult RC = Simulate(B, Tape(17, {{1, INP_A}, {9, INP_B}}), INP_Neutral, 0, false, false);
		TestEqual(P + TEXT(": the Jab, Strong cancel replay has two hits"), RC.HitCount(), 2);
		const int32 ComboDamage = RC.HitCount() == 2 ? RC.Contacts[1].Damage : -1;
		// The same hit on the knocked-down defender.
		B.Reset();
		Simulate(B, Tape(5, {{1, INP_A}}), INP_Neutral, 0, false, false);
		TestTrue(P + TEXT(": the Jab holds the defender"), B.Defender->CheckIsStunned());
		B.Defender->SetKnockdownState();
		FRollbackData S = Snapshot(B);
		Restore(B, S);
		const FSimResult RO = Simulate(B, Tape(12, {{1, INP_B}}), INP_Neutral, 0, false, true);
		AddInfo(FString::Printf(TEXT("%s: %s"), *P, *RO.ContactsStr()));
		TestEqual(P + TEXT(": the OTG hit lands"), RO.HitCount(), 1);
		if (RO.HitCount() == 1 && ComboDamage > 0)
		{
			TestEqual(P + TEXT(": the damage carries the engine's OtgProration"), RO.Contacts[0].Damage,
					  ComboDamage * B.Defender->OtgProration / 100);
			TestTrue(P + TEXT(": the OTG hit continues the combo"), RO.Contacts[0].bContinues);
		}
		Restore(B, S);
		const FComboSearchRequest Req = MakeRequest(B, {1}, 1, 30, 10);
		const TArray<FRouteRec> E = Enumerate(B, Req, true, &Stats);
		AddInfo(FString::Printf(TEXT("%s: %s"), *P, *ListStr(E)));
		TestTrue(P + TEXT(": the enumerator finds the OTG step"), E.Num() >= 1);
		bool bAllProrated = E.Num() > 0 && ComboDamage > 0;
		for (const FRouteRec& Rt : E)
		{
			bAllProrated &= Rt.Steps[0].Damage == ComboDamage * B.Defender->OtgProration / 100;
		}
		TestTrue(P + TEXT(": every OTG step's damage carries the proration"), bAllProrated);
		CheckExact(*this, P, B, Req, E);
		if (E.Num() >= 1)
		{
			const FComboTrial T = ToTrial(E[0], Req);
			Restore(B, S);
			const FVerdict V = ReportVerdict(UComboDiscovery::ValidateTrial(B.Game, T, INP_Neutral));
			TestTrue(P + TEXT(": the OTG step validates"), V.bCompleted && V.FailedStep == -1);
			TestEqual(P + TEXT(": ObservedDamage equals the step damage"), V.ObservedDamage, T.TotalDamage);
		}
		Bump(Counts, TEXT("otg.routes"), E.Num());
	}

	// Blocked prefix: Jab blocked on frame 5, attacker idle again while the defender is still in blockstun.
	{
		B.Reset();
		FSimResult R = Simulate(B, Tape(5, {{1, INP_A}}), Guard, 0, false, false);
		TestTrue(TEXT("V8 blocked prefix: Jab is blocked on frame 5"), R.Contacts.Num() == 1 && !R.Contacts[0].bHit);
		int32 F = 5;
		while (B.Attacker->GetStateType() != EStateType::Standing && F < 40)
		{
			StepBattle(B, INP_Neutral, INP_Neutral);
			++F;
		}
		TestEqual(TEXT("V8 blocked prefix: attacker idle on frame 16"), F, 16);
		TestTrue(TEXT("V8 blocked prefix: defender still in blockstun"), B.Defender->GetStateType() == EStateType::Blockstun && B.Defender->CheckIsStunned());
		FRollbackData S = Snapshot(B);
		const FSimResult Scan = Simulate(B, {}, INP_Neutral, 40, false, false);
		int32 FreeFrame = -1;
		for (int32 I = 0; I < Scan.Frames.Num() && FreeFrame < 0; ++I)
		{
			if (!Scan.Frames[I].bStunned) FreeFrame = I + 1;
		}
		TestTrue(TEXT("V8 blocked prefix: blockstun expires under neutral"), FreeFrame > 0);
		Restore(B, S);
		const FComboSearchRequest Req = MakeRequest(B, {0, 1, 2, 3}, 2, 60, 100);
		const TArray<FRouteRec> E = Enumerate(B, Req, false, &Stats);
		AddInfo(FString::Printf(TEXT("V8 blocked prefix (free on %d): %s"), FreeFrame, *ListStr(E)));
		TestTrue(TEXT("V8 blocked prefix: exempt first steps exist under neutral"), E.Num() > 0);
		bool bAllAfterFree = true;
		for (const FRouteRec& Rt : E) bAllAfterFree &= Rt.Steps[0].Hit > FreeFrame;
		TestTrue(TEXT("V8 blocked prefix: every first hit lands after the blockstun expired"), bAllAfterFree);
		CheckExact(*this, TEXT("V8 blocked prefix under neutral"), B, Req, E);
		const FComboSearchRequest ReqG = MakeRequest(B, {0, 1, 2, 3}, 2, 60, 100, FullBudget, Guard);
		const TArray<FRouteRec> EG = Enumerate(B, ReqG, false, &Stats);
		TestEqual(TEXT("V8 blocked prefix: enumerator finds nothing under guard"), EG.Num(), 0);
		ExpectEmpty(*this, TEXT("V8 blocked prefix under guard"), B, ReqG, EComplete::Yes);
	}

	// Integrity from the first hit-prefix situation.
	if (bHavePrefixSituation)
	{
		const FComboSearchRequest ReqG = MakeRequest(B, {0, 1, 2, 3}, 2, 60, 100, FullBudget, Guard);
		CheckIntegrity(*this, TEXT("V8 integrity after DiscoverCombos from a hit prefix"), B, FirstPrefixSituation,
					   [&]() { UComboDiscovery::DiscoverCombos(B.Game, ReqG); });
		Restore(B, FirstPrefixSituation);
		const TArray<FRouteRec> E = Enumerate(B, ReqG, true, &Stats);
		if (E.Num() > 0)
		{
			const FComboTrial T = ToTrial(E[0], ReqG);
			CheckIntegrity(*this, TEXT("V8 integrity after ValidateTrial from a hit prefix"), B, FirstPrefixSituation,
						   [&]() { UComboDiscovery::ValidateTrial(B.Game, T, Guard); });
			FComboTrial Wrong = T;
			Wrong.Steps[0].Damage += 1;
			FComboSearchRequest Small = ReqG;
			Small.MaxSimulatedFrames = 25;
			CheckIntegrity(*this, TEXT("V8 integrity after an interrupted search, a failed validation and a full search from a hit prefix"),
						   B, FirstPrefixSituation, [&]()
			{
				UComboDiscovery::DiscoverCombos(B.Game, Small);
				UComboDiscovery::ValidateTrial(B.Game, Wrong, INP_Neutral);
				UComboDiscovery::DiscoverCombos(B.Game, ReqG);
			});
		}
	}
	AddInfo(FString::Printf(TEXT("V8 finished: seed %d, histogram: %s"), Seed, *HistogramStr(Counts)));
	AddInfo(FString::Printf(TEXT("V8 %s"), *StatsStr(Stats, Timer.Seconds())));
	return !HasAnyErrors();
}
