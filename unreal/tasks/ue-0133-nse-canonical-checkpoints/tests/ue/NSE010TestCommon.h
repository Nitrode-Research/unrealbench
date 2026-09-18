// Shared helpers for the NSE010 checkpoint suite. Every expectation is read from a
// control battle driven by the engine alone; the feature never computes an expectation.
#pragma once
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GameplayTagContainer.h"
#include "NightSkyEngine/Fixtures/NSE010Fixture.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/BattleObject.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Battle/Script/StateMachine.h"
#include "NightSkyEngine/Battle/Script/BattleExtension.h"

namespace NSE010
{
using FBytes = TArray<uint8>;

// Fixture geometry: hurtboxes span [PosX - 30000, PosX + 30000] x [PosY, PosY + 100000],
// shot hitboxes span 10000 around the shot position, players start at -100000 and 100000.
constexpr int32 P1X = -100000;
constexpr int32 P2X = 100000;
constexpr int32 OnP2HurtboxX = 80000;
constexpr int32 ShotY = 50000;
constexpr int32 FarY = 300000;

inline FGameplayTag Tag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(FName(Name));
}
inline FGameplayTag StandTag()
{
	return Tag(TEXT("State.Universal.Stand"));
}
inline FGameplayTag TagInTag()
{
	return Tag(TEXT("State.Universal.TagIn"));
}
inline FGameplayTag IdleTag(const FNSE010Battle& B)
{
	return B.Players[0]->PrimaryStateMachine.StateNames[0];
}

inline FBytes Export(FNSE010Battle& B)
{
	return B.Game->ExportCheckpoint();
}
inline EBattleCheckpointResult Import(FNSE010Battle& B, const FBytes& Bytes)
{
	return B.Game->ImportCheckpoint(Bytes);
}
inline FString ResultName(EBattleCheckpointResult R)
{
	switch (R)
	{
	case EBattleCheckpointResult::Restored: return TEXT("Restored");
	case EBattleCheckpointResult::Malformed: return TEXT("Malformed");
	case EBattleCheckpointResult::UnsupportedVersion: return TEXT("UnsupportedVersion");
	case EBattleCheckpointResult::LineupMismatch: return TEXT("LineupMismatch");
	default: return FString::Printf(TEXT("Unknown(%d)"), static_cast<int32>(R));
	}
}
inline bool ExpectResult(FAutomationTestBase& T, const FString& Label, EBattleCheckpointResult Actual,
						  EBattleCheckpointResult Expected)
{
	return T.TestEqual(Label, ResultName(Actual), ResultName(Expected));
}
// Where the instruction pins no single rejection code, any listed code is accepted
// (the bands are recorded in verification.md).
inline bool ExpectResultIn(FAutomationTestBase& T, const FString& Label, EBattleCheckpointResult Actual,
							const TArray<EBattleCheckpointResult>& Accepted)
{
	FString Names;
	for (const EBattleCheckpointResult R : Accepted)
	{
		if (R == Actual) return true;
		Names += (Names.IsEmpty() ? TEXT("") : TEXT(" or ")) + ResultName(R);
	}
	T.AddError(FString::Printf(TEXT("%s: result %s, expected %s"), *Label, *ResultName(Actual), *Names));
	return false;
}

// Logical identity of an object inside its own battle: P<i> for players, O<i> for pool slots.
inline FString Id(const FNSE010Battle& B, const ABattleObject* Obj)
{
	if (!Obj) return TEXT("null");
	for (int32 I = 0; I < B.Players.Num(); ++I)
	{
		if (B.Players[I] == Obj) return FString::Printf(TEXT("P%d"), I);
	}
	for (int32 I = 0; I < B.Game->Objects.Num(); ++I)
	{
		if (B.Game->Objects[I] == Obj) return FString::Printf(TEXT("O%d"), I);
	}
	return TEXT("foreign");
}

