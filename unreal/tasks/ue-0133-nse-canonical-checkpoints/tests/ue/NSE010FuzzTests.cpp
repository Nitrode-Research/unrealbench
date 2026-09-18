// NSE010 portable checkpoints: seeded stateful fuzz. Three hoisted battles: A is the
// source, C the control (both re-armed through Reset every episode), B the target that is
// never reset and so carries the previous episode's state and slot order into every
// import. Every operation is addressed logically (side, slot, index) and applied to all
// battles alike, so the control is the oracle for every observation after an import.
// -FuzzSeed=<int> and -FuzzIterations=<int> override the defaults.
#include "NSE010TestCommon.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Math/RandomStream.h"

using namespace NSE010;

namespace
{
struct FNSE010Histogram
{
	TMap<FString, int32> Counts;
	void Bump(const FString& Key)
	{
		Counts.FindOrAdd(Key)++;
	}
	int32 Get(const FString& Key) const
	{
		const int32* Found = Counts.Find(Key);
		return Found ? *Found : 0;
	}
	FString ToString() const
	{
		TArray<FString> Keys;
		Counts.GetKeys(Keys);
		Keys.Sort();
		FString Out;
		for (const FString& Key : Keys)
		{
			Out += FString::Printf(TEXT("%s%s=%d"), Out.IsEmpty() ? TEXT("") : TEXT(" "), *Key, Counts[Key]);
		}
		return Out;
	}
};

int32 DrawInput(FRandomStream& Rand)
{
	const float R = Rand.GetFraction();
	if (R < 0.40f) return INP_Neutral;
	if (R < 0.55f) return INP_Left;
	if (R < 0.70f) return INP_Right;
	if (R < 0.85f) return INP_A;
	if (R < 0.90f) return INP_Left | INP_A;
	return INP_Right | INP_A;
}
FString InputName(int32 In)
{
	FString S;
	if (In & INP_Left) S += TEXT("L");
	if (In & INP_Right) S += TEXT("R");
	if (In & INP_A) S += TEXT("A");
	return S.IsEmpty() ? TEXT("N") : S;
}
bool PoolFull(const FNSE010Battle& B)
{
	for (const ABattleObject* Obj : B.Game->Objects)
	{
		if (!Obj->IsActive) return false;
	}
	return true;
}
TArray<int32> ActiveSlots(const FNSE010Battle& B)
{
	TArray<int32> Slots;
	for (int32 I = 0; I < B.Game->Objects.Num(); ++I)
	{
		if (B.Game->Objects[I]->IsActive) Slots.Add(I);
	}
	return Slots;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE010Fuzz, "UnrealBench.NSE010.Fuzz",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE010Fuzz::RunTest(const FString&)
{
	int32 Seed = 10133;
	FParse::Value(FCommandLine::Get(), TEXT("-FuzzSeed="), Seed);
	int32 Episodes = 120;
	FParse::Value(FCommandLine::Get(), TEXT("-FuzzIterations="), Episodes);
	Episodes = FMath::Max(1, Episodes);
	const double StartSeconds = FPlatformTime::Seconds();
	const double Deadline = StartSeconds + 200.0;

	FNSE010Lineup Lineup;
	Lineup.PoolCapacity = 6;
	Lineup.bWithExtension = true;
	TUniquePtr<FNSE010Battle> A = MakeUnique<FNSE010Battle>(Lineup);
	TUniquePtr<FNSE010Battle> B = MakeUnique<FNSE010Battle>(Lineup);
	TUniquePtr<FNSE010Battle> C = MakeUnique<FNSE010Battle>(Lineup);
	FRandomStream Rand(Seed);
	FNSE010Histogram H;
	TArray<FString> Recent;
	int32 Failures = 0;
	int32 EpisodesRun = 0;
	bool bOutOfTime = false;

	auto Log = [&](const FString& Op) {
		Recent.Add(Op);
		if (Recent.Num() > 16) Recent.RemoveAt(0);
	};
	auto Fail = [&](int32 Episode, int32 Frame, const TCHAR* Requirement, const FString& What) {
		AddError(FString::Printf(TEXT("Fuzz seed %d episode %d frame %d violates %s: %s; recent operations: %s"), Seed, Episode,
								 Frame, Requirement, *What, *FString::Join(Recent, TEXT(" | "))));
		++Failures;
	};

	for (int32 E = 0; E < Episodes && Failures < 4 && !bOutOfTime; ++E)
	{
		A->Reset();
		C->Reset();
		Recent.Reset();
		++EpisodesRun;
		if (!ExpectObsEqual(*this, FString::Printf(TEXT("Fuzz re-arm before episode %d: source equals control after Reset"), E), *A, *C))
		{
			++Failures;
			continue;
		}
		{
			const FBytes ResetA = Export(*A), ResetC = Export(*C);
			if (ResetA != ResetC)
			{
				Fail(E, 0, TEXT("R2"), FString::Printf(TEXT("exports after Reset differ (%d vs %d bytes)"), ResetA.Num(), ResetC.Num()));
				continue;
			}
		}
		const int32 Frames = Rand.RandRange(40, 160);
		int32 ExportsDone = 0;
		bool bImported = false;
		int32 CursorB = 0, CursorC = 0;
		FBytes LastExport;
		bool bWasFull = false;
		bool bEpisodeFailed = false;
		int32 PrevIn1 = INP_Neutral, PrevIn2 = INP_Neutral;

		auto Apply = [&](TFunctionRef<void(FNSE010Battle&)> Fn) {
			Fn(*A);
			Fn(*C);
			if (bImported) Fn(*B);
		};

		auto ExportPoint = [&](const FString& Kind, int32 F) {
			const FBytes BytesA = Export(*A), BytesC = Export(*C);
			if (BytesA.Num() <= 12)
			{
				Fail(E, F, TEXT("R2"), FString::Printf(TEXT("export has %d bytes, expected a header plus payload"), BytesA.Num()));
				return false;
			}
			if (BytesA != BytesC)
			{
				Fail(E, F, TEXT("R2"), TEXT("twin exports (source vs control) differ"));
				return false;
			}
			if (LastExport.Num() && LastExport == BytesA)
			{
				Fail(E, F, TEXT("R3"), TEXT("export equals the previous export point of this episode despite a later frame"));
				return false;
			}
			LastExport = BytesA;
			const int32 TraceBeforeImport = B->Trace.Num();
			const EBattleCheckpointResult Result = Import(*B, BytesA);
			if (Result != EBattleCheckpointResult::Restored)
			{
				Fail(E, F, TEXT("R4"), FString::Printf(TEXT("import into the target returned %s"), *ResultName(Result)));
				return false;
			}
			if (B->Trace.Num() != TraceBeforeImport)
			{
				Fail(E, F, TEXT("R4"), TEXT("the import fired a fixture callback"));
				return false;
			}
			bImported = true;
			CursorB = B->Trace.Num();
			CursorC = C->Trace.Num();
			if (!ExpectObsEqual(*this, FString::Printf(TEXT("Fuzz seed %d episode %d frame %d right after import (%s)"), Seed, E, F, *Kind), *B, *C))
			{
				Fail(E, F, TEXT("R4/R5"), TEXT("target observations differ from control right after the import"));
				return false;
			}
			if (SlotOrder(*B) != SlotOrder(*C))
			{
				Fail(E, F, TEXT("R6"), FString::Printf(TEXT("pool update order %s, expected %s"), *IntArrayString(SlotOrder(*B)),
													   *IntArrayString(SlotOrder(*C))));
				return false;
			}
			const FBytes BytesB = Export(*B);
			if (BytesB != BytesA)
			{
				Fail(E, F, TEXT("R4"), FString::Printf(TEXT("export right after the import differs from the imported bytes (%d vs %d bytes)"),
													   BytesB.Num(), BytesA.Num()));
				return false;
			}
			// Importing the same bytes again changes nothing and fires nothing.
			if (Import(*B, BytesA) != EBattleCheckpointResult::Restored || Export(*B) != BytesA || B->Trace.Num() != CursorB)
			{
				Fail(E, F, TEXT("R4"), TEXT("a second import of the same bytes changed the target or fired a callback"));
				return false;
			}
			// One corrupted copy must be rejected and leave the target alone. A version byte is
			// UnsupportedVersion, a magic byte Malformed; payload damage may be reported as
			// Malformed or LineupMismatch, never Restored.
			const int32 Pos = Rand.GetFraction() < 0.3f ? Rand.RandRange(0, 15) : Rand.RandRange(0, BytesA.Num() - 1);
			const uint8 Mask = static_cast<uint8>(Rand.RandRange(1, 255));
			FBytes Corrupt = BytesA;
			Corrupt[Pos] ^= Mask;
			const EBattleCheckpointResult CorruptResult = Import(*B, Corrupt);
			const bool bVersionByte = Pos >= 8 && Pos <= 11;
			const bool bAccepted = bVersionByte ? CorruptResult == EBattleCheckpointResult::UnsupportedVersion
								   : Pos < 8	? CorruptResult == EBattleCheckpointResult::Malformed
											   : (CorruptResult == EBattleCheckpointResult::Malformed ||
												  CorruptResult == EBattleCheckpointResult::LineupMismatch);
			if (!bAccepted)
			{
				Fail(E, F, TEXT("R7"), FString::Printf(TEXT("byte %d xor 0x%02x returned %s, expected %s"), Pos, Mask, *ResultName(CorruptResult),
													   bVersionByte ? TEXT("UnsupportedVersion") : Pos < 8 ? TEXT("Malformed") : TEXT("Malformed or LineupMismatch")));
				return false;
			}
			if (Export(*B) != BytesA)
			{
				Fail(E, F, TEXT("R7"), FString::Printf(TEXT("export changed after the rejected corruption at byte %d"), Pos));
				return false;
			}
			H.Bump(TEXT("export_") + Kind);
			++ExportsDone;
			Log(FString::Printf(TEXT("f%d export(%s)"), F, *Kind));
			return true;
		};

		for (int32 F = 0; F < Frames; ++F)
		{
			const int32 TraceBefore = C->Trace.Num();
			bool bSpawned = false;
			if (Rand.GetFraction() < 0.02f)
			{
				const int32 Side = Rand.RandRange(0, 1);
				const int32 Value = Rand.RandRange(0, 900);
				Apply([&](FNSE010Battle& X) {
					if (UNSE010Script* S = ScriptOf(X.Players[Side])) S->RandomWalk = Value;
				});
				H.Bump(TEXT("randomwalk"));
				Log(FString::Printf(TEXT("f%d randomwalk P%d=%d"), F, Side, Value));
			}
			if (Rand.GetFraction() < 0.01f)
			{
				const int32 Side = Rand.RandRange(0, 1);
				const int32 K = Rand.RandRange(1, 5);
				const int32 Duration = Rand.RandRange(5, 20);
				const int32 At = Counter(C->Players[Side]) + K;
				Apply([&](FNSE010Battle& X) {
					if (UNSE010Script* S = ScriptOf(X.Players[Side]))
					{
						S->FreezeAtCounter = At;
						S->FreezeDuration = Duration;
						S->FreezeSelfDuration = 0;
					}
				});
				H.Bump(TEXT("freeze_armed"));
				Log(FString::Printf(TEXT("f%d freeze P%d at=%d dur=%d"), F, Side, At, Duration));
			}
			if (Rand.GetFraction() < 0.03f)
			{
				const int32 Side = Rand.RandRange(0, 1);
				const int32 Dir = Side == 0 ? 1 : -1;
				const APlayerObject* Own = C->Players[Side];
				const APlayerObject* Enemy = C->Players[1 - Side];
				const int32 Choice = Rand.RandRange(0, 5);
				int32 X = 0;
				switch (Choice)
				{
				case 0: X = Own->PosX + 60000 * Dir; break;
				case 1: X = Enemy->PosX - 40000 * Dir; break; // hitbox edge touching the hurtbox edge
				case 2: X = Enemy->PosX - 39000 * Dir; break; // one thousand inside
				case 3: X = Enemy->PosX; break;
				case 4: X = Enemy->PosX + 200000 * Dir; break; // behind the enemy
				default: X = Rand.RandRange(-200000, 200000); break;
				}
				const int32 Y = Rand.GetFraction() < 0.5f ? 50000 : (Rand.GetFraction() < 0.5f ? 80000 : 60000);
				const int32 DamageChoice[3] = {50, 100, 250};
				const int32 TravelChoice[3] = {0, 2000, 4000};
				const int32 Damage = DamageChoice[Rand.RandRange(0, 2)];
				const int32 Travel = TravelChoice[Rand.RandRange(0, 2)];
				const bool bDeactivate = Rand.GetFraction() < 0.5f;
				const int32 HitstopFrames = Rand.GetFraction() < 0.5f ? 3 : 6;
				ABattleObject* OC = C->Spawn(C->Players[Side], X, Y);
				ABattleObject* OA = A->Spawn(A->Players[Side], X, Y);
				ABattleObject* OB = bImported ? B->Spawn(B->Players[Side], X, Y) : nullptr;
				if (!OC)
				{
					H.Bump(TEXT("spawn_refused"));
					Log(FString::Printf(TEXT("f%d spawn refused P%d"), F, Side));
					if (OA)
					{
						Fail(E, F, TEXT("driver"), TEXT("control refused a spawn the source accepted"));
						bEpisodeFailed = true;
						break;
					}
					if (bImported && OB)
					{
						Fail(E, F, TEXT("R6"), FString::Printf(TEXT("the target had a free slot (%s) where the control's pool was full"), *Id(*B, OB)));
						bEpisodeFailed = true;
						break;
					}
				}
				else
				{
					auto Configure = [&](ABattleObject* Obj) {
						if (UNSE010Script* S = ScriptOf(Obj))
						{
							S->Damage = Damage;
							S->Travel = Travel;
							S->bDeactivateOnHit = bDeactivate;
							S->HitstopFrames = HitstopFrames;
						}
					};
					Configure(OC);
					Configure(OA);
					Configure(OB);
					if (!OA || Id(*A, OA) != Id(*C, OC))
					{
						Fail(E, F, TEXT("driver"), TEXT("source and control spawned into different slots"));
						bEpisodeFailed = true;
						break;
					}
					if (bImported && (!OB || Id(*B, OB) != Id(*C, OC)))
					{
						Fail(E, F, TEXT("R6"), FString::Printf(TEXT("target spawned into %s, control into %s"), OB ? *Id(*B, OB) : TEXT("nothing"), *Id(*C, OC)));
						bEpisodeFailed = true;
						break;
					}
					bSpawned = true;
					H.Bump(TEXT("spawn"));
					Log(FString::Printf(TEXT("f%d spawn P%d at (%d,%d) dmg=%d travel=%d deact=%d hs=%d -> %s"), F, Side, X, Y, Damage,
										Travel, bDeactivate, HitstopFrames, *Id(*C, OC)));
				}
			}
			if (Rand.GetFraction() < 0.02f)
			{
				const TArray<int32> Slots = ActiveSlots(*C);
				const int32 Side = Rand.RandRange(0, 1);
				const int32 Index = Rand.RandRange(0, 15);
				const int32 Slot = Slots.Num() ? Slots[Rand.RandRange(0, Slots.Num() - 1)] : -1;
				const bool bFollow = Rand.GetFraction() < 0.5f;
				if (Slot >= 0)
				{
					Apply([&](FNSE010Battle& X) {
						X.Store(X.Players[Side], X.Game->Objects[Slot], Index);
						if (UNSE010Script* S = ScriptOf(X.Players[Side])) S->FollowStoredIndex = bFollow ? Index : -1;
					});
					H.Bump(TEXT("store"));
					Log(FString::Printf(TEXT("f%d store P%d slot %d at %d follow=%d"), F, Side, Slot, Index, bFollow));
				}
			}
			if (Rand.GetFraction() < 0.02f)
			{
				const TArray<int32> Slots = ActiveSlots(*C);
				if (Slots.Num())
				{
					const int32 Slot = Slots[Rand.RandRange(0, Slots.Num() - 1)];
					Apply([&](FNSE010Battle& X) { X.Game->Objects[Slot]->DeactivateObject(); });
					H.Bump(TEXT("deactivate"));
					Log(FString::Printf(TEXT("f%d deactivate slot %d"), F, Slot));
				}
			}
			const int32 In1 = DrawInput(Rand), In2 = DrawInput(Rand);
			H.Bump(TEXT("in1_") + InputName(In1));
			H.Bump(TEXT("in2_") + InputName(In2));
			if ((In1 & INP_A) && !(PrevIn1 & INP_A) && (In2 & INP_A) && !(PrevIn2 & INP_A)) H.Bump(TEXT("double_press"));
			PrevIn1 = In1;
			PrevIn2 = In2;
			Log(FString::Printf(TEXT("f%d in=(%s,%s)"), F, *InputName(In1), *InputName(In2)));
			A->Step(In1, In2);
			C->Step(In1, In2);
			if (bImported) B->Step(In1, In2);

			if (bImported)
			{
				if (!ExpectObsEqual(*this, FString::Printf(TEXT("Fuzz seed %d episode %d frame %d target vs control"), Seed, E, F), *B, *C) ||
					!ExpectTraceEqual(*this, FString::Printf(TEXT("Fuzz seed %d episode %d frame %d target vs control"), Seed, E, F), *B,
									  CursorB, *C, CursorC))
				{
					Fail(E, F, TEXT("R4/R5/R6"), TEXT("target diverged from control after the import"));
					bEpisodeFailed = true;
					break;
				}
			}
			if (F % 8 == 0 && (!ExpectObsEqual(*this, FString::Printf(TEXT("Fuzz driver episode %d frame %d source vs control"), E, F), *A, *C) ||
							   A->Trace != C->Trace))
			{
				Fail(E, F, TEXT("driver"), TEXT("source and control diverged (fixture or driver defect)"));
				bEpisodeFailed = true;
				break;
			}

			const bool bHit = C->Trace.Num() > TraceBefore;
			if (bHit) H.Bump(TEXT("hit"));
			const bool bFreeze = C->Game->BattleState.SuperFreezeDuration > 0;
			if (bFreeze) H.Bump(TEXT("freeze_frames"));
			const bool bFull = PoolFull(*C);
			if (C->Players[0]->CurrentHealth < 2000 || C->Players[1]->CurrentHealth < 2000)
			{
				H.Bump(TEXT("early_stop"));
				break;
			}
			if (ExportsDone < 3)
			{
				FString Kind = TEXT("random");
				float P = 0.01f;
				if (bFreeze)
				{
					Kind = TEXT("freeze");
					P = 0.3f;
				}
				else if (bHit)
				{
					Kind = TEXT("hit");
					P = 0.6f;
				}
				else if (bFull && !bWasFull)
				{
					Kind = TEXT("full");
					P = 0.7f;
				}
				else if (bSpawned)
				{
					Kind = TEXT("spawn");
					P = 0.3f;
				}
				if (Rand.GetFraction() < P && !ExportPoint(Kind, F))
				{
					bEpisodeFailed = true;
					break;
				}
			}
			bWasFull = bFull;
			if (FPlatformTime::Seconds() > Deadline)
			{
				bOutOfTime = true;
				break;
			}
		}
		if (bEpisodeFailed || bOutOfTime) continue;
		if (ExportsDone == 0 && !ExportPoint(TEXT("end"), Frames)) continue;
		if (bImported)
		{
			const FBytes EndA = Export(*A), EndB = Export(*B), EndC = Export(*C);
			if (EndA != EndB || EndA != EndC)
			{
				Fail(E, Frames, TEXT("R2/R4"), TEXT("source, target and control exports differ at the episode end"));
			}
		}
	}

	const double Elapsed = FPlatformTime::Seconds() - StartSeconds;
	AddInfo(FString::Printf(TEXT("Fuzz seed %d: %d episodes in %.1f s, histogram: %s"), Seed, EpisodesRun, Elapsed, *H.ToString()));
	if (bOutOfTime) AddInfo(TEXT("Fuzz stopped at the 200 s deadline"));
	TestTrue(FString::Printf(TEXT("Fuzz ran at least 30 episodes (%d)"), EpisodesRun), EpisodesRun >= 30);
	const TArray<FString> RequiredKinds = {TEXT("export_hit"), TEXT("export_spawn"), TEXT("export_freeze"), TEXT("export_full")};
	for (const FString& Kind : RequiredKinds)
	{
		TestTrue(FString::Printf(TEXT("Fuzz export point kind %s occurred at least once"), *Kind), H.Get(Kind) > 0);
	}
	TestTrue(TEXT("Fuzz finished under the 5 minute budget"), Elapsed < 290.0);
	return !HasAnyErrors();
}