inline UNSE010Script* ScriptOf(const ABattleObject* Obj)
{
	if (!Obj) return nullptr;
	if (Obj->IsPlayer)
	{
		const APlayerObject* Player = Cast<APlayerObject>(Obj);
		return Player ? Cast<UNSE010Script>(static_cast<UObject*>(Player->PrimaryStateMachine.CurrentState)) : nullptr;
	}
	return Cast<UNSE010Script>(static_cast<UObject*>(Obj->ObjectState.Get()));
}
inline UNSE010Script* RegisteredScript(const APlayerObject* Player, const FGameplayTag& Name)
{
	const int32 Index = Player->PrimaryStateMachine.StateNames.Find(Name);
	return Index == INDEX_NONE ? nullptr
							   : Cast<UNSE010Script>(static_cast<UObject*>(Player->PrimaryStateMachine.States[Index]));
}
inline FString StateName(const APlayerObject* Player)
{
	return Player->PrimaryStateMachine.CurrentState ? Player->PrimaryStateMachine.CurrentState->Name.ToString()
													: TEXT("none");
}
inline int32 Counter(const ABattleObject* Obj)
{
	const UNSE010Script* S = ScriptOf(Obj);
	return S ? S->Counter : -1;
}
// Pool part of the update order, as slot indices.
inline TArray<int32> SlotOrder(const FNSE010Battle& B)
{
	TArray<int32> Order;
	for (int32 I = B.Players.Num(); I < B.Game->SortedObjects.Num(); ++I)
	{
		Order.Add(B.Game->Objects.Find(B.Game->SortedObjects[I]));
	}
	return Order;
}
inline FString IntArrayString(const TArray<int32>& Values, int32 From = 0)
{
	FString Out = TEXT("[");
	for (int32 I = From; I < Values.Num(); ++I)
	{
		Out += FString::Printf(TEXT("%s%d"), I == From ? TEXT("") : TEXT(", "), Values[I]);
	}
	return Out + TEXT("]");
}

// Observation vector: gameplay state only, references by logical identity.
struct FObs
{
	TArray<FString> Keys;
	TArray<FString> Vals;
	void Add(const FString& K, const FString& V)
	{
		Keys.Add(K);
		Vals.Add(V);
	}
	void AddInt(const FString& K, int64 V)
	{
		Add(K, FString::Printf(TEXT("%lld"), V));
	}
};

inline uint32 InputHistoryHash(const APlayerObject* Player)
{
	uint32 Hash = 0;
	for (int32 I = 0; I < InputBufferSize; ++I)
	{
		Hash = Hash * 31 + static_cast<uint32>(Player->StoredInputBuffer.InputBufferInternal[I]);
		Hash = Hash * 31 + static_cast<uint32>(Player->StoredInputBuffer.InputTime[I]);
		Hash = Hash * 31 + static_cast<uint32>(Player->StoredInputBuffer.InputBufferValid[I]);
	}
	return Hash;
}

inline FObs Observe(const FNSE010Battle& B)
{
	FObs O;
	const FBattleState& S = B.Game->BattleState;
	O.AddInt(TEXT("FrameNumber"), S.FrameNumber);
	O.AddInt(TEXT("RoundTimer"), S.RoundTimer);
	O.AddInt(TEXT("PauseTimer"), S.PauseTimer);
	O.AddInt(TEXT("Meter0"), S.Meter[0]);
	O.AddInt(TEXT("Meter1"), S.Meter[1]);
	O.AddInt(TEXT("GaugeP1"), S.GaugeP1.Num() ? S.GaugeP1[0] : -1);
	O.AddInt(TEXT("GaugeP2"), S.GaugeP2.Num() ? S.GaugeP2[0] : -1);
	O.AddInt(TEXT("BattlePhase"), static_cast<int32>(S.BattlePhase));
	O.AddInt(TEXT("RoundCount"), S.RoundCount);
	O.AddInt(TEXT("P1RoundsWon"), S.P1RoundsWon);
	O.AddInt(TEXT("P2RoundsWon"), S.P2RoundsWon);
	O.AddInt(TEXT("SuperFreezeDuration"), S.SuperFreezeDuration);
	O.AddInt(TEXT("SuperFreezeSelfDuration"), S.SuperFreezeSelfDuration);
	O.Add(TEXT("SuperFreezeCaller"), Id(B, S.SuperFreezeCaller));
	O.Add(TEXT("MainPlayer0"), Id(B, S.MainPlayer[0]));
	O.Add(TEXT("MainPlayer1"), Id(B, S.MainPlayer[1]));
	O.AddInt(TEXT("RandomSeed"), S.RandomManager.GetSeed());
	O.AddInt(TEXT("ActiveObjectCount"), S.ActiveObjectCount);
	for (int32 Team = 0; Team < 2; ++Team)
	{
		for (int32 I = 0; I < S.TeamData[Team].CooldownTimer.Num(); ++I)
		{
			O.AddInt(FString::Printf(TEXT("Cooldown%d_%d"), Team, I), S.TeamData[Team].CooldownTimer[I]);
		}
	}
	O.AddInt(TEXT("ScreenWorldCenterX"), S.ScreenData.ScreenWorldCenterX);
	O.AddInt(TEXT("ScreenWorldCenterY"), S.ScreenData.ScreenWorldCenterY);
	O.AddInt(TEXT("ScreenWorldWidth"), S.ScreenData.ScreenWorldWidth);
	O.AddInt(TEXT("ScreenTargetCount"), S.ScreenData.TargetObjects.Num());
	for (int32 I = 0; I < S.ScreenData.TargetObjects.Num(); ++I)
	{
		O.Add(FString::Printf(TEXT("ScreenTarget%d"), I), Id(B, S.ScreenData.TargetObjects[I]));
	}
	if (const UNSE010Extension* E = B.Extension())
	{
		O.AddInt(TEXT("ExtTicks"), E->Ticks);
		O.AddInt(TEXT("ExtLiftPeriod"), E->LiftPeriod);
		O.AddInt(TEXT("ExtLiftAmount"), E->LiftAmount);
	}
	for (int32 I = 0; I < B.Players.Num(); ++I)
	{
		const APlayerObject* P = B.Players[I];
		const FString K = FString::Printf(TEXT("P%d."), I);
		O.AddInt(K + TEXT("PosX"), P->PosX);
		O.AddInt(K + TEXT("PosY"), P->PosY);
		O.AddInt(K + TEXT("Direction"), P->Direction);
		O.AddInt(K + TEXT("CurrentHealth"), P->CurrentHealth);
		O.AddInt(K + TEXT("Hitstop"), P->Hitstop);
		O.AddInt(K + TEXT("ComboCounter"), P->ComboCounter);
		O.AddInt(K + TEXT("OnScreen"), (P->PlayerFlags & PLF_IsOnScreen) != 0);
		O.AddInt(K + TEXT("PlayerFlags"), P->PlayerFlags);
		O.AddInt(K + TEXT("MiscFlags"), P->MiscFlags);
		O.AddInt(K + TEXT("AttackFlags"), P->AttackFlags);
		O.AddInt(K + TEXT("ActionTime"), P->ActionTime);
		O.AddInt(K + TEXT("Stance"), P->Stance);
		O.AddInt(K + TEXT("TeamIndex"), P->TeamIndex);
		O.Add(K + TEXT("State"), StateName(P));
		O.AddInt(K + TEXT("Counter"), Counter(P));
		O.Add(K + TEXT("Enemy"), Id(B, P->Enemy));
		O.Add(K + TEXT("AttackOwner"), Id(B, P->AttackOwner));
		O.AddInt(K + TEXT("IgnoreCount"), P->ObjectsToIgnoreHitsFrom.Num());
		for (int32 J = 0; J < P->ObjectsToIgnoreHitsFrom.Num(); ++J)
		{
			O.Add(K + FString::Printf(TEXT("Ignore%d"), J), Id(B, P->ObjectsToIgnoreHitsFrom[J]));
		}
		for (int32 J = 0; J < 16; ++J)
		{
			if (P->StoredBattleObjects[J]) O.Add(K + FString::Printf(TEXT("Stored%d"), J), Id(B, P->StoredBattleObjects[J]));
		}
		O.AddInt(K + TEXT("InputHash"), InputHistoryHash(P));
		O.AddInt(K + TEXT("BoxCount"), P->Boxes.Num());
	}
	for (int32 I = 0; I < B.Game->Objects.Num(); ++I)
	{
		const ABattleObject* Obj = B.Game->Objects[I];
		const FString K = FString::Printf(TEXT("O%d."), I);
		O.AddInt(K + TEXT("IsActive"), Obj->IsActive);
		if (!Obj->IsActive) continue;
		O.AddInt(K + TEXT("PosX"), Obj->PosX);
		O.AddInt(K + TEXT("PosY"), Obj->PosY);
		O.AddInt(K + TEXT("Direction"), Obj->Direction);
		O.AddInt(K + TEXT("Hitstop"), Obj->Hitstop);
		O.AddInt(K + TEXT("AttackFlags"), Obj->AttackFlags);
		O.AddInt(K + TEXT("MiscFlags"), Obj->MiscFlags);
		O.AddInt(K + TEXT("ActionTime"), Obj->ActionTime);
		O.AddInt(K + TEXT("ObjectStateIndex"), Obj->ObjectStateIndex);
		O.AddInt(K + TEXT("HitDamage"), Obj->NormalHit.Damage);
		O.Add(K + TEXT("Owner"), Id(B, Obj->Player));
		O.Add(K + TEXT("AttackTarget"), Id(B, Obj->AttackTarget));
		O.Add(K + TEXT("PositionLink"), Id(B, Obj->PositionLinkObj));
		O.Add(K + TEXT("StopLink"), Id(B, Obj->StopLinkObj));
		O.AddInt(K + TEXT("IgnoreCount"), Obj->ObjectsToIgnoreHitsFrom.Num());
		O.AddInt(K + TEXT("BoxCount"), Obj->Boxes.Num());
		if (Obj->Boxes.Num())
		{
			O.AddInt(K + TEXT("Box0Type"), Obj->Boxes[0].Type);
			O.AddInt(K + TEXT("Box0SizeX"), Obj->Boxes[0].SizeX);
			O.AddInt(K + TEXT("Box0SizeY"), Obj->Boxes[0].SizeY);
		}
		O.Add(K + TEXT("ScriptClass"), Obj->ObjectState ? Obj->ObjectState->GetClass()->GetName() : TEXT("none"));
		if (const UNSE010Script* Sc = ScriptOf(Obj))
		{
			O.AddInt(K + TEXT("Counter"), Sc->Counter);
			O.AddInt(K + TEXT("Damage"), Sc->Damage);
			O.AddInt(K + TEXT("Travel"), Sc->Travel);
			O.AddInt(K + TEXT("TraceId"), Sc->TraceId);
			O.AddInt(K + TEXT("Armed"), Sc->bArmed);
			O.AddInt(K + TEXT("DeactivateOnHit"), Sc->bDeactivateOnHit);
			O.AddInt(K + TEXT("HitstopFrames"), Sc->HitstopFrames);
		}
	}
	return O;
}

// Compares two observation vectors and reports the first differences.
inline bool ExpectObsEqual(FAutomationTestBase& T, const FString& Label, const FObs& Actual, const FObs& Expected)
{
	if (Actual.Keys != Expected.Keys)
	{
		T.AddError(FString::Printf(TEXT("%s: observation shapes differ (%d vs %d keys)"), *Label, Actual.Keys.Num(),
								   Expected.Keys.Num()));
		return false;
	}
	int32 Reported = 0;
	for (int32 I = 0; I < Actual.Keys.Num(); ++I)
	{
		if (Actual.Vals[I] != Expected.Vals[I])
		{
			if (Reported < 6)
			{
				T.AddError(FString::Printf(TEXT("%s: %s is %s, expected %s"), *Label, *Actual.Keys[I], *Actual.Vals[I],
										   *Expected.Vals[I]));
			}
			++Reported;
		}
	}
	if (Reported > 6)
	{
		T.AddInfo(FString::Printf(TEXT("%s: %d more differences not listed"), *Label, Reported - 6));
	}
	return Reported == 0;
}
inline bool ExpectObsEqual(FAutomationTestBase& T, const FString& Label, const FNSE010Battle& Actual,
						   const FNSE010Battle& Expected)
{
	return ExpectObsEqual(T, Label, Observe(Actual), Observe(Expected));
}
inline bool ObsDiffer(const FNSE010Battle& X, const FNSE010Battle& Y)
{
	const FObs A = Observe(X), B = Observe(Y);
	return A.Keys != B.Keys || A.Vals != B.Vals;
}

inline FString TraceString(const TArray<int32>& Trace, int32 From)
{
	return IntArrayString(Trace, FMath::Clamp(From, 0, Trace.Num()));
}
inline TArray<int32> TraceSince(const FNSE010Battle& B, int32 Cursor)
{
	TArray<int32> Out;
	for (int32 I = FMath::Clamp(Cursor, 0, B.Trace.Num()); I < B.Trace.Num(); ++I) Out.Add(B.Trace[I]);
	return Out;
}
inline bool ExpectTraceEqual(FAutomationTestBase& T, const FString& Label, const FNSE010Battle& Actual, int32 CursorA,
							 const FNSE010Battle& Expected, int32 CursorE)
{
	const TArray<int32> A = TraceSince(Actual, CursorA), E = TraceSince(Expected, CursorE);
	if (A != E)
	{
		T.AddError(FString::Printf(TEXT("%s: trace since cursor is %s, expected %s"), *Label, *IntArrayString(A),
								   *IntArrayString(E)));
		return false;
	}
	return true;
}
inline bool ExpectTraceIs(FAutomationTestBase& T, const FString& Label, const FNSE010Battle& Actual, int32 Cursor,
						  const TArray<int32>& Expected)
{
	const TArray<int32> A = TraceSince(Actual, Cursor);
	if (A != Expected)
	{
		T.AddError(FString::Printf(TEXT("%s: trace since cursor is %s, expected %s"), *Label, *IntArrayString(A),
								   *IntArrayString(Expected)));
		return false;
	}
	return true;
}

inline bool ExpectSameBytes(FAutomationTestBase& T, const FString& Label, const FBytes& X, const FBytes& Y)
{
	if (X.Num() != Y.Num())
	{
		T.AddError(FString::Printf(TEXT("%s: byte arrays differ in length (%d vs %d)"), *Label, X.Num(), Y.Num()));
		return false;
	}
	for (int32 I = 0; I < X.Num(); ++I)
	{
		if (X[I] != Y[I])
		{
			T.AddError(FString::Printf(TEXT("%s: byte arrays differ at index %d of %d (%d vs %d)"), *Label, I, X.Num(),
									   X[I], Y[I]));
			return false;
		}
	}
	return true;
}
inline bool ExpectDifferentBytes(FAutomationTestBase& T, const FString& Label, const FBytes& X, const FBytes& Y)
{
	if (X == Y)
	{
		T.AddError(FString::Printf(TEXT("%s: byte arrays are identical (%d bytes) but must differ"), *Label, X.Num()));
		return false;
	}
	return true;
}
inline bool ExpectPayload(FAutomationTestBase& T, const FString& Label, const FBytes& X)
{
	if (X.Num() <= 12)
	{
		T.AddError(FString::Printf(TEXT("%s: export has %d bytes, expected a header plus payload"), *Label, X.Num()));
		return false;
	}
	return true;
}
inline bool ExpectHeader(FAutomationTestBase& T, const FString& Label, const FBytes& X)
{
	static const uint8 Expected[12] = {'N', 'S', 'K', 'Y', 'C', 'K', 'P', 'T', 1, 0, 0, 0};
	if (X.Num() < 12)
	{
		T.AddError(FString::Printf(TEXT("%s: export has %d bytes, shorter than the 12 byte header"), *Label, X.Num()));
		return false;
	}
	for (int32 I = 0; I < 12; ++I)
	{
		if (X[I] != Expected[I])
		{
			T.AddError(FString::Printf(TEXT("%s: header byte %d is %d, expected %d"), *Label, I, X[I], Expected[I]));
			return false;
		}
	}
	return true;
}
inline FBytes WithVersion(FBytes Bytes, uint32 Version)
{
	if (Bytes.Num() < 12) Bytes.SetNumZeroed(12);
	Bytes[8] = Version & 0xFF;
	Bytes[9] = (Version >> 8) & 0xFF;
	Bytes[10] = (Version >> 16) & 0xFF;
	Bytes[11] = (Version >> 24) & 0xFF;
	return Bytes;
}

// Input tapes.
struct FInputs
{
	int32 In1 = INP_Neutral;
	int32 In2 = INP_Neutral;
};
using FTape = TArray<FInputs>;

inline FTape Constant(int32 Frames, int32 In1 = INP_Neutral, int32 In2 = INP_Neutral)
{
	FTape Tape;
	for (int32 I = 0; I < Frames; ++I) Tape.Add({In1, In2});
	return Tape;
}
inline FTape Slice(const FTape& Tape, int32 From, int32 To)
{
	FTape Out;
	for (int32 I = From; I < To && I < Tape.Num(); ++I) Out.Add(Tape[I]);
	return Out;
}
// T1: P1 shoots on frame 5 then walks toward P2; P2 walks toward P1. 40 frames, one hit on P2.
inline FTape TapeT1()
{
	FTape Tape = Constant(40);
	Tape[5].In1 = INP_A;
	for (int32 I = 6; I <= 20; ++I) Tape[I].In1 = INP_Right;
	for (int32 I = 11; I <= 30; ++I) Tape[I].In2 = INP_Right;
	return Tape;
}
// T2: P1 walks right from frame 1, P2 shoots on frame 2 at close range. 30 frames.
inline FTape TapeT2()
{
	FTape Tape = Constant(30);
	for (int32 I = 1; I <= 10; ++I) Tape[I].In1 = INP_Right;
	Tape[2].In2 = INP_A;
	for (int32 I = 3; I <= 15; ++I) Tape[I].In2 = INP_Right;
	return Tape;
}
// T3: like T2 with P1 walking left instead of right.
inline FTape TapeT3()
{
	FTape Tape = TapeT2();
	for (int32 I = 1; I <= 10; ++I) Tape[I].In1 = INP_Left;
	return Tape;
}
// T90: two P1 shots (frames 5 and 40), walks on both sides. 90 frames.
inline FTape TapeT90()
{
	FTape Tape = Constant(90);
	Tape[5].In1 = INP_A;
	for (int32 I = 6; I <= 20; ++I) Tape[I].In1 = INP_Right;
	Tape[40].In1 = INP_A;
	for (int32 I = 41; I <= 60; ++I) Tape[I].In1 = INP_Left;
	for (int32 I = 11; I <= 30; ++I) Tape[I].In2 = INP_Right;
	return Tape;
}
inline void Run(FNSE010Battle& B, const FTape& Tape)
{
	for (const FInputs& In : Tape) B.Step(In.In1, In.In2);
}
inline void RunAll(std::initializer_list<FNSE010Battle*> Battles, const FTape& Tape)
{
	for (const FInputs& In : Tape)
	{
		for (FNSE010Battle* B : Battles) B->Step(In.In1, In.In2);
	}
}

// After an import, steps Target and Control (and optionally Source) in lockstep and
// compares the observation vector and the trace suffix on every frame. Reports only the
// first mismatching frame so one wrong field does not flood the log, but keeps stepping
// every battle through the whole tape so later control assertions stay meaningful.
inline bool ExpectFollows(FAutomationTestBase& T, const FString& Label, FNSE010Battle& Target, FNSE010Battle& Control,
						  const FTape& Tape, FNSE010Battle* Source = nullptr)
{
	const int32 CursorT = Target.Trace.Num(), CursorC = Control.Trace.Num();
	const int32 CursorS = Source ? Source->Trace.Num() : 0;
	bool bOk = true;
	for (int32 F = 0; F <= Tape.Num(); ++F)
	{
		if (F > 0)
		{
			Target.Step(Tape[F - 1].In1, Tape[F - 1].In2);
			Control.Step(Tape[F - 1].In1, Tape[F - 1].In2);
			if (Source) Source->Step(Tape[F - 1].In1, Tape[F - 1].In2);
		}
		if (!bOk) continue;
		const FString Frame = FString::Printf(TEXT("%s (frame %d after import, battle frame %d)"), *Label, F,
											  Control.Game->BattleState.FrameNumber);
		bOk = ExpectObsEqual(T, Frame, Target, Control) && ExpectTraceEqual(T, Frame, Target, CursorT, Control, CursorC);
		if (bOk && Source)
		{
			bOk = ExpectObsEqual(T, Frame + TEXT(" [source vs control]"), *Source, Control) &&
				  ExpectTraceEqual(T, Frame + TEXT(" [source vs control]"), *Source, CursorS, Control, CursorC);
		}
	}
	return bOk;
}

inline void SetTemplateTravel(FNSE010Battle& B, int32 Travel)
{
	for (APlayerObject* P : B.Players) B.ShotTemplate(P)->Travel = Travel;
}
} // namespace NSE010
