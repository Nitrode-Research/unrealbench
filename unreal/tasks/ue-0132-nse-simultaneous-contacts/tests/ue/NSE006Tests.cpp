#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"
#include "NightSkyEngine/Fixtures/NSE006Fixture.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"

// Hidden suite for ue_task_0132 (NSE-006): order-independent simultaneous contacts. Every test drives the real
// battle frame through FNSE006Battle and reads public fields plus the fixture's callback trace. Expected values in
// the example tests are hand-derived; the fuzz tests use the Oracle namespace below, which never calls engine code.

namespace
{
using EEvent = ENSE006Event;
constexpr int32 FX = FNSE006Battle::FighterX;
constexpr int32 ClashBits = ENB_ForwardDash | ENB_NormalAttack;

FCollisionBox Hurt()
{
	return FNSE006Battle::Box();
}
FCollisionBox HitBox()
{
	return FNSE006Battle::Box(4, 4, BOX_Hit);
}
// Fighter strike that touches the opponent's hurtbox: P1 covers [49997, 50001], P2 the mirror.
FCollisionBox Reach()
{
	return FNSE006Battle::Box(4, 4, BOX_Hit, 100100, 0);
}
// Fighter strike over the middle grid: P1 covers [-11, 9], P2 [-9, 11]; the two overlap and touch no hurtbox.
FCollisionBox Centre()
{
	return FNSE006Battle::Box(20, 4, BOX_Hit, 50050, 0);
}
// P1 strike covering [-1, 50001]: overlaps P2's centre box and P2's hurtbox.
FCollisionBox ClashPlusReach()
{
	return FNSE006Battle::Box(50052, 4, BOX_Hit, 75076, 0);
}
// The plugin's native tags are not exported to the game module, so the tests resolve them by name.
FGameplayTag Tag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(FName(Name));
}
FGameplayTag PrimaryMachine()
{
	return Tag(TEXT("StateMachine.Primary"));
}
FGameplayTag HitstunTag(int32 Level)
{
	return Tag(*FString::Printf(TEXT("State.Universal.Hitstun.%d"), Level));
}
FGameplayTag StandBlockTag()
{
	return Tag(TEXT("State.Universal.Block.Stand"));
}
// Fighter strike on top of the fighter's hurtbox.
void Arm(FNSE006Battle& B, APlayerObject* P, TArray<FCollisionBox> Hits, int32 Damage = 100, int32 Priority = 0,
		 int32 Level = 0, int32 Hitstop = 3)
{
	B.Strike(P, Damage, Priority, Level, Hitstop);
	Hits.Insert(Hurt(), 0);
	B.Boxes(P, Hits);
}
// Attacking object with a hurtbox and a 4x4 hit box.
ABattleObject* Projectile(FNSE006Battle& B, APlayerObject* Owner, int32 Key, int32 X, int32 Damage = 100,
						  int32 Priority = 0, int32 Level = 0, int32 Hitstop = 3, int32 Slot = -1)
{
	auto* O = B.Spawn(Owner, Key, X, 0, Slot);
	B.Strike(O, Damage, Priority, Level, Hitstop);
	B.Boxes(O, {Hurt(), HitBox()});
	return O;
}
// Passive object with a hurtbox only.
ABattleObject* Victim(FNSE006Battle& B, APlayerObject* Owner, int32 Key, int32 X, int32 Slot = -1)
{
	return B.Spawn(Owner, Key, X, 0, Slot);
}
bool HitActive(const ABattleObject* O)
{
	return (O->AttackFlags & ATK_HitActive) != 0;
}
// TargetHealth of the first matching trace entry, -2 when there is none.
int32 HealthAt(const FNSE006Battle& B, EEvent Kind, int32 Actor, int32 Other)
{
	for (const auto& E : B.Trace)
	{
		if (E.Kind == Kind && E.Actor == Actor && E.Other == Other)
		{
			return E.TargetHealth;
		}
	}
	return -2;
}
// A contact's callbacks fire during its application; whether they see the receiving fighter's health before or
// after that contact's own damage is not pinned, so either value is accepted.
void CheckHealthAt(FAutomationTestBase& T, const FString& Label, const FNSE006Battle& B, EEvent Kind, int32 Actor,
				   int32 Other, int32 Before, int32 After)
{
	const int32 Got = HealthAt(B, Kind, Actor, Other);
	T.TestTrue(*FString::Printf(TEXT("%s: callback-time health %d or %d, got %d"), *Label, Before, After, Got),
			   Got == Before || Got == After);
}
TArray<FString> EntryNames(const TArray<FNSE006StateEntry>& Entries)
{
	TArray<FString> Names;
	for (const auto& E : Entries)
	{
		Names.Add(FString::Printf(TEXT("P%d:%s"), E.Player + 1, *E.State.ToString()));
	}
	Names.Sort();
	return Names;
}
// The state each fighter last entered, as "P<n>:<state>", sorted; fighters that entered nothing are absent.
TArray<FString> LastEntryNames(const TArray<FNSE006StateEntry>& Entries)
{
	FString Last[2];
	for (const auto& E : Entries)
	{
		if (E.Player == 0 || E.Player == 1)
		{
			Last[E.Player] = E.State.ToString();
		}
	}
	TArray<FString> Names;
	for (int32 I = 0; I < 2; ++I)
	{
		if (!Last[I].IsEmpty())
		{
			Names.Add(FString::Printf(TEXT("P%d:%s"), I + 1, *Last[I]));
		}
	}
	return Names;
}
FString Join(const TArray<FString>& Items)
{
	return Items.Num() ? FString::Join(Items, TEXT(", ")) : FString(TEXT("(none)"));
}
// Compares the state each fighter last entered with the expected list (at most one entry per fighter); a fighter
// absent from Expected must have entered nothing. How many transitions led there is not constrained.
void CheckEntries(FAutomationTestBase& T, const FString& Label, const FNSE006Battle& B,
				  const TArray<FNSE006StateEntry>& Expected)
{
	const TArray<FString> Actual = LastEntryNames(B.StateEntries), Want = EntryNames(Expected);
	T.TestEqual(*FString::Printf(TEXT("%s: last state entries expected [%s] got [%s]"), *Label, *Join(Want),
								 *Join(Actual)),
				Join(Actual), Join(Want));
}
int32 Input(int32 Player, bool bGuard, bool bCrouch)
{
	if (!bGuard)
	{
		return INP_Neutral;
	}
	if (Player == 0)
	{
		return bCrouch ? INP_DownLeft : INP_Left;
	}
	return bCrouch ? INP_DownRight : INP_Right;
}
} // namespace

// Independent model of one collision phase. Geometry is the published corner rule; everything else follows the
// instruction and the "what the engine does today" paragraph of SIMULTANEOUS_CONTACTS.md.
namespace Oracle
{
struct FBoxSpec
{
	int32 W = 4, H = 4, X = 0, Y = 0;
};
struct FArmorSpec
{
	bool bStrikes = false, bProjectiles = false;
	int32 Budget = 0;
	bool bChip = false;
	int32 DamagePercent = 0;
	bool Any() const
	{
		return Budget > 0 && (bStrikes || bProjectiles);
	}
};
struct FParticipant
{
	int32 Key = -1;
	bool bFighter = false;
	int32 Side = 0;
	int32 X = 0;
	bool bFacingLeft = false;
	TArray<FBoxSpec> Hit, Hurt;
	bool bAttacking = false;
	int32 Priority = 0, Damage = 100, CounterDamage = 100, Hitstop = 3, CounterHitstop = 3, Modifier = 0,
		  CounterModifier = 0, Hitstun = 10, CounterHitstun = 10, Level = 0;
	EBlockType BlockType = BLK_Mid;
	FArmorSpec Armor;
	bool bGuarding = false, bCrouchGuard = false;
	// Multi-frame fields: countdown already on the object and whether the hit is still active.
	int32 HitstopLeft = 0;
	bool bHitActive = true;
	FString Describe() const
	{
		FString S = FString::Printf(TEXT("%s%d side=%d x=%d left=%d"), bFighter ? TEXT("P") : TEXT("K"),
									bFighter ? Key + 1 : Key, Side, X, bFacingLeft);
		for (const auto& B : Hit)
		{
			S += FString::Printf(TEXT(" hit(%d,%d,%d,%d)"), B.W, B.H, B.X, B.Y);
		}
		for (const auto& B : Hurt)
		{
			S += FString::Printf(TEXT(" hurt(%d,%d,%d,%d)"), B.W, B.H, B.X, B.Y);
		}
		if (bAttacking)
		{
			S += FString::Printf(TEXT(" atk prio=%d dmg=%d/%d hs=%d/%d mod=%d/%d stun=%d/%d lvl=%d blk=%d act=%d"),
								 Priority, Damage, CounterDamage, Hitstop, CounterHitstop, Modifier,
								 CounterModifier, Hitstun, CounterHitstun, Level, (int32)BlockType, bHitActive);
		}
		if (Armor.Any())
		{
			S += FString::Printf(TEXT(" armor(s=%d p=%d n=%d chip=%d pct=%d)"), Armor.bStrikes, Armor.bProjectiles,
								 Armor.Budget, Armor.bChip, Armor.DamagePercent);
		}
		if (bGuarding)
		{
			S += bCrouchGuard ? TEXT(" crouchguard") : TEXT(" standguard");
		}
		if (HitstopLeft)
		{
			S += FString::Printf(TEXT(" hsleft=%d"), HitstopLeft);
		}
		return S;
	}
};
struct FPair
{
	int32 A, B;
};
struct FEncounter
{
	TArray<FParticipant> P;
	TArray<FPair> History;
	int32 Combo[2] = {0, 0};
	int32 Health[2] = {10000, 10000};
	bool Remembered(int32 A, int32 B) const
	{
		for (const auto& H : History)
		{
			if (H.A == A && H.B == B)
			{
				return true;
			}
		}
		return false;
	}
	const FParticipant* Find(int32 Key) const
	{
		for (const auto& X : P)
		{
			if (X.Key == Key)
			{
				return &X;
			}
		}
		return nullptr;
	}
	FString Describe() const
	{
		TArray<FString> Lines;
		for (const auto& X : P)
		{
			Lines.Add(X.Describe());
		}
		for (const auto& H : History)
		{
			Lines.Add(FString::Printf(TEXT("history(%d->%d)"), H.A, H.B));
		}
		return FString::Join(Lines, TEXT(" | "));
	}
};
// Receiving fighter's health at callback time: before and after the contact's own damage for the oracle, the
// observed value twice for an observation.
struct FHealthPair
{
	int32 Before = -1;
	int32 After = -1;
};
struct FOutcome
{
	int32 Health[2] = {10000, 10000};
	int32 Combo[2] = {0, 0};
	int32 FighterHitstop[2] = {0, 0};
	int32 Stun[2] = {0, 0};
	FString Entry[2];
	bool bClashBits[2] = {false, false};
	bool bAssertBits[2] = {true, true};
	TMap<int32, int32> Hitstop;
	TMap<int32, bool> Active;
	TMap<int32, int32> Budget;
	// Every event but HitOrBlock as "Kind:Actor:Other", sorted, with its callback-time health.
	TArray<FString> Events;
	TMap<FString, FHealthPair> EventHealth;
	TMap<int32, int32> HitOrBlock;
	// A strike in n >= 2 clashes may fire its clash callback once or once per clash: HitOrBlock minus this slack.
	TMap<int32, int32> HitOrBlockSlack;
	TMap<int64, int32> HitOrBlockPair;
	TArray<FPair> Contacts;
	int32 ClashPairs = 0, FighterClashes = 0, ObjectClashes = 0, Blocks = 0, Absorbs = 0, ArmorDamageAbsorbs = 0,
		  Lands = 0, ObjectVictimContacts = 0, FollowUpLands = 0, MultiContactTargets = 0, MultiClashStrikes = 0;
	bool Trade = false;
	FString Describe() const
	{
		FString S = FString::Printf(TEXT("hp=%d/%d combo=%d/%d fhs=%d/%d stun=%d/%d entry=%s/%s bits=%d/%d"),
									Health[0], Health[1], Combo[0], Combo[1], FighterHitstop[0],
									FighterHitstop[1], Stun[0], Stun[1], *Entry[0], *Entry[1], bClashBits[0],
									bClashBits[1]);
		TArray<int32> Keys;
		Hitstop.GetKeys(Keys);
		Keys.Sort();
		for (int32 K : Keys)
		{
			S += FString::Printf(TEXT(" K%d(hs=%d act=%d bud=%d)"), K, Hitstop[K], Active[K],
								 Budget.Contains(K) ? Budget[K] : -1);
		}
		S += TEXT(" events[");
		TArray<FString> Items;
		for (const FString& E : Events)
		{
			const FHealthPair H = EventHealth.FindRef(E);
			Items.Add(FString::Printf(TEXT("%s@%d/%d"), *E, H.Before, H.After));
		}
		S += FString::Join(Items, TEXT(","));
		S += TEXT("] hob[");
		TArray<int32> Actors;
		HitOrBlock.GetKeys(Actors);
		Actors.Sort();
		for (int32 A : Actors)
		{
			S += FString::Printf(TEXT("%d:%d "), A, HitOrBlock[A]);
		}
		S += TEXT("]");
		return S;
	}
};
FString EventKey(EEvent Kind, int32 Actor, int32 Other)
{
	const TCHAR* Names[] = {TEXT("Hit"), TEXT("CounterHit"), TEXT("Block"), TEXT("HitOrBlock"), TEXT("Receive")};
	return FString::Printf(TEXT("%s:%d:%d"), Names[(int32)Kind], Actor, Other);
}
// Key plus the recorded callback-time health, for same-implementation replay comparisons.
FString EventText(EEvent Kind, int32 Actor, int32 Other, int32 TargetHealth)
{
	return FString::Printf(TEXT("%s:%d"), *EventKey(Kind, Actor, Other), TargetHealth);
}
// B is -1 for a callback without a target (a clash), so it is stored offset by one to keep the key decodable.
int64 PairKey(int32 A, int32 B)
{
	return (int64)A * 1000 + B + 1;
}
int32 PairA(int64 Key)
{
	return (int32)(Key / 1000);
}
int32 PairB(int64 Key)
{
	return (int32)(Key % 1000) - 1;
}
int32 Trunc999(int32 V)
{
	return (int32)(((int64)V * 999) / 1000);
}
// Closed x and y extents of an authored box on an object: Pos + trunc((s * Offset +- Size / 2) * 999 / 1000).
void Extent(int32 X, bool bLeft, const FBoxSpec& B, int32& X0, int32& X1, int32& Y0, int32& Y1)
{
	const int32 O = bLeft ? -B.X : B.X;
	const int32 A = Trunc999(O - B.W / 2), C = Trunc999(O + B.W / 2);
	X0 = X + FMath::Min(A, C);
	X1 = X + FMath::Max(A, C);
	const int32 D = Trunc999(B.Y - B.H / 2), E = Trunc999(B.Y + B.H / 2);
	Y0 = FMath::Min(D, E);
	Y1 = FMath::Max(D, E);
}
bool BoxesTouch(const FParticipant& A, const TArray<FBoxSpec>& ABoxes, const FParticipant& B,
				const TArray<FBoxSpec>& BBoxes)
{
	for (const auto& BoxA : ABoxes)
	{
		int32 AX0, AX1, AY0, AY1;
		Extent(A.X, A.bFacingLeft, BoxA, AX0, AX1, AY0, AY1);
		for (const auto& BoxB : BBoxes)
		{
			int32 BX0, BX1, BY0, BY1;
			Extent(B.X, B.bFacingLeft, BoxB, BX0, BX1, BY0, BY1);
			if (!(AX1 < BX0 || BX1 < AX0) && !(AY1 < BY0 || BY1 < AY0))
			{
				return true;
			}
		}
	}
	return false;
}
int32 Blockstun(int32 Level)
{
	return Level == 0 ? 9 : Level == 1 ? 11 : 13;
}
FString HitstunName(int32 Level)
{
	return FString::Printf(TEXT("State.Universal.Hitstun.%d"), Level);
}
struct FContact
{
	int32 A, V;
};
// Returns false when two contacts on one target are tied under the published ladder.
bool Resolve(const FEncounter& E, FOutcome& Out)
{
	Out = FOutcome();
	for (int32 I = 0; I < 2; ++I)
	{
		Out.Health[I] = E.Health[I];
		Out.Combo[I] = E.Combo[I];
	}
	TMap<int32, int32> Candidate;
	for (const auto& P : E.P)
	{
		if (!P.bFighter)
		{
			Out.Hitstop.Add(P.Key, P.HitstopLeft);
			Out.Active.Add(P.Key, P.bAttacking && P.bHitActive);
		}
		else
		{
			Out.FighterHitstop[P.Key] = P.HitstopLeft;
		}
		Out.Budget.Add(P.Key, P.Armor.Budget);
	}
	auto Attacking = [](const FParticipant& P) { return P.bAttacking && P.bHitActive && P.Hit.Num() > 0; };
	auto Assign = [&](int32 Key, int32 Value)
	{
		int32& Slot = Candidate.FindOrAdd(Key);
		Slot = FMath::Max(Slot, Value);
	};
	// Clashes: same kind, different sides, hit boxes touching.
	TSet<int32> Clashing;
	TMap<int32, int32> ClashCount;
	for (int32 I = 0; I < E.P.Num(); ++I)
	{
		for (int32 J = I + 1; J < E.P.Num(); ++J)
		{
			const auto& A = E.P[I];
			const auto& B = E.P[J];
			if (A.Side == B.Side || A.bFighter != B.bFighter || !Attacking(A) || !Attacking(B))
			{
				continue;
			}
			if (!BoxesTouch(A, A.Hit, B, B.Hit))
			{
				continue;
			}
			Out.ClashPairs++;
			if (A.bFighter)
			{
				Out.FighterClashes++;
				Out.bClashBits[A.Key] = Out.bClashBits[B.Key] = true;
			}
			else
			{
				Out.ObjectClashes++;
			}
			Clashing.Add(A.Key);
			Clashing.Add(B.Key);
			ClashCount.FindOrAdd(A.Key)++;
			ClashCount.FindOrAdd(B.Key)++;
			Out.HitOrBlock.FindOrAdd(A.Key)++;
			Out.HitOrBlock.FindOrAdd(B.Key)++;
			Assign(A.Key, 16);
			Assign(B.Key, 16);
			if (!A.bFighter)
			{
				Out.Active[A.Key] = false;
				Out.Active[B.Key] = false;
			}
		}
	}
	for (const auto& C : ClashCount)
	{
		if (C.Value >= 2)
		{
			Out.MultiClashStrikes++;
			Out.HitOrBlockSlack.Add(C.Key, C.Value - 1);
		}
	}
	auto Emit = [&](EEvent Kind, int32 A, int32 V, int32 Before, int32 After)
	{
		const FString Key = EventKey(Kind, A, V);
		Out.Events.Add(Key);
		FHealthPair H;
		H.Before = Before;
		H.After = After;
		Out.EventHealth.Add(Key, H);
	};
	// Contacts: attacker not clashing, target not already contacted by it, hit box on hurt box.
	TMap<int32, TArray<FContact>> PerTarget;
	for (const auto& A : E.P)
	{
		if (!Attacking(A) || Clashing.Contains(A.Key))
		{
			continue;
		}
		for (const auto& V : E.P)
		{
			if (V.Side == A.Side || E.Remembered(A.Key, V.Key) || !BoxesTouch(A, A.Hit, V, V.Hurt))
			{
				continue;
			}
			PerTarget.FindOrAdd(V.Key).Add({A.Key, V.Key});
		}
	}
	bool Landed[2] = {false, false};
	TArray<int32> Targets;
	PerTarget.GetKeys(Targets);
	Targets.Sort();
	for (int32 TargetKey : Targets)
	{
		auto& List = PerTarget[TargetKey];
		const FParticipant& V = *E.Find(TargetKey);
		auto Rank = [&](const FContact& C, int32& Prio, int32& Kind, int32& Dmg, int32& Dist)
		{
			const FParticipant& A = *E.Find(C.A);
			Prio = A.Priority;
			Kind = A.bFighter ? 1 : 0;
			Dmg = A.Damage;
			Dist = FMath::Abs(A.X - V.X);
		};
		List.Sort(
			[&](const FContact& L, const FContact& R)
			{
				int32 LP, LK, LD, LX, RP, RK, RD, RX;
				Rank(L, LP, LK, LD, LX);
				Rank(R, RP, RK, RD, RX);
				if (LP != RP)
				{
					return LP > RP;
				}
				if (LK != RK)
				{
					return LK > RK;
				}
				if (LD != RD)
				{
					return LD > RD;
				}
				return LX < RX;
			});
		for (int32 I = 1; I < List.Num(); ++I)
		{
			int32 LP, LK, LD, LX, RP, RK, RD, RX;
			Rank(List[I - 1], LP, LK, LD, LX);
			Rank(List[I], RP, RK, RD, RX);
			if (LP == RP && LK == RK && LD == RD && LX == RX)
			{
				return false;
			}
		}
		if (List.Num() >= 2)
		{
			Out.MultiContactTargets++;
		}
		int32 Budget = V.Armor.Budget;
		int32 LandsHere = 0;
		for (const auto& C : List)
		{
			const FParticipant& A = *E.Find(C.A);
			Out.Contacts.Add({A.Key, V.Key});
			const bool bCovered = A.bFighter ? V.Armor.bStrikes : V.Armor.bProjectiles;
			if (!V.bFighter)
			{
				Out.ObjectVictimContacts++;
				if (bCovered && Budget > 0)
				{
					Budget--;
					Out.Absorbs++;
					Emit(EEvent::Hit, A.Key, V.Key, -1, -1);
				}
				else
				{
					Out.Lands++;
					Emit(EEvent::Hit, A.Key, V.Key, -1, -1);
					Emit(EEvent::Receive, V.Key, A.Key, -1, -1);
				}
				Assign(A.Key, A.Hitstop);
				Assign(V.Key, A.Hitstop);
				continue;
			}
			const int32 Vi = V.Key;
			const int32 TH = Out.Health[Vi];
			Out.HitOrBlock.FindOrAdd(A.Key)++;
			Out.HitOrBlockPair.FindOrAdd(PairKey(A.Key, V.Key))++;
			const bool bGuarded = V.bGuarding && (V.bCrouchGuard ? A.BlockType != BLK_High : A.BlockType != BLK_Low);
			if (bGuarded)
			{
				Out.Blocks++;
				Out.Health[Vi] -= A.Damage * 10 / 100;
				Assign(A.Key, A.Hitstop);
				Assign(V.Key, A.Hitstop);
				Emit(EEvent::Block, A.Key, V.Key, TH, Out.Health[Vi]);
				if (!Landed[Vi])
				{
					Out.Entry[Vi] = V.bCrouchGuard ? TEXT("State.Universal.Block.Crouch") : TEXT("State.Universal.Block.Stand");
					Out.Stun[Vi] = Blockstun(A.Level);
				}
			}
			else if (bCovered && Budget > 0)
			{
				Budget--;
				Out.Absorbs++;
				if (V.Armor.bChip)
				{
					Out.Health[Vi] -= A.Damage * 10 / 100;
				}
				if (V.Armor.DamagePercent)
				{
					Out.ArmorDamageAbsorbs++;
					Out.Health[Vi] -= A.Damage * V.Armor.DamagePercent / 100;
				}
				Assign(A.Key, A.Hitstop);
				Assign(V.Key, A.Hitstop);
				Emit(EEvent::Hit, A.Key, V.Key, TH, Out.Health[Vi]);
			}
			else
			{
				Landed[Vi] = true;
				Out.Lands++;
				LandsHere++;
				const int32 Side = A.Side;
				const int32 N = ++Out.Combo[Side];
				const bool bCounter = V.bAttacking;
				const int32 D = bCounter ? A.CounterDamage : A.Damage;
				Out.Health[Vi] -= N == 1 ? D : D * 50 / 100;
				Assign(A.Key, A.Hitstop);
				Assign(V.Key, bCounter ? A.CounterHitstop + A.CounterModifier : A.Hitstop + A.Modifier);
				Out.Entry[Vi] = HitstunName(A.Level);
				Out.Stun[Vi] = bCounter ? A.CounterHitstun : A.Hitstun;
				Emit(EEvent::Hit, A.Key, V.Key, TH, Out.Health[Vi]);
				Emit(EEvent::Receive, V.Key, A.Key, TH, Out.Health[Vi]);
				if (bCounter)
				{
					Emit(EEvent::CounterHit, A.Key, V.Key, TH, Out.Health[Vi]);
				}
			}
		}
		if (LandsHere >= 2)
		{
			Out.FollowUpLands++;
		}
		Out.Budget[V.Key] = Budget;
	}
	Out.Trade = Landed[0] && Landed[1];
	for (const auto& C : Candidate)
	{
		const FParticipant* P = E.Find(C.Key);
		if (P->bFighter)
		{
			Out.FighterHitstop[P->Key] = C.Value;
		}
		else
		{
			Out.Hitstop[P->Key] = C.Value;
		}
	}
	for (int32 I = 0; I < 2; ++I)
	{
		Out.bAssertBits[I] = !(Out.bClashBits[I] && Landed[I]);
	}
	Out.Events.Sort();
	return true;
}
} // namespace Oracle

namespace
{
// What the tests read back from the engine after a frame, in the oracle's shape.
struct FObserved
{
	Oracle::FOutcome O;
};
FObserved Observe(const FNSE006Battle& B, const TArray<int32>& ObjectKeys)
{
	FObserved R;
	auto& O = R.O;
	for (int32 I = 0; I < 2; ++I)
	{
		const APlayerObject* P = B.Game->Players[I];
		O.Health[I] = P->CurrentHealth;
		O.Combo[I] = P->ComboCounter;
		O.FighterHitstop[I] = (int32)P->Hitstop;
		O.Stun[I] = (int32)P->StunTime;
		O.bClashBits[I] = (const_cast<APlayerObject*>(P)->GetEnableFlags(PrimaryMachine()) & ClashBits) ==
						  ClashBits;
		O.Budget.Add(I, P->SuperArmorData.ArmorHits);
	}
	for (int32 Key : ObjectKeys)
	{
		const ABattleObject* K = B.Find(Key);
		O.Hitstop.Add(Key, K ? (int32)K->Hitstop : -1);
		O.Active.Add(Key, K ? HitActive(K) : false);
		O.Budget.Add(Key, K ? K->SuperArmorData.ArmorHits : -1);
	}
	for (const auto& E : B.Trace)
	{
		if (E.Kind == EEvent::HitOrBlock)
		{
			O.HitOrBlock.FindOrAdd(E.Actor)++;
			O.HitOrBlockPair.FindOrAdd(Oracle::PairKey(E.Actor, E.Other))++;
		}
		else
		{
			const FString Key = Oracle::EventKey(E.Kind, E.Actor, E.Other);
			O.Events.Add(Key);
			Oracle::FHealthPair H;
			H.Before = H.After = E.TargetHealth;
			O.EventHealth.Add(Key, H);
		}
	}
	O.Events.Sort();
	return R;
}
// First difference between an oracle outcome and an observation after the contact frame; empty when equal.
FString DiffOutcome(const Oracle::FOutcome& Want, const Oracle::FOutcome& Got, bool bBits = true)
{
	for (int32 I = 0; I < 2; ++I)
	{
		if (Want.Health[I] != Got.Health[I])
		{
			return FString::Printf(TEXT("P%d health want %d got %d"), I + 1, Want.Health[I], Got.Health[I]);
		}
		if (Want.Combo[I] != Got.Combo[I])
		{
			return FString::Printf(TEXT("P%d combo want %d got %d"), I + 1, Want.Combo[I], Got.Combo[I]);
		}
		if (Want.FighterHitstop[I] != Got.FighterHitstop[I])
		{
			return FString::Printf(TEXT("P%d hitstop want %d got %d"), I + 1, Want.FighterHitstop[I],
								   Got.FighterHitstop[I]);
		}
		if (Want.Budget.FindRef(I) != Got.Budget.FindRef(I))
		{
			return FString::Printf(TEXT("P%d armor budget want %d got %d"), I + 1, Want.Budget.FindRef(I),
								   Got.Budget.FindRef(I));
		}
		if (bBits && Want.bAssertBits[I] && Want.bClashBits[I] != Got.bClashBits[I])
		{
			return FString::Printf(TEXT("P%d clash cancel windows want %d got %d"), I + 1, Want.bClashBits[I],
								   Got.bClashBits[I]);
		}
	}
	for (const auto& H : Want.Hitstop)
	{
		if (!Got.Hitstop.Contains(H.Key) || Got.Hitstop[H.Key] != H.Value)
		{
			return FString::Printf(TEXT("K%d hitstop want %d got %d"), H.Key, H.Value,
								   Got.Hitstop.Contains(H.Key) ? Got.Hitstop[H.Key] : -1);
		}
		if (Got.Active.FindRef(H.Key) != Want.Active.FindRef(H.Key))
		{
			return FString::Printf(TEXT("K%d hit active want %d got %d"), H.Key, Want.Active.FindRef(H.Key),
								   Got.Active.FindRef(H.Key));
		}
		if (Got.Budget.FindRef(H.Key) != Want.Budget.FindRef(H.Key))
		{
			return FString::Printf(TEXT("K%d armor budget want %d got %d"), H.Key, Want.Budget.FindRef(H.Key),
								   Got.Budget.FindRef(H.Key));
		}
	}
	if (Want.Events != Got.Events)
	{
		return FString::Printf(TEXT("events want [%s] got [%s]"), *FString::Join(Want.Events, TEXT(",")),
							   *FString::Join(Got.Events, TEXT(",")));
	}
	for (const auto& H : Got.EventHealth)
	{
		const Oracle::FHealthPair* W = Want.EventHealth.Find(H.Key);
		if (W && H.Value.Before != W->Before && H.Value.Before != W->After)
		{
			return FString::Printf(TEXT("%s callback-time health want %d or %d got %d"), *H.Key, W->Before, W->After,
								   H.Value.Before);
		}
	}
	TSet<int32> Actors;
	for (const auto& H : Want.HitOrBlock)
	{
		Actors.Add(H.Key);
	}
	for (const auto& H : Got.HitOrBlock)
	{
		Actors.Add(H.Key);
	}
	for (int32 A : Actors)
	{
		const int32 W = Want.HitOrBlock.Contains(A) ? Want.HitOrBlock[A] : 0;
		const int32 G = Got.HitOrBlock.Contains(A) ? Got.HitOrBlock[A] : 0;
		const int32 Slack = Want.HitOrBlockSlack.Contains(A) ? Want.HitOrBlockSlack[A] : 0;
		if (G > W || G < W - Slack)
		{
			return FString::Printf(TEXT("HitOrBlock count on actor %d want %d..%d got %d"), A, W - Slack, W, G);
		}
	}
	for (const auto& H : Want.HitOrBlockPair)
	{
		const int32 G = Got.HitOrBlockPair.Contains(H.Key) ? Got.HitOrBlockPair[H.Key] : 0;
		if (G < H.Value)
		{
			return FString::Printf(TEXT("HitOrBlock(%d->%d) want at least %d got %d"), Oracle::PairA(H.Key),
								   Oracle::PairB(H.Key), H.Value, G);
		}
	}
	return FString();
}
// Difference in the frame-after observations: stun and one state entry per reacting fighter.
FString DiffReaction(const Oracle::FOutcome& Want, const FNSE006Battle& B)
{
	for (int32 I = 0; I < 2; ++I)
	{
		if ((int32)B.Game->Players[I]->StunTime != Want.Stun[I])
		{
			return FString::Printf(TEXT("P%d stun after entry want %d got %d"), I + 1, Want.Stun[I],
								   (int32)B.Game->Players[I]->StunTime);
		}
	}
	TArray<FString> Actual = LastEntryNames(B.StateEntries), Wanted;
	for (int32 I = 0; I < 2; ++I)
	{
		if (!Want.Entry[I].IsEmpty())
		{
			Wanted.Add(FString::Printf(TEXT("P%d:%s"), I + 1, *Want.Entry[I]));
		}
	}
	Wanted.Sort();
	if (Actual != Wanted)
	{
		return FString::Printf(TEXT("last state entries want [%s] got [%s]"), *Join(Wanted), *Join(Actual));
	}
	return FString();
}
// Builds an encounter in the engine. Objects are spawned in SpawnOrder (indices into E.P restricted to objects)
// with the matching forced slots.
void Build(FNSE006Battle& B, const Oracle::FEncounter& E, const TArray<int32>& SpawnOrder,
		   const TArray<int32>& Slots)
{
	B.Reset();
	auto Author = [&](ABattleObject* O, const Oracle::FParticipant& P)
	{
		TArray<FCollisionBox> Boxes;
		for (const auto& X : P.Hurt)
		{
			Boxes.Add(FNSE006Battle::Box(X.W, X.H, BOX_Hurt, X.X, X.Y));
		}
		if (P.bAttacking)
		{
			B.Strike(O, P.Damage, P.Priority, P.Level, P.Hitstop);
			O->CounterHit.Damage = P.CounterDamage;
			O->CounterHit.Hitstop = P.CounterHitstop;
			O->NormalHit.EnemyHitstopModifier = P.Modifier;
			O->CounterHit.EnemyHitstopModifier = P.CounterModifier;
			O->NormalHit.Hitstun = P.Hitstun;
			O->CounterHit.Hitstun = P.CounterHitstun;
			O->HitCommon.BlockType = P.BlockType;
			for (const auto& X : P.Hit)
			{
				Boxes.Add(FNSE006Battle::Box(X.W, X.H, BOX_Hit, X.X, X.Y));
			}
			if (!P.bHitActive)
			{
				O->EnableHit(false);
			}
		}
		B.Boxes(O, Boxes);
		if (P.Armor.Any())
		{
			B.Armor(O, P.Armor.Budget, P.Armor.bStrikes, P.Armor.bProjectiles);
			O->SuperArmorData.bArmorTakeChipDamage = P.Armor.bChip;
			O->SuperArmorData.ArmorDamagePercent = P.Armor.DamagePercent;
		}
		O->Hitstop = P.HitstopLeft;
	};
	for (const auto& P : E.P)
	{
		if (P.bFighter)
		{
			Author(B.Game->Players[P.Key], P);
			B.Guard(B.Game->Players[P.Key], P.bGuarding);
		}
	}
	for (int32 I = 0; I < SpawnOrder.Num(); ++I)
	{
		const auto& P = E.P[SpawnOrder[I]];
		auto* O = B.Spawn(B.Game->Players[P.Side], P.Key, P.X, 0, Slots.IsValidIndex(I) ? Slots[I] : -1);
		check(O);
		Author(O, P);
	}
}
TArray<int32> ObjectKeys(const Oracle::FEncounter& E)
{
	TArray<int32> Keys;
	for (const auto& P : E.P)
	{
		if (!P.bFighter)
		{
			Keys.Add(P.Key);
		}
	}
	return Keys;
}
TArray<int32> ObjectIndices(const Oracle::FEncounter& E)
{
	TArray<int32> Idx;
	for (int32 I = 0; I < E.P.Num(); ++I)
	{
		if (!E.P[I].bFighter)
		{
			Idx.Add(I);
		}
	}
	return Idx;
}
void Inputs(const Oracle::FEncounter& E, int32& In1, int32& In2)
{
	In1 = INP_Neutral;
	In2 = INP_Neutral;
	for (const auto& P : E.P)
	{
		if (P.bFighter)
		{
			(P.Key == 0 ? In1 : In2) = Input(P.Key, P.bGuarding, P.bCrouchGuard);
		}
	}
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
} // namespace

#define NSE006_TEST(Class, Name)                                                                                \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, "UnrealBench.NSE006." Name,                                         \
									 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// R2, R7, R1: both directions of a mutual strike apply as counter hits, and detection ignores the facing change a
// contact applies (S2.1, S2.2, S1.1).
NSE006_TEST(FNSE006Trades, "Trades")
bool FNSE006Trades::RunTest(const FString&)
{
	FNSE006Battle B;
	APlayerObject* P1 = B.P1;
	APlayerObject* P2 = B.P2;
	{
		B.Reset();
		Arm(B, P1, {Reach()});
		P1->CounterHit.Damage = 150;
		Arm(B, P2, {Reach()});
		P2->CounterHit.Damage = 150;
		B.Step();
		TestEqual(TEXT("S2.1 trade: P1 health after P2's strike as a counter hit (counter damage 150)"),
				  P1->CurrentHealth, 9850);
		TestEqual(TEXT("S2.1 trade: P2 health after P1's strike as a counter hit (counter damage 150)"),
				  P2->CurrentHealth, 9850);
		TestEqual(TEXT("S2.1 trade: P1 combo counter"), P1->ComboCounter, 1);
		TestEqual(TEXT("S2.1 trade: P2 combo counter"), P2->ComboCounter, 1);
		TestEqual(TEXT("S2.1 trade: CounterHit callbacks on P1 with target P2"), B.Count(EEvent::CounterHit, 0, 1),
				  1);
		TestEqual(TEXT("S2.1 trade: CounterHit callbacks on P2 with target P1"), B.Count(EEvent::CounterHit, 1, 0),
				  1);
		B.Step();
		CheckEntries(*this, TEXT("S2.1 trade: both fighters enter Hitstun_0 on the next frame"), B,
					 {{0, HitstunTag(0)}, {1, HitstunTag(0)}});
	}
	{
		B.Reset();
		Arm(B, P1, {Reach()});
		B.Step();
		TestEqual(TEXT("S2.2 one-sided: no CounterHit callback when only P1 attacks"), B.Count(EEvent::CounterHit),
				  0);
		TestEqual(TEXT("S2.2 one-sided: P2 loses normal damage 100"), P2->CurrentHealth, 9900);
		TestEqual(TEXT("S2.2 one-sided: P1 untouched"), P1->CurrentHealth, 10000);
		TestEqual(TEXT("S2.2 one-sided: P1 combo counter"), P1->ComboCounter, 1);
		TestEqual(TEXT("S2.2 one-sided: P2 combo counter"), P2->ComboCounter, 0);
	}
	{
		// S1.1: P2 faces right with a box at offset -100100, which covers [-50001, -49997] and touches P1; the
		// facing flip P1's contact applies moves that box to +150000, so a per-pair walk never lets P2 hit back.
		B.Reset();
		Arm(B, P1, {Reach()});
		P2->Direction = DIR_Right;
		Arm(B, P2, {FNSE006Battle::Box(4, 4, BOX_Hit, -100100, 0)});
		B.Step();
		TestEqual(TEXT("S1.1 frozen facing: P1 health after P2's frozen-facing strike lands as a counter hit"),
				  P1->CurrentHealth, 9900);
		TestEqual(TEXT("S1.1 frozen facing: P2 health after P1's strike"), P2->CurrentHealth, 9900);
		TestEqual(TEXT("S1.1 frozen facing: P1 combo counter"), P1->ComboCounter, 1);
		TestEqual(TEXT("S1.1 frozen facing: P2 combo counter"), P2->ComboCounter, 1);
		TestEqual(TEXT("S1.1 frozen facing: Receive callbacks on P1 from P2"), B.Count(EEvent::Receive, 0, 1), 1);
		TestEqual(TEXT("S1.1 frozen facing: Receive callbacks on P2 from P1"), B.Count(EEvent::Receive, 1, 0), 1);
	}
	return !HasAnyErrors();
}

// R1, R8: hit disables, invulnerability and deactivation applied during the phase change nothing detected in the
// frame (S1.2, S1.3, S1.4).
NSE006_TEST(FNSE006FrozenDetection, "FrozenDetection")
bool FNSE006FrozenDetection::RunTest(const FString&)
{
	FNSE006Battle B;
	APlayerObject* P1 = B.P1;
	APlayerObject* P2 = B.P2;
	{
		B.Reset();
		B.Armor(P2, 1, true, true, true);
		ABattleObject* K2 = Projectile(B, P1, 2, FX);
		Victim(B, P2, 3, FX);
		B.Step();
		TestEqual(TEXT("S1.2 frozen armor disable: K2 absorbed by P2 (Hit callbacks on K2 with target P2)"),
				  B.Count(EEvent::Hit, 2, 1), 1);
		TestEqual(TEXT("S1.2 frozen armor disable: no Receive on P2 from K2"), B.Count(EEvent::Receive, 1, 2), 0);
		TestEqual(TEXT("S1.2 frozen armor disable: P2 armor budget spent"), P2->SuperArmorData.ArmorHits, 0);
		TestEqual(TEXT("S1.2 frozen armor disable: K2 still lands on K3 in the same frame (Receive on K3 from K2)"),
				  B.Count(EEvent::Receive, 3, 2), 1);
		Victim(B, P2, 4, FX);
		B.Step();
		TestEqual(TEXT("S1.2 frozen armor disable: the disable still takes effect, so a fresh K4 is not hit next frame"),
				  B.Count(EEvent::Receive, 4, 2), 0);
		(void)K2;
	}
	{
		B.Reset();
		B.InvulnerableOnReceive(P2, 60);
		Arm(B, P1, {Reach()});
		Projectile(B, P1, 2, FX);
		B.Step();
		TestEqual(TEXT("S1.3 frozen invulnerability: P2 health after strike 100 and K2 100 prorated"),
				  P2->CurrentHealth, 9850);
		TestEqual(TEXT("S1.3 frozen invulnerability: P1 combo counter"), P1->ComboCounter, 2);
		TestEqual(TEXT("S1.3 frozen invulnerability: Receive callbacks on P2 in the frame"),
				  B.Count(EEvent::Receive, 1), 2);
		Projectile(B, P1, 3, FX);
		B.Step();
		TestEqual(TEXT("S1.3 frozen invulnerability: fresh K3 lands nothing on the next frame (Receive count)"),
				  B.Count(EEvent::Receive, 1), 2);
		TestEqual(TEXT("S1.3 frozen invulnerability: P2 health unchanged on the next frame"), P2->CurrentHealth,
				  9850);
	}
	{
		B.Reset();
		ABattleObject* K2 = Projectile(B, P1, 2, FX);
		B.OnHit(K2, ENSE006OnHit::Deactivate);
		Victim(B, P2, 3, FX);
		B.Step();
		TestEqual(TEXT("S1.4 deactivation during phase: P2 health after K2"), P2->CurrentHealth, 9900);
		TestEqual(TEXT("S1.4 deactivation during phase: K2 also lands on K3 (Receive on K3 from K2)"),
				  B.Count(EEvent::Receive, 3, 2), 1);
		for (int32 I = 0; I < 8 && K2->IsActive; ++I)
		{
			B.Step();
		}
		TestFalse(TEXT("S1.4 deactivation during phase: K2 deactivates within eight frames"), K2->IsActive);
	}
	return !HasAnyErrors();
}

// R3, R6, R7: clash precedence over every contact of the clashing strike, multi-way clashes, mixed kinds, cancel
// windows (S3.1 to S3.7).
NSE006_TEST(FNSE006Clashes, "Clashes")
bool FNSE006Clashes::RunTest(const FString&)
{
	FNSE006Battle B;
	APlayerObject* P1 = B.P1;
	APlayerObject* P2 = B.P2;
	{
		B.Reset();
		Arm(B, P1, {Centre()});
		Arm(B, P2, {Centre()});
		TestEqual(TEXT("S3.7 idle enables nothing before the clash (P1 enable flags)"),
				  P1->GetEnableFlags(PrimaryMachine()) & ClashBits, 0);
		B.Step();
		TestEqual(TEXT("S3.1 clash only: P1 hitstop"), (int32)P1->Hitstop, 16);
		TestEqual(TEXT("S3.1 clash only: P2 hitstop"), (int32)P2->Hitstop, 16);
		TestFalse(TEXT("S3.1 clash only: P1 loses its active hit"), HitActive(P1));
		TestFalse(TEXT("S3.1 clash only: P2 loses its active hit"), HitActive(P2));
		TestEqual(TEXT("S3.1 clash only: P1 health"), P1->CurrentHealth, 10000);
		TestEqual(TEXT("S3.1 clash only: P2 health"), P2->CurrentHealth, 10000);
		TestEqual(TEXT("S3.1 clash only: HitOrBlock callbacks"), B.Count(EEvent::HitOrBlock), 2);
		TestEqual(TEXT("S3.1 clash only: Hit callbacks"), B.Count(EEvent::Hit), 0);
		TestEqual(TEXT("S3.7 fighter clash enables forward dash and normal attack on P1"),
				  P1->GetEnableFlags(PrimaryMachine()) & ClashBits, ClashBits);
		TestEqual(TEXT("S3.7 fighter clash enables forward dash and normal attack on P2"),
				  P2->GetEnableFlags(PrimaryMachine()) & ClashBits, ClashBits);
	}
	{
		B.Reset();
		Arm(B, P1, {ClashPlusReach()});
		Arm(B, P2, {Centre()});
		B.Step();
		TestEqual(TEXT("S3.2 clash plus reach: P2 health untouched by the clashing strike"), P2->CurrentHealth,
				  10000);
		TestEqual(TEXT("S3.2 clash plus reach: P1 hitstop"), (int32)P1->Hitstop, 16);
		TestEqual(TEXT("S3.2 clash plus reach: P2 hitstop"), (int32)P2->Hitstop, 16);
		TestEqual(TEXT("S3.2 clash plus reach: Hit callbacks"), B.Count(EEvent::Hit), 0);
	}
	{
		B.Reset();
		ABattleObject* K2 = Projectile(B, P1, 2, FX);
		ABattleObject* K3 = Projectile(B, P2, 3, FX);
		B.Step();
		TestEqual(TEXT("S3.3 clash suppresses other targets: P2 health untouched by clashing K2"),
				  P2->CurrentHealth, 10000);
		TestEqual(TEXT("S3.3 clash suppresses other targets: K2 hitstop"), (int32)K2->Hitstop, 16);
		TestEqual(TEXT("S3.3 clash suppresses other targets: K3 hitstop"), (int32)K3->Hitstop, 16);
		TestFalse(TEXT("S3.3 clash suppresses other targets: K2 loses its active hit"), HitActive(K2));
		TestFalse(TEXT("S3.3 clash suppresses other targets: K3 loses its active hit"), HitActive(K3));
		TestEqual(TEXT("S3.3 clash suppresses other targets: Receive callbacks"), B.Count(EEvent::Receive), 0);
		TestEqual(TEXT("S3.3 clash suppresses other targets: HitOrBlock on K2"), B.Count(EEvent::HitOrBlock, 2), 1);
		TestEqual(TEXT("S3.3 clash suppresses other targets: HitOrBlock on K3"), B.Count(EEvent::HitOrBlock, 3), 1);
	}
	{
		B.Reset();
		Arm(B, P1, {Reach()});
		ABattleObject* K3 = Projectile(B, P2, 3, FX);
		B.Step();
		TestEqual(TEXT("S3.4 mixed kinds never clash: P1's strike lands on K3 (Receive on K3 from P1)"),
				  B.Count(EEvent::Receive, 3, 0), 1);
		TestEqual(TEXT("S3.4 mixed kinds never clash: K3 hitstop is the strike's 3, not the clash 16"),
				  (int32)K3->Hitstop, 3);
		TestEqual(TEXT("S3.4 mixed kinds never clash: no HitOrBlock on K3"), B.Count(EEvent::HitOrBlock, 3), 0);
		TestEqual(TEXT("S3.4 mixed kinds never clash: P2 also hit by the reach box"), P2->CurrentHealth, 9900);
	}
	{
		B.Reset();
		ABattleObject* K2 = Projectile(B, P1, 2, 0);
		ABattleObject* K3 = Projectile(B, P2, 3, 0);
		B.Step();
		TestEqual(TEXT("S3.5 object clash on the middle grid: Receive callbacks"), B.Count(EEvent::Receive), 0);
		TestEqual(TEXT("S3.5 object clash on the middle grid: K2 hitstop"), (int32)K2->Hitstop, 16);
		TestEqual(TEXT("S3.5 object clash on the middle grid: K3 hitstop"), (int32)K3->Hitstop, 16);
		TestEqual(TEXT("S3.7 object clash enables nothing on P1"), P1->GetEnableFlags(PrimaryMachine()) & ClashBits,
				  0);
		TestEqual(TEXT("S3.7 object clash enables nothing on P2"), P2->GetEnableFlags(PrimaryMachine()) & ClashBits,
				  0);
	}
	{
		B.Reset();
		ABattleObject* K2 = Projectile(B, P1, 2, 0);
		ABattleObject* K3 = Projectile(B, P2, 3, 0);
		ABattleObject* K4 = Projectile(B, P2, 4, 0);
		B.Step();
		TestEqual(TEXT("S3.6 multi-way clash: K2 hitstop"), (int32)K2->Hitstop, 16);
		TestEqual(TEXT("S3.6 multi-way clash: K3 hitstop"), (int32)K3->Hitstop, 16);
		TestEqual(TEXT("S3.6 multi-way clash: K4 hitstop"), (int32)K4->Hitstop, 16);
		TestFalse(TEXT("S3.6 multi-way clash: K2 loses its active hit"), HitActive(K2));
		TestFalse(TEXT("S3.6 multi-way clash: K3 loses its active hit"), HitActive(K3));
		TestFalse(TEXT("S3.6 multi-way clash: K4 loses its active hit"), HitActive(K4));
		const int32 K2Clashes = B.Count(EEvent::HitOrBlock, 2);
		TestTrue(*FString::Printf(TEXT("S3.6 multi-way clash: K2 fires its clash callback once or once per clash, got %d"),
								  K2Clashes),
				 K2Clashes == 1 || K2Clashes == 2);
		TestEqual(TEXT("S3.6 multi-way clash: K3 HitOrBlock"), B.Count(EEvent::HitOrBlock, 3), 1);
		TestEqual(TEXT("S3.6 multi-way clash: K4 HitOrBlock"), B.Count(EEvent::HitOrBlock, 4), 1);
		TestEqual(TEXT("S3.6 multi-way clash: no Receive (K4 does not land on K2)"), B.Count(EEvent::Receive), 0);
	}
	return !HasAnyErrors();
}

// R4: the same-target ladder, combo continuation, reaction and stun of the last landing contact, callbacks during
// application (S4.1 to S4.7).
NSE006_TEST(FNSE006SameTargetOrder, "SameTargetOrder")
bool FNSE006SameTargetOrder::RunTest(const FString&)
{
	FNSE006Battle B;
	APlayerObject* P1 = B.P1;
	APlayerObject* P2 = B.P2;
	{
		B.Reset();
		B.Armor(P2, 1);
		Arm(B, P1, {Reach()}, 300, 0);
		Projectile(B, P1, 2, FX, 100, 5);
		B.Step();
		TestEqual(TEXT("S4.1 priority rung: K2 (100, priority 5) absorbed (Hit on K2 with target P2)"),
				  B.Count(EEvent::Hit, 2, 1), 1);
		TestEqual(TEXT("S4.1 priority rung: no Receive on P2 from K2"), B.Count(EEvent::Receive, 1, 2), 0);
		TestEqual(TEXT("S4.1 priority rung: strike (300, priority 0) lands (Receive on P2 from P1)"),
				  B.Count(EEvent::Receive, 1, 0), 1);
		TestEqual(TEXT("S4.1 priority rung: P2 health"), P2->CurrentHealth, 9700);
		TestEqual(TEXT("S4.1 priority rung: P1 combo counter"), P1->ComboCounter, 1);
		TestEqual(TEXT("S4.1 priority rung: P2 armor budget"), P2->SuperArmorData.ArmorHits, 0);
	}
	{
		B.Reset();
		B.Armor(P2, 1);
		Arm(B, P1, {Reach()}, 100, 0);
		Projectile(B, P1, 2, FX, 300, 0);
		B.Step();
		TestEqual(TEXT("S4.2 kind rung: strike (100) absorbed before K2 (300) (Hit on P1 with target P2)"),
				  B.Count(EEvent::Hit, 0, 1), 1);
		TestEqual(TEXT("S4.2 kind rung: no Receive on P2 from P1"), B.Count(EEvent::Receive, 1, 0), 0);
		TestEqual(TEXT("S4.2 kind rung: K2 lands 300"), P2->CurrentHealth, 9700);
		TestEqual(TEXT("S4.2 kind rung: Receive on P2 from K2"), B.Count(EEvent::Receive, 1, 2), 1);
	}
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		B.Reset();
		B.Armor(P2, 1);
		if (Variant == 0)
		{
			Projectile(B, P1, 2, FX, 300, 0, 0, 3, 0);
			Projectile(B, P1, 3, FX, 100, 0, 0, 3, 1);
		}
		else
		{
			Projectile(B, P1, 3, FX, 100, 0, 0, 3, 0);
			Projectile(B, P1, 2, FX, 300, 0, 0, 3, 1);
		}
		B.Step();
		const FString L = FString::Printf(TEXT("S4.3 damage rung (K2 300 in slot %d, K3 100 in slot %d)"),
										  Variant == 0 ? 0 : 1, Variant == 0 ? 1 : 0);
		TestEqual(*(L + TEXT(": K2 absorbed (Hit on K2 with target P2)")), B.Count(EEvent::Hit, 2, 1), 1);
		TestEqual(*(L + TEXT(": no Receive on P2 from K2")), B.Count(EEvent::Receive, 1, 2), 0);
		TestEqual(*(L + TEXT(": K3 lands (Receive on P2 from K3)")), B.Count(EEvent::Receive, 1, 3), 1);
		TestEqual(*(L + TEXT(": P2 health")), P2->CurrentHealth, 9900);
	}
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		B.Reset();
		B.Armor(P2, 1);
		const int32 X2 = Variant == 0 ? FX - 2 : FX + 2, X3 = Variant == 0 ? FX + 1 : FX - 1;
		Projectile(B, P1, 2, X2, 100, 0, 0, 3, 0);
		Projectile(B, P1, 3, X3, 100, 0, 0, 3, 1);
		B.Step();
		const FString L = FString::Printf(TEXT("S4.4 distance rung (K2 at %d in slot 0, K3 at %d in slot 1)"), X2,
										  X3);
		TestEqual(*(L + TEXT(": nearer K3 absorbed (Hit on K3 with target P2)")), B.Count(EEvent::Hit, 3, 1), 1);
		TestEqual(*(L + TEXT(": no Receive on P2 from K3")), B.Count(EEvent::Receive, 1, 3), 0);
		TestEqual(*(L + TEXT(": farther K2 lands (Receive on P2 from K2)")), B.Count(EEvent::Receive, 1, 2), 1);
	}
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		B.Reset();
		const int32 StrikeLevel = Variant == 0 ? 2 : 0, ObjectLevel = Variant == 0 ? 0 : 2;
		Arm(B, P1, {Reach()}, 400, 0, StrikeLevel);
		Projectile(B, P1, 2, FX, 100, 0, ObjectLevel);
		B.Step();
		const FString L = FString::Printf(TEXT("S4.5 proration and reaction (strike 400 level %d, K2 100 level %d)"),
										  StrikeLevel, ObjectLevel);
		TestEqual(*(L + TEXT(": P2 health 10000 - 400 - 50")), P2->CurrentHealth, 9550);
		TestEqual(*(L + TEXT(": P1 combo counter")), P1->ComboCounter, 2);
		CheckHealthAt(*this, L + TEXT(": Hit(P1->P2) fires during its own application"), B, EEvent::Hit, 0, 1, 10000,
					  9600);
		CheckHealthAt(*this, L + TEXT(": Receive(P2<-P1) fires during its own application"), B, EEvent::Receive, 1, 0,
					  10000, 9600);
		CheckHealthAt(*this, L + TEXT(": Hit(K2->P2) fires during its own application"), B, EEvent::Hit, 2, 1, 9600,
					  9550);
		CheckHealthAt(*this, L + TEXT(": Receive(P2<-K2) fires during its own application"), B, EEvent::Receive, 1, 2,
					  9600, 9550);
		B.Step();
		CheckEntries(*this, L + TEXT(": one entry, the last landing contact's hitstun"), B,
					 {{1, HitstunTag(Variant == 0 ? 0 : 2)}});
	}
	{
		B.Reset();
		Projectile(B, P1, 2, FX, 100);
		B.Step();
		TestEqual(TEXT("S4.6 combo across frames: frame 1 K2 lands 100"), P2->CurrentHealth, 9900);
		TestEqual(TEXT("S4.6 combo across frames: frame 1 combo"), P1->ComboCounter, 1);
		Projectile(B, P1, 3, FX, 200);
		B.Step();
		TestEqual(TEXT("S4.6 combo across frames: frame 2 K3 (200) lands prorated 100"), P2->CurrentHealth, 9800);
		TestEqual(TEXT("S4.6 combo across frames: frame 2 combo"), P1->ComboCounter, 2);
	}
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		B.Reset();
		Arm(B, P1, {Reach()});
		ABattleObject* K2 = Projectile(B, P1, 2, FX, 100, Variant);
		K2->NormalHit.Hitstun = 20;
		K2->CounterHit.Hitstun = 20;
		B.Step();
		B.Step();
		TestEqual(*FString::Printf(TEXT("S4.7 stun of the last landing contact (strike hitstun 10 priority 0, K2 "
										"hitstun 20 priority %d): P2 stun after entry"),
								   Variant),
				  (int32)P2->StunTime, Variant == 0 ? 20 : 10);
	}
	return !HasAnyErrors();
}

// R5, R4: armor budgets consumed in ladder order, coverage, chip and armor damage, absorption after a landed hit
// (S5.1 to S5.6).
NSE006_TEST(FNSE006ArmorBudget, "ArmorBudget")
bool FNSE006ArmorBudget::RunTest(const FString&)
{
	FNSE006Battle B;
	APlayerObject* P1 = B.P1;
	APlayerObject* P2 = B.P2;
	{
		B.Reset();
		B.Armor(P2, 2);
		Arm(B, P1, {Reach()}, 100, 1);
		Projectile(B, P1, 2, FX, 100, 2);
		Projectile(B, P1, 3, FX, 100, 3);
		B.Step();
		TestEqual(TEXT("S5.1 two of three (armor 2; strike p1, K2 p2, K3 p3): K3 absorbed"),
				  B.Count(EEvent::Hit, 3, 1), 1);
		TestEqual(TEXT("S5.1 two of three: no Receive on P2 from K3"), B.Count(EEvent::Receive, 1, 3), 0);
		TestEqual(TEXT("S5.1 two of three: K2 absorbed"), B.Count(EEvent::Hit, 2, 1), 1);
		TestEqual(TEXT("S5.1 two of three: no Receive on P2 from K2"), B.Count(EEvent::Receive, 1, 2), 0);
		TestEqual(TEXT("S5.1 two of three: strike lands (Receive on P2 from P1)"), B.Count(EEvent::Receive, 1, 0), 1);
		TestEqual(TEXT("S5.1 two of three: P2 armor budget"), P2->SuperArmorData.ArmorHits, 0);
		TestEqual(TEXT("S5.1 two of three: P2 health"), P2->CurrentHealth, 9900);
		TestEqual(TEXT("S5.1 two of three: P1 combo counter"), P1->ComboCounter, 1);
	}
	{
		B.Reset();
		B.Armor(P2, 1, false, true);
		Arm(B, P1, {Reach()}, 100, 3);
		Projectile(B, P1, 2, FX, 100, 1, 0, 3, 0);
		Projectile(B, P1, 3, FX, 100, 2, 0, 3, 1);
		B.Step();
		TestEqual(TEXT("S5.2 coverage (projectiles only, budget 1; strike p3, K3 p2, K2 p1): strike lands without "
						"consuming (Receive on P2 from P1)"),
				  B.Count(EEvent::Receive, 1, 0), 1);
		TestEqual(TEXT("S5.2 coverage: K3 absorbed"), B.Count(EEvent::Hit, 3, 1), 1);
		TestEqual(TEXT("S5.2 coverage: no Receive on P2 from K3"), B.Count(EEvent::Receive, 1, 3), 0);
		TestEqual(TEXT("S5.2 coverage: K2 lands after the budget is spent"), B.Count(EEvent::Receive, 1, 2), 1);
		TestEqual(TEXT("S5.2 coverage: P2 health 10000 - 100 - 50"), P2->CurrentHealth, 9850);
		TestEqual(TEXT("S5.2 coverage: P2 armor budget"), P2->SuperArmorData.ArmorHits, 0);
	}
	{
		B.Reset();
		B.Armor(P2, 2);
		P2->SuperArmorData.bArmorTakeChipDamage = true;
		Projectile(B, P1, 2, FX, 100);
		Projectile(B, P1, 3, FX, 200, 1);
		B.Step();
		TestEqual(TEXT("S5.3 chip through armor: P2 health 10000 - 10 - 20"), P2->CurrentHealth, 9970);
		TestEqual(TEXT("S5.3 chip through armor: budget"), P2->SuperArmorData.ArmorHits, 0);
		TestEqual(TEXT("S5.3 chip through armor: no Receive"), B.Count(EEvent::Receive), 0);
	}
	{
		B.Reset();
		ABattleObject* K4 = Victim(B, P2, 4, 0);
		B.Armor(K4, 1, false, true);
		Projectile(B, P1, 2, 0);
		B.Step();
		TestEqual(TEXT("S5.4 object victim budget: frame 1 K2 absorbed by K4"), B.Count(EEvent::Hit, 2, 4), 1);
		TestEqual(TEXT("S5.4 object victim budget: frame 1 no Receive on K4"), B.Count(EEvent::Receive, 4, 2), 0);
		TestEqual(TEXT("S5.4 object victim budget: K4 budget"), K4->SuperArmorData.ArmorHits, 0);
		Projectile(B, P1, 3, 0);
		B.Step();
		TestEqual(TEXT("S5.4 object victim budget: frame 2 K3 lands on K4"), B.Count(EEvent::Receive, 4, 3), 1);
	}
	{
		B.Reset();
		B.Armor(P2, 1, false, true);
		Arm(B, P1, {Reach()}, 100, 0, 1);
		Projectile(B, P1, 2, FX, 100, 0);
		B.Step();
		TestEqual(TEXT("S5.5 absorption after a landed hit: P2 health"), P2->CurrentHealth, 9900);
		TestEqual(TEXT("S5.5 absorption after a landed hit: budget"), P2->SuperArmorData.ArmorHits, 0);
		TestEqual(TEXT("S5.5 absorption after a landed hit: K2 absorbed"), B.Count(EEvent::Hit, 2, 1), 1);
		TestEqual(TEXT("S5.5 absorption after a landed hit: no Receive on P2 from K2"),
				  B.Count(EEvent::Receive, 1, 2), 0);
		TestEqual(TEXT("S5.5 absorption after a landed hit: P1 combo counter stays 1 after the frame"),
				  P1->ComboCounter, 1);
		B.Step();
		CheckEntries(*this, TEXT("S5.5 absorption after a landed hit: P2 still enters Hitstun_1"), B,
					 {{1, HitstunTag(1)}});
	}
	{
		B.Reset();
		B.Armor(P2, 2);
		P2->SuperArmorData.ArmorDamagePercent = 30;
		P2->SuperArmorData.bArmorTakeChipDamage = true;
		Projectile(B, P1, 2, FX, 100);
		Projectile(B, P1, 3, FX, 200, 1);
		B.Step();
		TestEqual(TEXT("S5.6 armor damage 30 percent plus chip: P2 health 10000 - 80 - 40"), P2->CurrentHealth,
				  9880);
		TestEqual(TEXT("S5.6 armor damage: budget"), P2->SuperArmorData.ArmorHits, 0);
		TestEqual(TEXT("S5.6 armor damage: no Receive"), B.Count(EEvent::Receive), 0);
	}
	return !HasAnyErrors();
}

// R6, R4: guard covers by posture, blocks before armor, clashing strikes never chip, reaction of the last blocked
// contact (S6.1 to S6.6).
NSE006_TEST(FNSE006Guard, "Guard")
bool FNSE006Guard::RunTest(const FString&)
{
	FNSE006Battle B;
	APlayerObject* P1 = B.P1;
	APlayerObject* P2 = B.P2;
	{
		B.Reset();
		B.Guard(P2, true);
		Arm(B, P1, {Reach()}, 100, 0, 0);
		Projectile(B, P1, 2, FX, 200, 0, 1);
		B.Step(INP_Neutral, INP_Right);
		TestEqual(TEXT("S6.1 two blocked contacts: P2 health after chip 10 + 20"), P2->CurrentHealth, 9970);
		TestEqual(TEXT("S6.1 two blocked contacts: Block callbacks"), B.Count(EEvent::Block), 2);
		TestEqual(TEXT("S6.1 two blocked contacts: stun is the last blocked contact's (K2 level 1)"),
				  (int32)P2->StunTime, 11);
		TestEqual(TEXT("S6.1 two blocked contacts: no Receive"), B.Count(EEvent::Receive), 0);
		CheckHealthAt(*this, TEXT("S6.1 two blocked contacts: Block(P1->P2) fires during its own application"), B,
					  EEvent::Block, 0, 1, 10000, 9990);
		CheckHealthAt(*this, TEXT("S6.1 two blocked contacts: Block(K2->P2) fires during its own application"), B,
					  EEvent::Block, 2, 1, 9990, 9970);
		B.Step(INP_Neutral, INP_Right);
		CheckEntries(*this, TEXT("S6.1 two blocked contacts: P2 enters StandBlock once"), B,
					 {{1, StandBlockTag()}});
	}
	{
		B.Reset();
		B.Guard(P2, true);
		Arm(B, P1, {ClashPlusReach()});
		Arm(B, P2, {Centre()});
		Projectile(B, P1, 2, FX, 200, 0, 1);
		B.Step(INP_Neutral, INP_Right);
		TestEqual(TEXT("S6.2 guard plus clash: only K2 chips 20"), P2->CurrentHealth, 9980);
		TestEqual(TEXT("S6.2 guard plus clash: Block(K2->P2)"), B.Count(EEvent::Block, 2, 1), 1);
		TestEqual(TEXT("S6.2 guard plus clash: no Block(P1->P2)"), B.Count(EEvent::Block, 0, 1), 0);
		TestEqual(TEXT("S6.2 guard plus clash: no Hit"), B.Count(EEvent::Hit), 0);
		TestEqual(TEXT("S6.2 guard plus clash: P1 hitstop"), (int32)P1->Hitstop, 16);
		TestEqual(TEXT("S6.2 guard plus clash: P2 hitstop is the clash's 16"), (int32)P2->Hitstop, 16);
	}
	{
		B.Reset();
		B.Guard(P2, true);
		Arm(B, P1, {Reach()});
		ABattleObject* K2 = Projectile(B, P1, 2, FX);
		K2->HitCommon.BlockType = BLK_Low;
		B.Step(INP_Neutral, INP_Right);
		TestEqual(TEXT("S6.3 standing guard: mid strike blocked"), B.Count(EEvent::Block, 0, 1), 1);
		TestEqual(TEXT("S6.3 standing guard: low K2 lands"), B.Count(EEvent::Receive, 1, 2), 1);
		TestEqual(TEXT("S6.3 standing guard: P2 health 10000 - 10 - 100"), P2->CurrentHealth, 9890);
		B.Step(INP_Neutral, INP_Right);
		CheckEntries(*this, TEXT("S6.3 standing guard: the landing contact decides the reaction"), B,
					 {{1, HitstunTag(0)}});
	}
	{
		B.Reset();
		B.Guard(P2, true);
		Arm(B, P1, {Reach()});
		P1->HitCommon.BlockType = BLK_High;
		Projectile(B, P1, 2, FX);
		B.Step(INP_Neutral, INP_DownRight);
		TestEqual(TEXT("S6.3 crouching guard: high strike lands"), B.Count(EEvent::Receive, 1, 0), 1);
		TestEqual(TEXT("S6.3 crouching guard: mid K2 blocked"), B.Count(EEvent::Block, 2, 1), 1);
		TestEqual(TEXT("S6.3 crouching guard: P2 health 10000 - 100 - 10"), P2->CurrentHealth, 9890);
		B.Step(INP_Neutral, INP_DownRight);
		CheckEntries(*this, TEXT("S6.3 crouching guard: a block after the landing contact leaves the hitstun"), B,
					 {{1, HitstunTag(0)}});
	}
	{
		B.Reset();
		B.Guard(P2, true);
		B.Armor(P2, 1);
		Arm(B, P1, {Reach()});
		B.Step(INP_Neutral, INP_Right);
		TestEqual(TEXT("S6.4 guard before armor: chip 10"), P2->CurrentHealth, 9990);
		TestEqual(TEXT("S6.4 guard before armor: Block callbacks"), B.Count(EEvent::Block), 1);
		TestEqual(TEXT("S6.4 guard before armor: Hit callbacks"), B.Count(EEvent::Hit), 0);
		TestEqual(TEXT("S6.4 guard before armor: budget untouched"), P2->SuperArmorData.ArmorHits, 1);
	}
	{
		B.Reset();
		B.Guard(P2, true);
		ABattleObject* K2 = Projectile(B, P1, 2, FX);
		ABattleObject* K3 = Projectile(B, P2, 3, FX);
		B.Step(INP_Neutral, INP_Right);
		TestEqual(TEXT("S6.5 clashing object never chips: P2 health"), P2->CurrentHealth, 10000);
		TestEqual(TEXT("S6.5 clashing object never chips: Block callbacks"), B.Count(EEvent::Block), 0);
		TestEqual(TEXT("S6.5 clashing object never chips: K2 hitstop"), (int32)K2->Hitstop, 16);
		TestEqual(TEXT("S6.5 clashing object never chips: K3 hitstop"), (int32)K3->Hitstop, 16);
		TestFalse(TEXT("S6.5 clashing object never chips: K2 loses its hit"), HitActive(K2));
		TestFalse(TEXT("S6.5 clashing object never chips: K3 loses its hit"), HitActive(K3));
	}
	{
		B.Reset();
		B.Guard(P2, true);
		B.Armor(P2, 1, false, true);
		Arm(B, P1, {Reach()}, 100, 0, 1);
		ABattleObject* K2 = Projectile(B, P1, 2, FX);
		K2->HitCommon.BlockType = BLK_Low;
		B.Step(INP_Neutral, INP_Right);
		TestEqual(TEXT("S6.6 block then absorb: chip 10 only"), P2->CurrentHealth, 9990);
		TestEqual(TEXT("S6.6 block then absorb: stun of the blocked level-1 strike"), (int32)P2->StunTime, 11);
		TestEqual(TEXT("S6.6 block then absorb: low K2 absorbed"), B.Count(EEvent::Hit, 2, 1), 1);
		TestEqual(TEXT("S6.6 block then absorb: no Receive"), B.Count(EEvent::Receive), 0);
		TestEqual(TEXT("S6.6 block then absorb: budget"), P2->SuperArmorData.ArmorHits, 0);
		B.Step(INP_Neutral, INP_Right);
		CheckEntries(*this, TEXT("S6.6 block then absorb: the absorption leaves the block reaction in place"), B,
					 {{1, StandBlockTag()}});
	}
	{
		B.Reset();
		B.Guard(P2, true);
		Arm(B, P1, {Reach()}, 100, 0, 2);
		Projectile(B, P1, 2, FX, 100, 0, 0);
		B.Step(INP_Neutral, INP_Right);
		TestEqual(TEXT("S6.6 two blocks (strike level 2 then K2 level 0): stun of the last blocked contact"),
				  (int32)P2->StunTime, 9);
		TestEqual(TEXT("S6.6 two blocks: chip 10 + 10"), P2->CurrentHealth, 9980);
		TestEqual(TEXT("S6.6 two blocks: Block callbacks"), B.Count(EEvent::Block), 2);
	}
	return !HasAnyErrors();
}

// R7, R2: hitstop is the maximum any contact or clash assigns, per role, replacing the countdown (S7.1 to S7.4).
NSE006_TEST(FNSE006Hitstop, "Hitstop")
bool FNSE006Hitstop::RunTest(const FString&)
{
	FNSE006Battle B;
	APlayerObject* P1 = B.P1;
	APlayerObject* P2 = B.P2;
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		B.Reset();
		const int32 H1 = Variant == 0 ? 7 : 3, H2 = Variant == 0 ? 3 : 7;
		Arm(B, P1, {Reach()}, 100, 0, 0, H1);
		Arm(B, P2, {Reach()}, 100, 0, 0, H2);
		B.Step();
		TestEqual(*FString::Printf(TEXT("S7.1 trade hitstops %d and %d: P1 ends with the larger"), H1, H2),
				  (int32)P1->Hitstop, 7);
		TestEqual(*FString::Printf(TEXT("S7.1 trade hitstops %d and %d: P2 ends with the larger"), H1, H2),
				  (int32)P2->Hitstop, 7);
	}
	{
		B.Reset();
		Arm(B, P1, {Centre()});
		Arm(B, P2, {Centre()});
		Projectile(B, P1, 2, FX);
		B.Step();
		TestEqual(TEXT("S7.2 clash plus separate contact: K2 lands on P2"), B.Count(EEvent::Receive, 1, 2), 1);
		TestEqual(TEXT("S7.2 clash plus separate contact: P2 health"), P2->CurrentHealth, 9900);
		TestEqual(TEXT("S7.2 clash plus separate contact: P2 hitstop is the clash's 16, not K2's 3"),
				  (int32)P2->Hitstop, 16);
		TestEqual(TEXT("S7.2 clash plus separate contact: P1 hitstop"), (int32)P1->Hitstop, 16);
	}
	{
		B.Reset();
		Projectile(B, P1, 2, FX, 100, 0, 0, 7);
		B.Step();
		TestEqual(TEXT("S7.3 replacement: frame 1 P2 hitstop 7"), (int32)P2->Hitstop, 7);
		Projectile(B, P1, 3, FX, 100, 0, 0, 3);
		B.Step();
		TestEqual(TEXT("S7.3 replacement: frame 2 K3 (hitstop 3) replaces the remaining 6"), (int32)P2->Hitstop, 3);
	}
	{
		B.Reset();
		Arm(B, P1, {Reach()}, 100, 0, 0, 3);
		P1->CounterHit.Hitstop = 9;
		Arm(B, P2, {Reach()}, 100, 0, 0, 3);
		B.Step();
		TestEqual(TEXT("S7.4 per-role data: P2 (counter-hit victim of P1, counter hitstop 9) ends with 9"),
				  (int32)P2->Hitstop, 9);
		TestEqual(TEXT("S7.4 per-role data: P1 (attacker 3, victim of P2's counter hitstop 3) ends with 3"),
				  (int32)P1->Hitstop, 3);
	}
	{
		B.Reset();
		ABattleObject* K2 = Projectile(B, P1, 2, FX);
		K2->NormalHit.EnemyHitstopModifier = 4;
		K2->CounterHit.EnemyHitstopModifier = 4;
		B.Step();
		TestEqual(TEXT("S7.4 modifier: idle P2 gets hitstop 3 + 4"), (int32)P2->Hitstop, 7);
		TestEqual(TEXT("S7.4 modifier: K2 keeps its own 3"), (int32)K2->Hitstop, 3);
	}
	return !HasAnyErrors();
}

// R8, R4: follow-ups spawned during the phase join the next frame in ladder order; a hit disable during the phase
// applies after the frame (S8.1, S8.2).
NSE006_TEST(FNSE006CallbackDeferral, "CallbackDeferral")
bool FNSE006CallbackDeferral::RunTest(const FString&)
{
	FNSE006Battle B;
	APlayerObject* P1 = B.P1;
	APlayerObject* P2 = B.P2;
	{
		B.Reset();
		ABattleObject* K2 = Projectile(B, P1, 2, FX);
		B.OnHit(K2, ENSE006OnHit::SpawnFollowUp, 4, FX, 0);
		B.Step();
		TestEqual(TEXT("S8.1 follow-up: frame 1 P2 loses K2's damage once"), P2->CurrentHealth, 9900);
		TestEqual(TEXT("S8.1 follow-up: frame 1 no Hit on K4"), B.Count(EEvent::Hit, 4), 0);
		ABattleObject* K4 = B.Find(4);
		TestNotNull(TEXT("S8.1 follow-up: K4 spawned"), K4);
		if (K4)
		{
			TestTrue(TEXT("S8.1 follow-up: K4 hit active after the frame"), HitActive(K4));
		}
		B.Step();
		TestEqual(TEXT("S8.1 follow-up: frame 2 K4 lands (Receive on P2 from K4)"), B.Count(EEvent::Receive, 1, 4),
				  1);
		TestEqual(TEXT("S8.1 follow-up: frame 2 prorated damage"), P2->CurrentHealth, 9850);
		TestEqual(TEXT("S8.1 follow-up: frame 2 combo"), P1->ComboCounter, 2);
	}
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		B.Reset();
		ABattleObject* K2 = Projectile(B, P1, 2, 0);
		ABattleObject* K5 = Victim(B, P2, 5, 0);
		B.Armor(K5, 2, false, true);
		B.OnHit(K2, ENSE006OnHit::SpawnFollowUp, 4, 0, 0);
		B.Step();
		const FString L = FString::Printf(TEXT("S8.1 follow-up ladder on object victim (variant %d)"), Variant);
		TestEqual(*(L + TEXT(": frame 1 K2 absorbed by K5")), B.Count(EEvent::Hit, 2, 5), 1);
		TestEqual(*(L + TEXT(": frame 1 K5 budget")), K5->SuperArmorData.ArmorHits, 1);
		TestEqual(*(L + TEXT(": frame 1 no Hit on K4")), B.Count(EEvent::Hit, 4), 0);
		ABattleObject* K4 = B.Find(4);
		TestNotNull(*(L + TEXT(": K4 spawned")), K4);
		if (!K4)
		{
			continue;
		}
		ABattleObject* K3 = Projectile(B, P1, 3, 0, 100, Variant == 0 ? 1 : 0);
		if (Variant == 1)
		{
			K4->SetContactPriority(2);
		}
		const int32 Absorbed = Variant == 0 ? 3 : 4, Landed = Variant == 0 ? 4 : 3;
		B.Step();
		TestEqual(*FString::Printf(TEXT("%s: frame 2 higher priority K%d absorbed"), *L, Absorbed),
				  B.Count(EEvent::Hit, Absorbed, 5), 1);
		TestEqual(*FString::Printf(TEXT("%s: frame 2 no Receive on K5 from K%d"), *L, Absorbed),
				  B.Count(EEvent::Receive, 5, Absorbed), 0);
		TestEqual(*FString::Printf(TEXT("%s: frame 2 K%d lands"), *L, Landed), B.Count(EEvent::Receive, 5, Landed),
				  1);
		TestEqual(*(L + TEXT(": frame 2 K5 budget")), K5->SuperArmorData.ArmorHits, 0);
		(void)K3;
	}
	{
		B.Reset();
		ABattleObject* K2 = Projectile(B, P1, 2, FX);
		B.OnHit(K2, ENSE006OnHit::DisableHit);
		Victim(B, P2, 3, FX);
		B.Step();
		TestEqual(TEXT("S8.2 disable during phase: P2 health"), P2->CurrentHealth, 9900);
		TestEqual(TEXT("S8.2 disable during phase: K2 also lands on K3"), B.Count(EEvent::Receive, 3, 2), 1);
		TestFalse(TEXT("S8.2 disable during phase: K2 has no active hit after the frame"), HitActive(K2));
		Victim(B, P2, 4, FX);
		B.Step();
		TestEqual(TEXT("S8.2 disable during phase: fresh K4 is not hit next frame"), B.Count(EEvent::Receive, 4, 2),
				  0);
	}
	return !HasAnyErrors();
}

// R1, R4, R5: a target's contact order is fixed at phase start while the hit data and armor each contact applies
// with are read as it applies, so a callback of an earlier contact changes what later contacts do but not their
// order (A1 to A3).
NSE006_TEST(FNSE006LiveApplicationData, "LiveApplicationData")
bool FNSE006LiveApplicationData::RunTest(const FString&)
{
	FNSE006Battle B;
	APlayerObject* P1 = B.P1;
	APlayerObject* P2 = B.P2;
	{
		// A1: P1 (priority 3) lands first; its callback triples K3's damage and lifts K3's priority above K2's. K2
		// (priority 2 at phase start) still applies before K3 and spends the single projectile armor unit; K3 lands
		// with its new damage, prorated. K3 sits in the lower pool slot so a slot-order walk absorbs it instead.
		B.Reset();
		B.Armor(P2, 1, false, true);
		Arm(B, P1, {Reach()}, 100, 3);
		ABattleObject* K3 = Projectile(B, P1, 3, FX, 100, 1, 0, 3, 0);
		Projectile(B, P1, 2, FX, 100, 2, 0, 3, 1);
		B.OnHitDo(P1, [K3]
		{
			K3->NormalHit.Damage = 300;
			K3->CounterHit.Damage = 300;
			K3->SetContactPriority(9);
		});
		B.Step();
		TestEqual(TEXT("A1 frozen order, live data: strike lands (Receive on P2 from P1)"),
				  B.Count(EEvent::Receive, 1, 0), 1);
		TestEqual(TEXT("A1 frozen order, live data: K2 (priority 2 at phase start) absorbed"),
				  B.Count(EEvent::Hit, 2, 1), 1);
		TestEqual(TEXT("A1 frozen order, live data: no Receive on P2 from K2"), B.Count(EEvent::Receive, 1, 2), 0);
		TestEqual(TEXT("A1 frozen order, live data: K3 lands after the budget is spent"),
				  B.Count(EEvent::Receive, 1, 3), 1);
		TestEqual(TEXT("A1 frozen order, live data: P2 health 10000 - 100 - 300 * 50 / 100"), P2->CurrentHealth,
				  9750);
		TestEqual(TEXT("A1 frozen order, live data: P2 armor budget"), P2->SuperArmorData.ArmorHits, 0);
		TestEqual(TEXT("A1 frozen order, live data: P1 combo counter"), P1->ComboCounter, 2);
	}
	{
		// A2: P1's absorbed contact turns P2's armor coverage off from its callback; K2 then lands instead of
		// spending the second unit.
		B.Reset();
		B.Armor(P2, 2, true, true);
		Arm(B, P1, {Reach()}, 100, 3);
		Projectile(B, P1, 2, FX, 100, 2);
		B.OnHitDo(P1, [P2]
		{
			P2->SuperArmorData.bArmorStrike = false;
			P2->SuperArmorData.bArmorProjectile = false;
		});
		B.Step();
		TestEqual(TEXT("A2 live armor: strike absorbed (Hit on P1 with target P2)"), B.Count(EEvent::Hit, 0, 1), 1);
		TestEqual(TEXT("A2 live armor: no Receive on P2 from P1"), B.Count(EEvent::Receive, 1, 0), 0);
		TestEqual(TEXT("A2 live armor: K2 lands once coverage is gone"), B.Count(EEvent::Receive, 1, 2), 1);
		TestEqual(TEXT("A2 live armor: P2 health 10000 - 100"), P2->CurrentHealth, 9900);
		TestEqual(TEXT("A2 live armor: one budget unit spent"), P2->SuperArmorData.ArmorHits, 1);
		TestEqual(TEXT("A2 live armor: P1 combo counter"), P1->ComboCounter, 1);
	}
	{
		// A3: P1's landing contact grants P2 projectile armor from its callback; K2 is absorbed and the landed hit's
		// reaction stays.
		B.Reset();
		Arm(B, P1, {Reach()}, 100, 3, 1);
		Projectile(B, P1, 2, FX, 100, 2);
		B.OnHitDo(P1, [&B, P2] { B.Armor(P2, 1, false, true); });
		B.Step();
		TestEqual(TEXT("A3 armor granted by a callback: strike lands"), B.Count(EEvent::Receive, 1, 0), 1);
		TestEqual(TEXT("A3 armor granted by a callback: K2 absorbed"), B.Count(EEvent::Hit, 2, 1), 1);
		TestEqual(TEXT("A3 armor granted by a callback: no Receive on P2 from K2"), B.Count(EEvent::Receive, 1, 2), 0);
		TestEqual(TEXT("A3 armor granted by a callback: P2 health 10000 - 100"), P2->CurrentHealth, 9900);
		TestEqual(TEXT("A3 armor granted by a callback: budget spent"), P2->SuperArmorData.ArmorHits, 0);
		TestEqual(TEXT("A3 armor granted by a callback: P1 combo counter"), P1->ComboCounter, 1);
		B.Step();
		CheckEntries(*this, TEXT("A3 armor granted by a callback: P2 still enters Hitstun_1"), B,
					 {{1, HitstunTag(1)}});
	}
	return !HasAnyErrors();
}

// R9: contact once per activation, EnableHit forgets, reset and reuse forget both directions and zero the priority,
// priority survives fighter state changes (S9.1 to S9.4).
NSE006_TEST(FNSE006ContactHistory, "ContactHistory")
bool FNSE006ContactHistory::RunTest(const FString&)
{
	FNSE006Battle B;
	APlayerObject* P1 = B.P1;
	APlayerObject* P2 = B.P2;
	{
		B.Reset();
		ABattleObject* K2 = Projectile(B, P1, 2, FX);
		for (int32 I = 0; I < 3; ++I)
		{
			B.Step();
		}
		TestEqual(TEXT("S9.1 once per activation: Receive on P2 from K2 over three frames"),
				  B.Count(EEvent::Receive, 1, 2), 1);
		TestEqual(TEXT("S9.1 once per activation: P2 health"), P2->CurrentHealth, 9900);
		K2->EnableHit(true);
		B.Step();
		TestEqual(TEXT("S9.1 EnableHit(true) forgets: K2 lands again"), B.Count(EEvent::Receive, 1, 2), 2);
		TestEqual(TEXT("S9.1 EnableHit(true) forgets: P2 health after the prorated second hit"), P2->CurrentHealth,
				  9850);
	}
	{
		B.Reset();
		ABattleObject* K2 = Projectile(B, P1, 2, FX, 100, 5);
		B.Step();
		TestEqual(TEXT("S9.2 reuse: K2 (priority 5) lands"), B.Count(EEvent::Receive, 1, 2), 1);
		const int32 Slot = (int32)K2->ObjNumber;
		K2->DeactivateObject();
		for (int32 I = 0; I < 3; ++I)
		{
			B.Step();
		}
		TestTrue(TEXT("S9.2 reuse: K2 active through its hitstop"), K2->IsActive);
		B.Step();
		TestFalse(TEXT("S9.2 reuse: K2 inactive"), K2->IsActive);
		ABattleObject* K6 = B.Spawn(P1, 6, FX, 0, Slot);
		TestEqual(TEXT("S9.2 reuse: same pool slot"), K6 ? (int32)K6->ObjNumber : -1, Slot);
		TestEqual(TEXT("S9.2 reuse: reused object starts with priority zero"), K6 ? K6->ContactPriority : -1, 0);
		B.Strike(K6);
		B.Boxes(K6, {Hurt(), HitBox()});
		B.Step();
		TestEqual(TEXT("S9.2 reuse: reused slot lands on P2 again"), B.Count(EEvent::Receive, 1, 6), 1);
	}
	{
		B.Reset();
		Arm(B, P1, {Reach()});
		ABattleObject* K3 = Victim(B, P2, 3, FX);
		B.Step();
		TestEqual(TEXT("S9.2 reuse as target: P1 hits K3"), B.Count(EEvent::Receive, 3, 0), 1);
		const int32 Slot = (int32)K3->ObjNumber;
		K3->DeactivateObject();
		for (int32 I = 0; I < 4; ++I)
		{
			B.Step();
		}
		TestFalse(TEXT("S9.2 reuse as target: K3 inactive"), K3->IsActive);
		Victim(B, P2, 7, FX, Slot);
		B.Step();
		TestEqual(TEXT("S9.2 reuse as target: P1's still-active strike lands on the new object in the same slot"),
				  B.Count(EEvent::Receive, 7, 0), 1);
	}
	{
		B.Reset();
		B.Guard(P2, true);
		Projectile(B, P1, 2, FX);
		B.Step(INP_Neutral, INP_Right);
		B.Step(INP_Neutral, INP_Right);
		TestEqual(TEXT("S9.3 blocked contact consumes the activation: one Block over two frames"),
				  B.Count(EEvent::Block, 2, 1), 1);
		TestEqual(TEXT("S9.3 blocked contact consumes the activation: chip once"), P2->CurrentHealth, 9990);
	}
	{
		B.Reset();
		B.Armor(P2, 1, true, true);
		Projectile(B, P1, 2, FX);
		B.Step();
		B.Step();
		TestEqual(TEXT("S9.3 absorbed contact consumes the activation: one Hit over two frames"),
				  B.Count(EEvent::Hit, 2, 1), 1);
		TestEqual(TEXT("S9.3 absorbed contact consumes the activation: budget"), P2->SuperArmorData.ArmorHits, 0);
		TestEqual(TEXT("S9.3 absorbed contact consumes the activation: no Receive on frame 2 with budget 0"),
				  B.Count(EEvent::Receive, 1, 2), 0);
	}
	{
		B.Reset();
		B.Strike(P1, 100, 5);
		Projectile(B, P2, 3, -FX);
		B.Step();
		TestEqual(TEXT("S9.4 priority survives: K3 hits P1 on frame 1"), B.Count(EEvent::Receive, 0, 3), 1);
		B.Step();
		CheckEntries(*this, TEXT("S9.4 priority survives: P1 enters Hitstun_0 on frame 2"), B,
					 {{0, HitstunTag(0)}});
		TestEqual(TEXT("S9.4 priority survives the fighter state change"), P1->ContactPriority, 5);
		B.Defer(P1, [&] { Arm(B, P1, {Reach()}, 100, 5); });
		B.Step();
		B.Step();
		TestEqual(TEXT("S9.4 re-armed P1 lands on P2 on frame 4"), B.Count(EEvent::Receive, 1, 0), 1);
		B.Defer(P1, [&] { Arm(B, P1, {Reach()}, 100, 5); });
		for (int32 I = 0; I < 3; ++I)
		{
			B.Step();
		}
		TestEqual(TEXT("S9.4 Strike re-issued without EnableHit keeps the history: no second Receive by frame 7"),
				  B.Count(EEvent::Receive, 1, 0), 1);
	}
	return !HasAnyErrors();
}

namespace
{
Oracle::FParticipant Fighter(int32 Side)
{
	Oracle::FParticipant P;
	P.Key = Side;
	P.bFighter = true;
	P.Side = Side;
	P.X = Side == 0 ? -FX : FX;
	P.bFacingLeft = Side == 1;
	P.Hurt.Add({4, 4, 0, 0});
	return P;
}
Oracle::FParticipant Object(int32 Key, int32 Side, int32 X, bool bAttacking)
{
	Oracle::FParticipant P;
	P.Key = Key;
	P.Side = Side;
	P.X = X;
	P.bFacingLeft = Side == 1;
	P.Hurt.Add({4, 4, 0, 0});
	P.bAttacking = bAttacking;
	if (bAttacking)
	{
		P.Hit.Add({4, 4, 0, 0});
	}
	return P;
}
// The fixed S10 encounter: strike (100, p1) plus K2 (300, p0 at 50000), K3 (100, p2 at 49998), K4 (100, p0 at
// 50001) on P2 with armor 2. Ladder: K3 absorbed, strike absorbed, K2 lands 300, K4 lands 50.
Oracle::FEncounter PermutationEncounter()
{
	Oracle::FEncounter E;
	auto P1 = Fighter(0);
	P1.bAttacking = true;
	P1.Priority = 1;
	P1.Hit.Add({4, 4, 100100, 0});
	E.P.Add(P1);
	auto P2 = Fighter(1);
	P2.Armor.bStrikes = P2.Armor.bProjectiles = true;
	P2.Armor.Budget = 2;
	E.P.Add(P2);
	auto K2 = Object(2, 0, FX, true);
	K2.Damage = K2.CounterDamage = 300;
	E.P.Add(K2);
	auto K3 = Object(3, 0, FX - 2, true);
	K3.Priority = 2;
	E.P.Add(K3);
	E.P.Add(Object(4, 0, FX + 1, true));
	return E;
}
const int32 SlotPatterns[4][3] = {{0, 1, 2}, {3, 2, 1}, {1, 3, 0}, {2, 0, 3}};
const int32 SpawnOrders[6][3] = {{2, 3, 4}, {2, 4, 3}, {3, 2, 4}, {3, 4, 2}, {4, 2, 3}, {4, 3, 2}};
} // namespace

// R10: every spawn order and slot pattern of one encounter gives the same outcome, and a priority change changes it
// (S10.1, S10.2).
NSE006_TEST(FNSE006Permutations, "Permutations")
bool FNSE006Permutations::RunTest(const FString&)
{
	FNSE006Battle B;
	const Oracle::FEncounter E = PermutationEncounter();
	Oracle::FOutcome Expected;
	TestTrue(TEXT("S10.1 oracle ladder is total"), Oracle::Resolve(E, Expected));
	FObserved First;
	bool bHaveFirst = false;
	for (int32 Order = 0; Order < 6; ++Order)
	{
		for (int32 Pattern = 0; Pattern < 4; ++Pattern)
		{
			TArray<int32> SpawnOrder, Slots;
			for (int32 I = 0; I < 3; ++I)
			{
				SpawnOrder.Add(SpawnOrders[Order][I]);
				Slots.Add(SlotPatterns[Pattern][I]);
			}
			Build(B, E, SpawnOrder, Slots);
			B.Step();
			const FObserved Got = Observe(B, ObjectKeys(E));
			const FString L = FString::Printf(TEXT("S10.1 spawn order (K%d,K%d,K%d) slots (%d,%d,%d)"),
											  E.P[SpawnOrder[0]].Key, E.P[SpawnOrder[1]].Key,
											  E.P[SpawnOrder[2]].Key, Slots[0], Slots[1], Slots[2]);
			TestEqual(*(L + TEXT(": P2 health 10000 - 300 - 50")), B.P2->CurrentHealth, 9650);
			TestEqual(*(L + TEXT(": P2 armor budget")), B.P2->SuperArmorData.ArmorHits, 0);
			TestEqual(*(L + TEXT(": P1 combo counter")), B.P1->ComboCounter, 2);
			TestEqual(*(L + TEXT(": K3 absorbed")), B.Count(EEvent::Hit, 3, 1), 1);
			TestEqual(*(L + TEXT(": strike absorbed")), B.Count(EEvent::Hit, 0, 1), 1);
			TestEqual(*(L + TEXT(": K2 lands")), B.Count(EEvent::Receive, 1, 2), 1);
			TestEqual(*(L + TEXT(": K4 lands")), B.Count(EEvent::Receive, 1, 4), 1);
			const FString Diff = DiffOutcome(Expected, Got.O);
			TestTrue(*(L + TEXT(": matches the oracle: ") + Diff), Diff.IsEmpty());
			if (!bHaveFirst)
			{
				First = Got;
				bHaveFirst = true;
			}
			else
			{
				const FString PermDiff = DiffOutcome(First.O, Got.O);
				TestTrue(*(L + TEXT(": equals the first build: ") + PermDiff), PermDiff.IsEmpty());
			}
			if (HasAnyErrors())
			{
				AddInfo(E.Describe());
				return false;
			}
		}
	}
	{
		Oracle::FEncounter Changed = E;
		Changed.P[2].Priority = 3;
		Changed.P[3].Priority = 0;
		Build(B, Changed, {2, 3, 4}, {0, 1, 2});
		B.Step();
		TestEqual(TEXT("S10.2 discrimination (K2 p3, K3 p0): K2 absorbed, strike absorbed, K3 and K4 land"),
				  B.P2->CurrentHealth, 9850);
		TestEqual(TEXT("S10.2 discrimination: K2 absorbed"), B.Count(EEvent::Hit, 2, 1), 1);
		TestEqual(TEXT("S10.2 discrimination: no Receive from K2"), B.Count(EEvent::Receive, 1, 2), 0);
		TestEqual(TEXT("S10.2 discrimination: K3 lands"), B.Count(EEvent::Receive, 1, 3), 1);
	}
	return !HasAnyErrors();
}

namespace
{
// One frame of an S11.4 episode: everything a replay must reproduce, as one comparable string.
FString CaptureFrame(const FNSE006Battle& B, int32 TraceStart, int32 EntriesStart)
{
	FString S = FString::Printf(TEXT("hp=%d/%d combo=%d/%d hs=%d/%d stun=%d/%d p1armor=%d"), B.P1->CurrentHealth,
								B.P2->CurrentHealth, B.P1->ComboCounter, B.P2->ComboCounter, (int32)B.P1->Hitstop,
								(int32)B.P2->Hitstop, (int32)B.P1->StunTime, (int32)B.P2->StunTime,
								B.P1->SuperArmorData.ArmorHits);
	for (int32 Key : {2, 3, 4, 6})
	{
		const ABattleObject* K = B.Find(Key);
		S += FString::Printf(TEXT(" K%d(%s hs=%d act=%d)"), Key, K ? TEXT("on") : TEXT("off"),
							 K ? (int32)K->Hitstop : -1, K ? HitActive(K) : 0);
	}
	TArray<FString> Events;
	for (int32 I = TraceStart; I < B.Trace.Num(); ++I)
	{
		const auto& E = B.Trace[I];
		Events.Add(Oracle::EventText(E.Kind, E.Actor, E.Other, E.TargetHealth));
	}
	Events.Sort();
	S += TEXT(" events[") + FString::Join(Events, TEXT(",")) + TEXT("]");
	TArray<FNSE006StateEntry> Entries;
	for (int32 I = EntriesStart; I < B.StateEntries.Num(); ++I)
	{
		Entries.Add(B.StateEntries[I]);
	}
	S += TEXT(" entries[") + Join(EntryNames(Entries)) + TEXT("]");
	return S;
}
} // namespace

// R11: contact history and priority in the snapshot; restore plus identical replay reproduces every outcome
// (S11.1 to S11.4).
NSE006_TEST(FNSE006Rollback, "Rollback")
bool FNSE006Rollback::RunTest(const FString&)
{
	FNSE006Battle B;
	APlayerObject* P1 = B.P1;
	APlayerObject* P2 = B.P2;
	{
		B.Reset();
		Projectile(B, P1, 2, 0);
		Victim(B, P2, 3, 0);
		B.Step();
		TestEqual(TEXT("S11.1 pre-snapshot memory: K2 lands on K3 on frame 1"), B.Count(EEvent::Receive, 3, 2), 1);
		const int32 H = B.Snapshot();
		B.Step();
		TestEqual(TEXT("S11.1 pre-snapshot memory: no second contact on frame 2"), B.Count(EEvent::Receive, 3, 2), 1);
		B.Restore(H);
		B.Step();
		TestEqual(TEXT("S11.1 pre-snapshot memory: contact before the snapshot stays remembered after restore"),
				  B.Count(EEvent::Receive, 3, 2), 0);
	}
	{
		B.Reset();
		Projectile(B, P1, 2, 0);
		Victim(B, P2, 3, 0);
		const int32 H = B.Snapshot();
		B.Step();
		TestEqual(TEXT("S11.2 discarded future (object victim): K2 lands on K3"), B.Count(EEvent::Receive, 3, 2), 1);
		B.Restore(H);
		B.Step();
		TestEqual(TEXT("S11.2 discarded future (object victim): K2 lands on K3 again after restoring the "
						"pre-contact snapshot"),
				  B.Count(EEvent::Receive, 3, 2), 1);
	}
	{
		B.Reset();
		Projectile(B, P1, 2, FX);
		const int32 H = B.Snapshot();
		B.Step();
		TestEqual(TEXT("S11.2 discarded future (fighter victim): P2 health"), P2->CurrentHealth, 9900);
		B.Restore(H);
		B.Step();
		TestEqual(TEXT("S11.2 discarded future (fighter victim): P2 health after restore and replay"),
				  P2->CurrentHealth, 9900);
		TestEqual(TEXT("S11.2 discarded future (fighter victim): Receive on P2 from K2 after restore"),
				  B.Count(EEvent::Receive, 1, 2), 1);
	}
	{
		B.Reset();
		B.Armor(P2, 1);
		Arm(B, P1, {Reach()}, 300, 0);
		ABattleObject* K2 = Projectile(B, P1, 2, FX, 100, 5);
		const int32 H = B.Snapshot();
		K2->SetContactPriority(0);
		B.Restore(H);
		TestEqual(TEXT("S11.3 priority in snapshot: restored field"), K2->ContactPriority, 5);
		B.Step();
		TestEqual(TEXT("S11.3 priority in snapshot: restored priority 5 puts K2 first, absorbed"),
				  B.Count(EEvent::Hit, 2, 1), 1);
		TestEqual(TEXT("S11.3 priority in snapshot: no Receive from K2"), B.Count(EEvent::Receive, 1, 2), 0);
		TestEqual(TEXT("S11.3 priority in snapshot: strike lands 300"), P2->CurrentHealth, 9700);
	}
	{
		// S11.4: five scripted frames with a trade, an absorption on a re-armed fighter, a follow-up, an object
		// victim re-hit after EnableHit, snapshots before frame 1 and after frame 2, and a replay from each.
		B.Reset();
		Arm(B, P1, {Reach()}, 100, 0, 0, 3);
		Arm(B, P2, {Reach()}, 100, 0, 0, 3);
		Projectile(B, P1, 2, 0);
		Victim(B, P2, 6, 0);
		TArray<TFunction<void()>> Before;
		Before.SetNum(6);
		Before[2] = [&]
		{
			B.Defer(P1, [&] { B.Armor(P1, 1, false, true); });
			ABattleObject* K3 = Projectile(B, P2, 3, -FX);
			B.OnHit(K3, ENSE006OnHit::SpawnFollowUp, 4, -FX, 0);
		};
		Before[4] = [&]
		{
			if (ABattleObject* K2 = B.Find(2))
			{
				K2->EnableHit(true);
			}
		};
		auto RunFrame = [&](int32 Frame) -> FString
		{
			if (Before[Frame])
			{
				Before[Frame]();
			}
			const int32 TraceStart = B.Trace.Num(), EntriesStart = B.StateEntries.Num();
			B.Step();
			return CaptureFrame(B, TraceStart, EntriesStart);
		};
		TArray<FString> Record;
		Record.SetNum(6);
		const int32 H0 = B.Snapshot();
		int32 H2 = -1;
		for (int32 Frame = 1; Frame <= 5; ++Frame)
		{
			Record[Frame] = RunFrame(Frame);
			if (Frame == 2)
			{
				H2 = B.Snapshot();
			}
		}
		TestEqual(TEXT("S11.4 frame 1: trade, P1 health"), Record[1].Contains(TEXT("hp=9900/9900")), true);
		TestTrue(TEXT("S11.4 frame 1: K2 lands on K6 (Receive:6:2)"), Record[1].Contains(TEXT("Receive:6:2:-1")));
		TestTrue(TEXT("S11.4 frame 2: K3 absorbed by re-armed P1 (Hit:3:0)"), Record[2].Contains(TEXT("Hit:3:0:")));
		TestTrue(TEXT("S11.4 frame 2: no Receive from K3"), !Record[2].Contains(TEXT("Receive:0:3")));
		TestTrue(TEXT("S11.4 frame 3: follow-up K4 lands on P1"), Record[3].Contains(TEXT("Receive:0:4:")));
		TestTrue(TEXT("S11.4 frame 3: P1 health 9850"), Record[3].Contains(TEXT("hp=9850/9900")));
		TestTrue(TEXT("S11.4 frame 4: K2 lands on K6 again after EnableHit"),
				 Record[4].Contains(TEXT("Receive:6:2:-1")));
		for (int32 Start : {0, 2})
		{
			B.Restore(Start == 0 ? H0 : H2);
			for (int32 Frame = Start + 1; Frame <= 5; ++Frame)
			{
				const FString Replay = RunFrame(Frame);
				TestEqual(*FString::Printf(TEXT("S11.4 replay from snapshot after frame %d: frame %d expected {%s} got {%s}"),
										   Start, Frame, *Record[Frame], *Replay),
						  Replay, Record[Frame]);
			}
		}
	}
	return !HasAnyErrors();
}

// R12, R9, R10, R11: a hit re-enabled from a callback while the frame's contacts apply forgets its history as usual,
// and the contacts still to apply that frame belong to the old activation, so every overlapped target is contacted
// again next frame, in either slot order and after a restore (B1).
NSE006_TEST(FNSE006ReenableDuringPhase, "ReenableDuringPhase")
bool FNSE006ReenableDuringPhase::RunTest(const FString&)
{
	FNSE006Battle B;
	APlayerObject* P1 = B.P1;
	APlayerObject* P2 = B.P2;
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		B.Reset();
		ABattleObject* K2 = Projectile(B, P1, 2, 0);
		Victim(B, P2, Variant == 0 ? 3 : 4, 0);
		Victim(B, P2, Variant == 0 ? 4 : 3, 0);
		B.OnHitDo(K2, [K2] { K2->EnableHit(true); });
		const int32 H = B.Snapshot();
		const FString L = FString::Printf(TEXT("B1 re-enable during the phase (victims spawned %s)"),
										  Variant == 0 ? TEXT("K3 then K4") : TEXT("K4 then K3"));
		auto ThreeFrames = [&](const FString& Prefix)
		{
			B.Step();
			TestEqual(*(Prefix + TEXT(": frame 1 K2 lands on K3")), B.Count(EEvent::Receive, 3, 2), 1);
			TestEqual(*(Prefix + TEXT(": frame 1 K2 lands on K4")), B.Count(EEvent::Receive, 4, 2), 1);
			B.Step();
			TestEqual(*(Prefix + TEXT(": frame 2 the new activation contacts K3 again")),
					  B.Count(EEvent::Receive, 3, 2), 2);
			TestEqual(*(Prefix + TEXT(": frame 2 the new activation contacts K4 again")),
					  B.Count(EEvent::Receive, 4, 2), 2);
			B.Step();
			TestEqual(*(Prefix + TEXT(": frame 3 nothing more on K3")), B.Count(EEvent::Receive, 3, 2), 2);
			TestEqual(*(Prefix + TEXT(": frame 3 nothing more on K4")), B.Count(EEvent::Receive, 4, 2), 2);
		};
		ThreeFrames(L);
		B.Restore(H);
		ThreeFrames(L + TEXT(", replayed from the snapshot"));
	}
	return !HasAnyErrors();
}

// R11, R9: contact history and priority restored from a snapshot belong to the object that occupied a slot at the
// snapshot, in both directions, even after the slot was reused (C1 to C3).
NSE006_TEST(FNSE006ReuseRollback, "ReuseRollback")
bool FNSE006ReuseRollback::RunTest(const FString&)
{
	FNSE006Battle B;
	APlayerObject* P1 = B.P1;
	APlayerObject* P2 = B.P2;
	auto Retire = [&](ABattleObject* K)
	{
		K->DeactivateObject();
		for (int32 I = 0; I < 8 && K->IsActive; ++I)
		{
			B.Step();
		}
		return !K->IsActive;
	};
	{
		// C1: snapshot before any contact; K2 hits K3, K3 retires, K5 takes its slot and is hit; after the restore K3
		// is back with no history and is hit again.
		B.Reset();
		Projectile(B, P1, 2, 0, 100, 0, 0, 3, 0);
		ABattleObject* K3 = Victim(B, P2, 3, 0, 1);
		const int32 Slot = (int32)K3->ObjNumber;
		const int32 H = B.Snapshot();
		B.Step();
		TestEqual(TEXT("C1 discarded future across reuse: K2 lands on K3"), B.Count(EEvent::Receive, 3, 2), 1);
		TestTrue(TEXT("C1 discarded future across reuse: K3 retires"), Retire(K3));
		ABattleObject* K5 = Victim(B, P2, 5, 0, Slot);
		TestEqual(TEXT("C1 discarded future across reuse: K5 took K3's slot"), K5 ? (int32)K5->ObjNumber : -1, Slot);
		B.Step();
		TestEqual(TEXT("C1 discarded future across reuse: K2 lands on the replacement K5"),
				  B.Count(EEvent::Receive, 5, 2), 1);
		B.Restore(H);
		B.Step();
		TestEqual(TEXT("C1 discarded future across reuse: after the restore K2 lands on K3 again"),
				  B.Count(EEvent::Receive, 3, 2), 1);
	}
	{
		// C2: K2 hits K3, snapshot; K2 retires, K6 takes its slot and hits K3; after the restore K3 still remembers K2
		// until K2's hit is re-enabled.
		B.Reset();
		ABattleObject* K2 = Projectile(B, P1, 2, 0, 100, 0, 0, 3, 0);
		Victim(B, P2, 3, 0, 1);
		B.Step();
		TestEqual(TEXT("C2 memory across reuse: K2 lands on K3"), B.Count(EEvent::Receive, 3, 2), 1);
		const int32 Slot = (int32)K2->ObjNumber;
		const int32 H = B.Snapshot();
		TestTrue(TEXT("C2 memory across reuse: K2 retires"), Retire(K2));
		ABattleObject* K6 = Projectile(B, P1, 6, 0, 100, 0, 0, 3, Slot);
		TestEqual(TEXT("C2 memory across reuse: K6 took K2's slot"), K6 ? (int32)K6->ObjNumber : -1, Slot);
		B.Step();
		TestEqual(TEXT("C2 memory across reuse: K6 lands on K3"), B.Count(EEvent::Receive, 3, 6), 1);
		B.Restore(H);
		B.Step();
		TestEqual(TEXT("C2 memory across reuse: after the restore K2 does not land on K3 again"),
				  B.Count(EEvent::Receive, 3, 2), 0);
		ABattleObject* Restored = B.Find(2);
		TestNotNull(TEXT("C2 memory across reuse: K2 is back after the restore"), Restored);
		if (Restored)
		{
			Restored->EnableHit(true);
			B.Step();
			TestEqual(TEXT("C2 memory across reuse: EnableHit(true) after the restore lands once"),
					  B.Count(EEvent::Receive, 3, 2), 1);
		}
	}
	{
		// C3: K2 (priority 5, no hit box yet) in slot 1 at the snapshot; it retires and K6 reuses the slot with
		// priority zero. After the restore K2 gets its hit box and a new K7 (300 damage, priority 0) takes slot 0:
		// K2's restored priority 5 spends the single armor unit and K7 lands.
		B.Reset();
		B.Armor(P2, 1, false, true);
		ABattleObject* K2 = B.Spawn(P1, 2, FX, 0, 1);
		B.Strike(K2, 100, 5);
		const int32 Slot = (int32)K2->ObjNumber;
		const int32 H = B.Snapshot();
		B.Step();
		TestEqual(TEXT("C3 priority across reuse: K2 has no hit box, nothing lands"), B.Count(EEvent::Receive), 0);
		TestTrue(TEXT("C3 priority across reuse: K2 retires"), Retire(K2));
		ABattleObject* K6 = B.Spawn(P1, 6, FX, 0, Slot);
		TestEqual(TEXT("C3 priority across reuse: K6 took K2's slot"), K6 ? (int32)K6->ObjNumber : -1, Slot);
		TestEqual(TEXT("C3 priority across reuse: the reused slot starts at priority zero before it is armed"),
				  K6 ? K6->ContactPriority : -1, 0);
		if (K6)
		{
			B.Strike(K6);
			B.Boxes(K6, {Hurt(), HitBox()});
		}
		B.Step();
		TestEqual(TEXT("C3 priority across reuse: K6 absorbed alone"), B.Count(EEvent::Hit, 6, 1), 1);
		B.Restore(H);
		ABattleObject* Restored = B.Find(2);
		TestNotNull(TEXT("C3 priority across reuse: K2 is back after the restore"), Restored);
		if (Restored)
		{
			TestEqual(TEXT("C3 priority across reuse: restored priority"), Restored->ContactPriority, 5);
			B.Boxes(Restored, {Hurt(), HitBox()});
			Projectile(B, P1, 7, FX, 300, 0, 0, 3, 0);
			B.Step();
			TestEqual(TEXT("C3 priority across reuse: K2 (priority 5) absorbed"), B.Count(EEvent::Hit, 2, 1), 1);
			TestEqual(TEXT("C3 priority across reuse: no Receive on P2 from K2"), B.Count(EEvent::Receive, 1, 2), 0);
			TestEqual(TEXT("C3 priority across reuse: K7 (300, priority 0, lower slot) lands"),
					  B.Count(EEvent::Receive, 1, 7), 1);
			TestEqual(TEXT("C3 priority across reuse: P2 health"), P2->CurrentHealth, 9700);
		}
	}
	return !HasAnyErrors();
}

namespace
{
int32 Choose(FRandomStream& R, const TArray<int32>& L)
{
	return L[R.RandRange(0, L.Num() - 1)];
}
bool Chance(FRandomStream& R, float P)
{
	return R.FRand() < P;
}
void DrawAttackData(FRandomStream& R, Oracle::FParticipant& P)
{
	P.Priority = R.RandRange(0, 2);
	P.Damage = Choose(R, {50, 100, 200, 300});
	P.CounterDamage = P.Damage + (Chance(R, 0.5f) ? 50 : 0);
	P.Hitstop = Choose(R, {2, 3, 5, 7});
	P.CounterHitstop = P.Hitstop + (Chance(R, 0.5f) ? 4 : 0);
	P.Modifier = Chance(R, 0.5f) ? 4 : 0;
	P.CounterModifier = Chance(R, 0.5f) ? 4 : 0;
	P.Hitstun = Chance(R, 0.5f) ? 20 : 10;
	P.CounterHitstun = P.Hitstun + (Chance(R, 0.5f) ? 5 : 0);
	P.Level = R.RandRange(0, 2);
	P.BlockType = Chance(R, 0.8f) ? BLK_Mid : Chance(R, 0.5f) ? BLK_Low : BLK_High;
}
void DrawArmor(FRandomStream& R, Oracle::FArmorSpec& A, int32 MaxBudget)
{
	A.Budget = R.RandRange(0, MaxBudget);
	A.bStrikes = Chance(R, 0.5f);
	A.bProjectiles = Chance(R, 0.5f);
	A.bChip = Chance(R, 0.2f);
	A.DamagePercent = Chance(R, 0.5f) ? 30 : 0;
	if (!A.Any())
	{
		A = Oracle::FArmorSpec();
	}
}
// Object positions come from three clusters regardless of owner, biased to the touch and miss boundaries of P2's
// hurtbox (49998 touches, 49997 misses; 50002 touches, 50003 misses) and their mirrors, or the middle grid.
int32 DrawObjectX(FRandomStream& R)
{
	const int32 Cluster = R.RandRange(0, 2);
	if (Cluster == 2)
	{
		return Choose(R, {-6, -3, 0, 3, 6});
	}
	const int32 X = Chance(R, 0.5f) ? Choose(R, {49996, 49997, 49998, 50000, 50002, 50003})
									: R.RandRange(49994, 50006);
	return Cluster == 0 ? X : -X;
}
Oracle::FEncounter DrawEncounter(FRandomStream& R, int32 ObjectCount)
{
	Oracle::FEncounter E;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		Oracle::FParticipant P = Fighter(Side);
		if (Chance(R, 0.6f))
		{
			P.bAttacking = true;
			const int32 Set = R.RandRange(0, 2);
			if (Set != 1)
			{
				P.Hit.Add({4, 4, 100100, 0});
			}
			if (Set != 0)
			{
				P.Hit.Add({20, 4, 50050, 0});
			}
			DrawAttackData(R, P);
		}
		DrawArmor(R, P.Armor, 3);
		E.P.Add(P);
	}
	if (Chance(R, 0.2f))
	{
		Oracle::FParticipant& G = E.P[R.RandRange(0, 1)];
		G.bGuarding = true;
		G.bCrouchGuard = Chance(R, 0.5f);
	}
	const int32 Count = ObjectCount < 0 ? R.RandRange(0, 4) : ObjectCount;
	for (int32 I = 0; I < Count; ++I)
	{
		Oracle::FParticipant P = Object(2 + I, R.RandRange(0, 1), DrawObjectX(R), Chance(R, 0.8f));
		if (P.bAttacking)
		{
			DrawAttackData(R, P);
		}
		if (Chance(R, 0.2f))
		{
			DrawArmor(R, P.Armor, 2);
		}
		E.P.Add(P);
	}
	return E;
}
// Draws until the per-target ladder is total; Redraws counts the rejected draws. Returns false when the bound is hit.
bool DrawTotalEncounter(FRandomStream& R, int32 ObjectCount, Oracle::FEncounter& E, Oracle::FOutcome& Expected,
						int32& Redraws)
{
	for (int32 Attempt = 0; Attempt < 20; ++Attempt)
	{
		E = DrawEncounter(R, ObjectCount);
		if (Oracle::Resolve(E, Expected))
		{
			return true;
		}
		++Redraws;
	}
	return false;
}
void RandomPermutation(FRandomStream& R, TArray<int32>& Items)
{
	for (int32 I = Items.Num() - 1; I > 0; --I)
	{
		Items.Swap(I, R.RandRange(0, I));
	}
}
void RandomBuild(FRandomStream& R, FNSE006Battle& B, const Oracle::FEncounter& E, FString& Description)
{
	TArray<int32> Order = ObjectIndices(E);
	RandomPermutation(R, Order);
	TArray<int32> Slots = {0, 1, 2, 3};
	RandomPermutation(R, Slots);
	Slots.SetNum(Order.Num());
	Build(B, E, Order, Slots);
	Description = TEXT("spawn order");
	for (int32 I = 0; I < Order.Num(); ++I)
	{
		Description += FString::Printf(TEXT(" K%d@slot%d"), E.P[Order[I]].Key, Slots[I]);
	}
}
int32 SwapSides(int32 Key)
{
	return Key == 0 ? 1 : Key == 1 ? 0 : Key;
}
// The mirrored encounter: sides and fighter roles swapped, positions negated, authored offsets kept.
Oracle::FEncounter Mirror(const Oracle::FEncounter& E)
{
	Oracle::FEncounter M = E;
	for (auto& P : M.P)
	{
		P.Side = 1 - P.Side;
		if (P.bFighter)
		{
			P.Key = P.Side;
		}
		P.X = -P.X;
		P.bFacingLeft = P.Side == 1;
	}
	for (auto& H : M.History)
	{
		H.A = SwapSides(H.A);
		H.B = SwapSides(H.B);
	}
	return M;
}
// An observed outcome with the fighters' roles exchanged, for comparison with the mirrored build.
Oracle::FOutcome SwapOutcome(const Oracle::FOutcome& O)
{
	Oracle::FOutcome S = O;
	Swap(S.Health[0], S.Health[1]);
	Swap(S.Combo[0], S.Combo[1]);
	Swap(S.FighterHitstop[0], S.FighterHitstop[1]);
	Swap(S.Stun[0], S.Stun[1]);
	Swap(S.Entry[0], S.Entry[1]);
	Swap(S.bClashBits[0], S.bClashBits[1]);
	Swap(S.bAssertBits[0], S.bAssertBits[1]);
	const int32 B0 = O.Budget.FindRef(0), B1 = O.Budget.FindRef(1);
	S.Budget.Add(0, B1);
	S.Budget.Add(1, B0);
	S.Events.Reset();
	S.EventHealth.Reset();
	for (const FString& Text : O.Events)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(":"), false);
		const FString Key = FString::Printf(TEXT("%s:%d:%d"), *Parts[0], SwapSides(FCString::Atoi(*Parts[1])),
											SwapSides(FCString::Atoi(*Parts[2])));
		S.Events.Add(Key);
		S.EventHealth.Add(Key, O.EventHealth.FindRef(Text));
	}
	S.Events.Sort();
	S.HitOrBlock.Reset();
	for (const auto& H : O.HitOrBlock)
	{
		S.HitOrBlock.Add(SwapSides(H.Key), H.Value);
	}
	S.HitOrBlockSlack.Reset();
	for (const auto& H : O.HitOrBlockSlack)
	{
		S.HitOrBlockSlack.Add(SwapSides(H.Key), H.Value);
	}
	S.HitOrBlockPair.Reset();
	for (const auto& H : O.HitOrBlockPair)
	{
		S.HitOrBlockPair.Add(Oracle::PairKey(SwapSides(Oracle::PairA(H.Key)), SwapSides(Oracle::PairB(H.Key))),
							 H.Value);
	}
	return S;
}
struct FOpHistogram
{
	TMap<FString, int32> Counts;
	void Add(const TCHAR* Name, int32 N = 1)
	{
		Counts.FindOrAdd(Name) += N;
	}
	FString Text() const
	{
		TArray<FString> Keys;
		Counts.GetKeys(Keys);
		Keys.Sort();
		TArray<FString> Items;
		for (const FString& K : Keys)
		{
			Items.Add(FString::Printf(TEXT("%s=%d"), *K, Counts[K]));
		}
		return FString::Join(Items, TEXT(" "));
	}
};
void CountOutcome(FOpHistogram& H, const Oracle::FOutcome& O)
{
	H.Add(TEXT("fighter_clashes"), O.FighterClashes);
	H.Add(TEXT("object_clashes"), O.ObjectClashes);
	H.Add(TEXT("multi_way_clash_strikes"), O.MultiClashStrikes);
	H.Add(TEXT("object_victim_contacts"), O.ObjectVictimContacts);
	H.Add(TEXT("trades"), O.Trade ? 1 : 0);
	H.Add(TEXT("blocks"), O.Blocks);
	H.Add(TEXT("absorptions"), O.Absorbs);
	H.Add(TEXT("armor_damage_absorptions"), O.ArmorDamageAbsorbs);
	H.Add(TEXT("landings"), O.Lands);
	H.Add(TEXT("follow_up_landings"), O.FollowUpLands);
	H.Add(TEXT("multi_contact_targets"), O.MultiContactTargets);
}
} // namespace

// R1 to R7 and R10 by seeded single-frame encounters: oracle agreement, reaction after one more frame, permutation
// equality under a second spawn order and slot pattern, and the mirror relation every fourth episode.
NSE006_TEST(FNSE006FuzzSingleFrame, "FuzzSingleFrame")
bool FNSE006FuzzSingleFrame::RunTest(const FString&)
{
	const double Start = FPlatformTime::Seconds();
	const double CapSeconds = 90.0;
	TArray<int32> Seeds = {6001, 6002, 6003};
	const int32 SeedOverride = GetSeed(0);
	if (SeedOverride)
	{
		Seeds = {SeedOverride};
	}
	const int32 Iterations = GetIterations(150);
	const int32 Floor = FMath::Min(150, Iterations * Seeds.Num());
	FNSE006Battle B;
	FOpHistogram H;
	int32 Episodes = 0;
	bool bCapped = false;
	for (int32 Seed : Seeds)
	{
		FRandomStream R(Seed);
		for (int32 Episode = 0; Episode < Iterations && !bCapped; ++Episode)
		{
			if (FPlatformTime::Seconds() - Start > CapSeconds)
			{
				bCapped = true;
				break;
			}
			Oracle::FEncounter E;
			Oracle::FOutcome Expected;
			int32 Redraws = 0;
			const bool bTotal = DrawTotalEncounter(R, -1, E, Expected, Redraws);
			H.Add(TEXT("redraws"), Redraws);
			if (!bTotal)
			{
				H.Add(TEXT("skipped_tied"));
				continue;
			}
			int32 In1, In2;
			Inputs(E, In1, In2);
			const TArray<int32> Keys = ObjectKeys(E);
			auto Fail = [&](const FString& Where, const FString& Diff, const FString& BuildText)
			{
				AddInfo(FString::Printf(TEXT("seed=%d episode=%d %s"), Seed, Episode, *BuildText));
				AddInfo(TEXT("encounter: ") + E.Describe());
				AddInfo(TEXT("oracle: ") + Expected.Describe());
				AddInfo(TEXT("histogram so far: ") + H.Text());
				TestTrue(*FString::Printf(TEXT("FuzzSingleFrame seed %d episode %d %s: %s"), Seed, Episode, *Where,
										  *Diff),
						 false);
			};
			FString BuildText;
			RandomBuild(R, B, E, BuildText);
			B.Step(In1, In2);
			const FObserved First = Observe(B, Keys);
			FString Diff = DiffOutcome(Expected, First.O);
			if (!Diff.IsEmpty())
			{
				Fail(TEXT("contact frame versus oracle"), Diff, BuildText);
				return false;
			}
			const int32 TraceCount = B.Trace.Num();
			B.Step(In1, In2);
			if (B.Trace.Num() != TraceCount)
			{
				Fail(TEXT("frame after"), FString::Printf(TEXT("%d unexpected callbacks on the frame after"),
														  B.Trace.Num() - TraceCount),
					 BuildText);
				return false;
			}
			Diff = DiffReaction(Expected, B);
			if (!Diff.IsEmpty())
			{
				Fail(TEXT("reaction entry on the frame after"), Diff, BuildText);
				return false;
			}
			FString SecondText;
			RandomBuild(R, B, E, SecondText);
			B.Step(In1, In2);
			const FObserved Second = Observe(B, Keys);
			Diff = DiffOutcome(First.O, Second.O);
			if (!Diff.IsEmpty())
			{
				Fail(TEXT("permutation equality"), Diff, BuildText + TEXT(" versus ") + SecondText);
				return false;
			}
			H.Add(TEXT("permutation_pairs"));
			if (Episode % 4 == 0)
			{
				const Oracle::FEncounter M = Mirror(E);
				int32 MIn1, MIn2;
				Inputs(M, MIn1, MIn2);
				FString MirrorText;
				RandomBuild(R, B, M, MirrorText);
				B.Step(MIn1, MIn2);
				const FObserved Mirrored = Observe(B, Keys);
				Diff = DiffOutcome(SwapOutcome(First.O), Mirrored.O);
				if (!Diff.IsEmpty())
				{
					Fail(TEXT("mirror relation"), Diff, BuildText + TEXT(" mirrored ") + MirrorText);
					AddInfo(TEXT("mirrored encounter: ") + M.Describe());
					return false;
				}
				H.Add(TEXT("mirrored"));
			}
			CountOutcome(H, Expected);
			if (E.P[0].bGuarding || E.P[1].bGuarding)
			{
				H.Add(TEXT("guarding_fighters"));
			}
			++Episodes;
		}
	}
	AddInfo(FString::Printf(TEXT("FuzzSingleFrame seeds=%s episodes=%d seconds=%.1f histogram: %s"),
							*FString::JoinBy(Seeds, TEXT(","), [](int32 S) { return FString::FromInt(S); }),
							Episodes, FPlatformTime::Seconds() - Start, *H.Text()));
	TestTrue(*FString::Printf(TEXT("FuzzSingleFrame coverage floor: %d episodes completed, %d required"), Episodes,
							  Floor),
			 Episodes >= Floor);
	return !HasAnyErrors();
}

// R10 by enumeration: for seeded three-object encounters every spawn order and every ordered slot assignment gives
// one outcome, the first build matches the oracle, and a priority flip that the oracle says matters changes it.
NSE006_TEST(FNSE006FuzzPermutations, "FuzzPermutations")
bool FNSE006FuzzPermutations::RunTest(const FString&)
{
	const double Start = FPlatformTime::Seconds();
	const double CapSeconds = 90.0;
	FRandomStream R(GetSeed(6401));
	const int32 Encounters = GetIterations(10);
	const int32 Floor = FMath::Min(6, Encounters);
	FNSE006Battle B;
	FOpHistogram H;
	int32 Done = 0;
	TArray<TArray<int32>> SlotAssignments;
	for (int32 A = 0; A < 4; ++A)
	{
		for (int32 C = 0; C < 4; ++C)
		{
			for (int32 D = 0; D < 4; ++D)
			{
				if (A != C && A != D && C != D)
				{
					SlotAssignments.Add({A, C, D});
				}
			}
		}
	}
	for (int32 Index = 0; Index < Encounters; ++Index)
	{
		if (FPlatformTime::Seconds() - Start > CapSeconds)
		{
			break;
		}
		Oracle::FEncounter E;
		Oracle::FOutcome Expected;
		int32 Redraws = 0;
		bool bFound = false;
		for (int32 Attempt = 0; Attempt < 20 && !bFound; ++Attempt)
		{
			bFound = DrawTotalEncounter(R, 3, E, Expected, Redraws) && Expected.MultiContactTargets > 0;
		}
		H.Add(TEXT("redraws"), Redraws);
		if (!bFound)
		{
			H.Add(TEXT("skipped"));
			continue;
		}
		int32 In1, In2;
		Inputs(E, In1, In2);
		const TArray<int32> Keys = ObjectKeys(E);
		const TArray<int32> Objects = ObjectIndices(E);
		FObserved First;
		bool bHaveFirst = false;
		int32 Builds = 0;
		for (int32 Order = 0; Order < 6; ++Order)
		{
			for (const TArray<int32>& Slots : SlotAssignments)
			{
				TArray<int32> SpawnOrder;
				for (int32 I = 0; I < 3; ++I)
				{
					SpawnOrder.Add(Objects[SpawnOrders[Order][I] - 2]);
				}
				Build(B, E, SpawnOrder, Slots);
				B.Step(In1, In2);
				const FObserved Got = Observe(B, Keys);
				const FString BuildText = FString::Printf(TEXT("spawn order K%d,K%d,K%d slots %d,%d,%d"),
														  E.P[SpawnOrder[0]].Key, E.P[SpawnOrder[1]].Key,
														  E.P[SpawnOrder[2]].Key, Slots[0], Slots[1], Slots[2]);
				FString Diff;
				if (!bHaveFirst)
				{
					Diff = DiffOutcome(Expected, Got.O);
					First = Got;
					bHaveFirst = true;
				}
				else
				{
					Diff = DiffOutcome(First.O, Got.O);
				}
				++Builds;
				if (!Diff.IsEmpty())
				{
					AddInfo(FString::Printf(TEXT("encounter %d %s"), Index, *BuildText));
					AddInfo(TEXT("encounter: ") + E.Describe());
					AddInfo(TEXT("oracle: ") + Expected.Describe());
					AddInfo(TEXT("histogram so far: ") + H.Text());
					TestTrue(*FString::Printf(TEXT("FuzzPermutations encounter %d build %d (%s): %s"), Index, Builds,
											  *BuildText, *Diff),
							 false);
					return false;
				}
			}
		}
		H.Add(TEXT("builds"), Builds);
		CountOutcome(H, Expected);
		// Discrimination: raise the priority of the last contact in some multi-contact ladder.
		Oracle::FEncounter Changed = E;
		Oracle::FOutcome ChangedExpected;
		bool bChanged = false;
		for (int32 I = Expected.Contacts.Num() - 1; I >= 0 && !bChanged; --I)
		{
			const int32 Attacker = Expected.Contacts[I].A;
			for (auto& P : Changed.P)
			{
				if (P.Key == Attacker)
				{
					P.Priority = 9;
				}
			}
			if (Oracle::Resolve(Changed, ChangedExpected) && !DiffOutcome(Expected, ChangedExpected).IsEmpty())
			{
				bChanged = true;
			}
			else
			{
				Changed = E;
			}
		}
		if (bChanged)
		{
			int32 CIn1, CIn2;
			Inputs(Changed, CIn1, CIn2);
			Build(B, Changed, Objects, {0, 1, 2});
			B.Step(CIn1, CIn2);
			const FObserved Got = Observe(B, Keys);
			const FString Same = DiffOutcome(First.O, Got.O);
			const FString Diff = DiffOutcome(ChangedExpected, Got.O);
			if (Same.IsEmpty() || !Diff.IsEmpty())
			{
				AddInfo(TEXT("encounter: ") + E.Describe());
				AddInfo(TEXT("changed: ") + Changed.Describe());
				AddInfo(TEXT("oracle for changed: ") + ChangedExpected.Describe());
				TestTrue(*FString::Printf(TEXT("FuzzPermutations encounter %d discrimination: priority flip %s; %s"),
										  Index, Same.IsEmpty() ? TEXT("left the outcome unchanged") : TEXT("changed it"),
										  *Diff),
						 false);
				return false;
			}
			H.Add(TEXT("discriminated"));
		}
		++Done;
	}
	AddInfo(FString::Printf(TEXT("FuzzPermutations encounters=%d seconds=%.1f histogram: %s"), Done,
							FPlatformTime::Seconds() - Start, *H.Text()));
	TestTrue(*FString::Printf(TEXT("FuzzPermutations coverage floor: %d encounters completed, %d required"), Done,
							  Floor),
			 Done >= Floor);
	return !HasAnyErrors();
}

// Stateful model for FuzzStateful: objects on the middle grid, fighters that strike at most the centre, so no
// fighter is ever a victim and no fighter data is wiped. Per frame: hitstop countdown, pending deactivation, pending
// move, then the single-frame oracle with the tracked history, then the callback effects of the frame.
namespace Stateful
{
struct FObj
{
	int32 Key = -1, Side = 0, X = 0;
	bool bActive = true, bAttacker = false, bHitActive = false, bHasHurt = true;
	int32 Hitstop = 0, Priority = 0, Damage = 100, HitstopValue = 3;
	Oracle::FArmorSpec Armor;
	bool bDeactivatePending = false, bMovePending = false;
	int32 MoveDX = 0;
	ENSE006OnHit OnHit = ENSE006OnHit::None;
	int32 FollowUpKey = -1, FollowUpX = 0;
};
struct FModel
{
	TArray<FObj> Objects;
	bool FighterAttacking[2] = {false, false};
	int32 FighterHitstop[2] = {0, 0};
	TArray<Oracle::FPair> History;
	int32 ActiveCount() const
	{
		int32 N = 0;
		for (const auto& O : Objects)
		{
			N += O.bActive ? 1 : 0;
		}
		return N;
	}
	FObj* Find(int32 Key)
	{
		for (auto& O : Objects)
		{
			if (O.Key == Key)
			{
				return &O;
			}
		}
		return nullptr;
	}
	TArray<int32> Keys() const
	{
		TArray<int32> K;
		for (const auto& O : Objects)
		{
			K.Add(O.Key);
		}
		return K;
	}
	void Forget(int32 Key, bool bAsTargetToo)
	{
		History.RemoveAll([&](const Oracle::FPair& P) { return P.A == Key || (bAsTargetToo && P.B == Key); });
	}
};
enum class EOp : uint8
{
	Move,
	EnableHit,
	OnHit,
	Spawn,
	Snapshot
};
struct FOp
{
	EOp Kind = EOp::Snapshot;
	int32 Key = -1, DX = 0;
	ENSE006OnHit Action = ENSE006OnHit::None;
	int32 FollowUpKey = -1, FollowUpX = 0;
	int32 Side = 0, X = 0;
	bool bAttacker = false;
	int32 Damage = 100, Priority = 0, HitstopValue = 3, Slot = -1;
	Oracle::FArmorSpec Armor;
	FString Text() const
	{
		switch (Kind)
		{
		case EOp::Move:
			return FString::Printf(TEXT("move(K%d,%d)"), Key, DX);
		case EOp::EnableHit:
			return FString::Printf(TEXT("enablehit(K%d)"), Key);
		case EOp::OnHit:
			return FString::Printf(TEXT("onhit(K%d,%d,K%d@%d)"), Key, (int32)Action, FollowUpKey, FollowUpX);
		case EOp::Spawn:
			return FString::Printf(TEXT("spawn(K%d side=%d x=%d atk=%d dmg=%d prio=%d hs=%d armor=%d/%d/%d slot=%d)"),
								   Key, Side, X, bAttacker, Damage, Priority, HitstopValue, Armor.bStrikes,
								   Armor.bProjectiles, Armor.Budget, Slot);
		default:
			return TEXT("snapshot");
		}
	}
};
// Applies one operation to the engine and to the model. Returns false when the engine has no such object.
bool Apply(FNSE006Battle& B, FModel& M, const FOp& Op)
{
	switch (Op.Kind)
	{
	case EOp::Move:
	{
		ABattleObject* K = B.Find(Op.Key);
		FObj* O = M.Find(Op.Key);
		if (!K || !O)
		{
			return false;
		}
		B.Move(K, Op.DX, 0);
		O->bMovePending = true;
		O->MoveDX = Op.DX;
		return true;
	}
	case EOp::EnableHit:
	{
		ABattleObject* K = B.Find(Op.Key);
		FObj* O = M.Find(Op.Key);
		if (!K || !O)
		{
			return false;
		}
		K->EnableHit(true);
		O->bHitActive = true;
		M.Forget(Op.Key, false);
		return true;
	}
	case EOp::OnHit:
	{
		ABattleObject* K = B.Find(Op.Key);
		FObj* O = M.Find(Op.Key);
		if (!K || !O)
		{
			return false;
		}
		B.OnHit(K, Op.Action, Op.FollowUpKey, Op.FollowUpX, 0);
		O->OnHit = Op.Action;
		O->FollowUpKey = Op.FollowUpKey;
		O->FollowUpX = Op.FollowUpX;
		return true;
	}
	case EOp::Spawn:
	{
		ABattleObject* K = B.Spawn(B.Game->Players[Op.Side], Op.Key, Op.X, 0, Op.Slot);
		if (!K)
		{
			return false;
		}
		if (Op.bAttacker)
		{
			B.Strike(K, Op.Damage, Op.Priority, 0, Op.HitstopValue);
			B.Boxes(K, {Hurt(), HitBox()});
		}
		if (Op.Armor.Any())
		{
			B.Armor(K, Op.Armor.Budget, Op.Armor.bStrikes, Op.Armor.bProjectiles);
		}
		FObj O;
		O.Key = Op.Key;
		O.Side = Op.Side;
		O.X = Op.X;
		O.bAttacker = Op.bAttacker;
		O.bHitActive = Op.bAttacker;
		O.Damage = Op.Damage;
		O.Priority = Op.Priority;
		O.HitstopValue = Op.HitstopValue;
		O.Armor = Op.Armor;
		M.Objects.Add(O);
		return true;
	}
	default:
		return true;
	}
}
// One battle frame of the model. Returns false on a tied ladder (the generator makes damages unique, so this is
// a generator defect rather than an engine one).
bool StepModel(FModel& M, Oracle::FOutcome& Out, int32& FollowUpsSpawned, int32& PoolFull)
{
	for (auto& O : M.Objects)
	{
		if (!O.bActive)
		{
			continue;
		}
		if (O.Hitstop > 0)
		{
			O.Hitstop--;
		}
		else if (O.bDeactivatePending)
		{
			O.bActive = false;
			M.Forget(O.Key, true);
		}
		else if (O.bMovePending)
		{
			O.X += O.MoveDX;
			O.bMovePending = false;
		}
	}
	for (int32 S = 0; S < 2; ++S)
	{
		if (M.FighterHitstop[S] > 0)
		{
			M.FighterHitstop[S]--;
		}
	}
	Oracle::FEncounter E;
	for (int32 S = 0; S < 2; ++S)
	{
		Oracle::FParticipant P = Fighter(S);
		P.bAttacking = M.FighterAttacking[S];
		if (P.bAttacking)
		{
			P.Hit.Add({20, 4, 50050, 0});
		}
		P.HitstopLeft = M.FighterHitstop[S];
		E.P.Add(P);
	}
	for (const auto& O : M.Objects)
	{
		if (!O.bActive)
		{
			continue;
		}
		Oracle::FParticipant P;
		P.Key = O.Key;
		P.Side = O.Side;
		P.X = O.X;
		P.bFacingLeft = O.Side == 1;
		if (O.bHasHurt)
		{
			P.Hurt.Add({4, 4, 0, 0});
		}
		P.bAttacking = O.bAttacker;
		P.bHitActive = O.bHitActive;
		if (O.bAttacker)
		{
			P.Hit.Add({4, 4, 0, 0});
		}
		P.Priority = O.Priority;
		P.Damage = P.CounterDamage = O.Damage;
		P.Hitstop = P.CounterHitstop = O.HitstopValue;
		P.Armor = O.Armor;
		P.HitstopLeft = O.Hitstop;
		E.P.Add(P);
	}
	E.History = M.History;
	if (!Oracle::Resolve(E, Out))
	{
		return false;
	}
	for (int32 S = 0; S < 2; ++S)
	{
		M.FighterHitstop[S] = Out.FighterHitstop[S];
		if (Out.bClashBits[S])
		{
			M.FighterAttacking[S] = false;
		}
	}
	for (auto& O : M.Objects)
	{
		if (!O.bActive)
		{
			continue;
		}
		O.Hitstop = Out.Hitstop.FindRef(O.Key);
		O.bHitActive = Out.Active.FindRef(O.Key);
		O.Armor.Budget = Out.Budget.FindRef(O.Key);
	}
	M.History.Append(Out.Contacts);
	// Callback effects: every Hit callback of an attacker (landed or absorbed contact) runs its armed action.
	TArray<FObj> Spawned;
	for (auto& O : M.Objects)
	{
		if (!O.bActive || O.OnHit == ENSE006OnHit::None)
		{
			continue;
		}
		const FString Prefix = FString::Printf(TEXT("Hit:%d:"), O.Key);
		bool bHit = false;
		for (const FString& Text : Out.Events)
		{
			if (Text.StartsWith(Prefix))
			{
				bHit = true;
			}
		}
		if (!bHit)
		{
			continue;
		}
		switch (O.OnHit)
		{
		case ENSE006OnHit::DisableHit:
			// The engine clears the flag inside the callback, so the outcome compared after the frame sees it too.
			O.bHitActive = false;
			Out.Active.Add(O.Key, false);
			break;
		case ENSE006OnHit::Deactivate:
			O.bDeactivatePending = true;
			break;
		case ENSE006OnHit::SpawnFollowUp:
			if (M.ActiveCount() + Spawned.Num() < FNSE006Battle::PoolSize)
			{
				FObj F;
				F.Key = O.FollowUpKey;
				F.Side = O.Side;
				F.X = O.FollowUpX;
				F.bAttacker = true;
				F.bHitActive = true;
				F.bHasHurt = false;
				Spawned.Add(F);
				++FollowUpsSpawned;
			}
			else
			{
				++PoolFull;
			}
			O.OnHit = ENSE006OnHit::None;
			break;
		default:
			break;
		}
	}
	M.Objects.Append(Spawned);
	return true;
}
} // namespace Stateful

// R8, R9, R11 by seeded multi-frame episodes against the explicit model, with a snapshot restored at the end of
// every episode and the recorded operations replayed from it.
NSE006_TEST(FNSE006FuzzStateful, "FuzzStateful")
bool FNSE006FuzzStateful::RunTest(const FString&)
{
	using namespace Stateful;
	const double Start = FPlatformTime::Seconds();
	const double CapSeconds = 90.0;
	TArray<int32> Seeds = {6201, 6202};
	const int32 SeedOverride = GetSeed(0);
	if (SeedOverride)
	{
		Seeds = {SeedOverride};
	}
	const int32 Iterations = GetIterations(100);
	const int32 Floor = FMath::Min(100, Iterations * Seeds.Num());
	const TArray<int32> Grid = {-6, -3, 0, 3, 6};
	FNSE006Battle B;
	FOpHistogram H;
	int32 Episodes = 0;
	bool bCapped = false;
	for (int32 Seed : Seeds)
	{
		FRandomStream R(Seed);
		for (int32 Episode = 0; Episode < Iterations && !bCapped; ++Episode)
		{
			if (FPlatformTime::Seconds() - Start > CapSeconds)
			{
				bCapped = true;
				break;
			}
			B.Reset();
			FModel M;
			for (int32 S = 0; S < 2; ++S)
			{
				M.FighterAttacking[S] = Chance(R, 0.5f);
				if (M.FighterAttacking[S])
				{
					Arm(B, B.Game->Players[S], {Centre()});
					H.Add(TEXT("fighter_centre_strikes"));
				}
			}
			int32 NextKey = 2;
			bool bFollowUpUsed = false;
			const int32 Frames = R.RandRange(3, 6);
			TArray<TArray<FOp>> Ops;
			Ops.SetNum(Frames + 1);
			TArray<int32> Handles, SnapshotFrames;
			TArray<FModel> ModelSnapshots;
			TArray<FString> Records;
			Records.SetNum(Frames + 1);
			TArray<FString> Log;
			auto Fail = [&](int32 Frame, const FString& Where, const FString& Diff)
			{
				AddInfo(FString::Printf(TEXT("seed=%d episode=%d frame=%d frames=%d fighters=%d/%d"), Seed, Episode,
										Frame, Frames, M.FighterAttacking[0], M.FighterAttacking[1]));
				AddInfo(TEXT("operations: ") + FString::Join(Log, TEXT(" ; ")));
				AddInfo(TEXT("histogram so far: ") + H.Text());
				TestTrue(*FString::Printf(TEXT("FuzzStateful seed %d episode %d frame %d %s: %s"), Seed, Episode,
										  Frame, *Where, *Diff),
						 false);
			};
			TArray<int32> FreeSlots;
			auto DrawSpawn = [&]() -> FOp
			{
				FOp Op;
				Op.Kind = EOp::Spawn;
				Op.Key = NextKey++;
				Op.Side = R.RandRange(0, 1);
				Op.X = Choose(R, Grid);
				Op.bAttacker = Chance(R, 0.7f);
				// Unique damages keep every same-target ladder total; the fixture's follow-ups use 100.
				Op.Damage = 110 + 10 * Op.Key;
				Op.Priority = R.RandRange(0, 1);
				Op.HitstopValue = Choose(R, {2, 3, 5});
				if (Chance(R, Op.bAttacker ? 0.3f : 0.6f))
				{
					Op.Armor.Budget = R.RandRange(1, 2);
					Op.Armor.bStrikes = Chance(R, 0.5f);
					Op.Armor.bProjectiles = !Op.Armor.bStrikes || Chance(R, 0.5f);
				}
				// FreeSlots is the engine's free pool at the start of the boundary minus the slots taken by spawns
				// already drawn for it; forcing a free slot makes the fixture take exactly that slot.
				Op.Slot = Choose(R, FreeSlots);
				FreeSlots.Remove(Op.Slot);
				return Op;
			};
			auto DrawOps = [&](int32 Frame, TArray<FOp>& Out)
			{
				FreeSlots.Reset();
				for (int32 I = 0; I < FNSE006Battle::PoolSize; ++I)
				{
					if (!B.Game->Objects[I]->IsActive)
					{
						FreeSlots.Add(I);
					}
				}
				TArray<int32> Active, Attackers;
				for (const auto& O : M.Objects)
				{
					if (O.bActive)
					{
						Active.Add(O.Key);
						if (O.bAttacker)
						{
							Attackers.Add(O.Key);
						}
					}
				}
				if (Frame == 1)
				{
					const int32 N = R.RandRange(1, 3);
					for (int32 I = 0; I < N; ++I)
					{
						Out.Add(DrawSpawn());
					}
					FOp Snap;
					Snap.Kind = EOp::Snapshot;
					Out.Add(Snap);
					return;
				}
				const int32 N = R.RandRange(0, 2);
				int32 PendingSpawns = 0;
				for (int32 I = 0; I < N; ++I)
				{
					const float Roll = R.FRand();
					FOp Op;
					if (Roll < 0.3f && Active.Num())
					{
						// A new move replaces any pending one, so the clamp uses the base position.
						Op.Kind = EOp::Move;
						Op.Key = Choose(R, Active);
						const FObj* O = M.Find(Op.Key);
						TArray<int32> Deltas = {0};
						if (O->X - 3 >= -6)
						{
							Deltas.Add(-3);
						}
						if (O->X + 3 <= 6)
						{
							Deltas.Add(3);
						}
						Op.DX = Choose(R, Deltas);
					}
					else if (Roll < 0.45f && Attackers.Num())
					{
						Op.Kind = EOp::EnableHit;
						Op.Key = Choose(R, Attackers);
					}
					else if (Roll < 0.65f && Attackers.Num())
					{
						Op.Kind = EOp::OnHit;
						Op.Key = Choose(R, Attackers);
						if (!bFollowUpUsed && Chance(R, 0.5f))
						{
							Op.Action = ENSE006OnHit::SpawnFollowUp;
							Op.FollowUpKey = NextKey++;
							Op.FollowUpX = Choose(R, Grid);
							bFollowUpUsed = true;
						}
						else
						{
							Op.Action = Chance(R, 0.5f) ? ENSE006OnHit::DisableHit : ENSE006OnHit::Deactivate;
						}
					}
					else if (M.ActiveCount() + PendingSpawns < FNSE006Battle::PoolSize && FreeSlots.Num())
					{
						Op = DrawSpawn();
						++PendingSpawns;
					}
					else
					{
						continue;
					}
					Out.Add(Op);
				}
				if (Chance(R, 0.4f))
				{
					FOp Snap;
					Snap.Kind = EOp::Snapshot;
					Out.Add(Snap);
				}
			};
			bool bEpisodeFailed = false, bTied = false;
			int32 FollowUps = 0, PoolFull = 0;
			for (int32 Frame = 1; Frame <= Frames && !bEpisodeFailed; ++Frame)
			{
				DrawOps(Frame, Ops[Frame]);
				for (const FOp& Op : Ops[Frame])
				{
					Log.Add(FString::Printf(TEXT("f%d %s"), Frame, *Op.Text()));
					if (Op.Kind == EOp::Snapshot)
					{
						Handles.Add(B.Snapshot());
						SnapshotFrames.Add(Frame);
						ModelSnapshots.Add(M);
						H.Add(TEXT("snapshots"));
						continue;
					}
					H.Add(Op.Kind == EOp::Move		   ? TEXT("op_move")
						  : Op.Kind == EOp::EnableHit ? TEXT("op_enablehit")
						  : Op.Kind == EOp::OnHit	   ? TEXT("op_onhit")
													   : TEXT("op_spawn"));
					if (Op.Kind == EOp::OnHit)
					{
						H.Add(Op.Action == ENSE006OnHit::SpawnFollowUp ? TEXT("armed_followup")
							  : Op.Action == ENSE006OnHit::DisableHit  ? TEXT("armed_disable")
																		: TEXT("armed_deactivate"));
					}
					if (!Apply(B, M, Op))
					{
						Fail(Frame, TEXT("operation"), TEXT("engine has no object for ") + Op.Text());
						return false;
					}
				}
				B.Trace.Reset();
				B.Step();
				Oracle::FOutcome Out;
				if (!StepModel(M, Out, FollowUps, PoolFull))
				{
					bTied = true;
					break;
				}
				const FObserved Got = Observe(B, M.Keys());
				FString Diff = DiffOutcome(Out, Got.O, false);
				for (const auto& O : M.Objects)
				{
					if (Diff.IsEmpty() && !O.bActive && B.Find(O.Key))
					{
						Diff = FString::Printf(TEXT("K%d should be inactive"), O.Key);
					}
				}
				if (!Diff.IsEmpty())
				{
					AddInfo(TEXT("model outcome: ") + Out.Describe());
					Fail(Frame, TEXT("versus model"), Diff);
					return false;
				}
				Records[Frame] = Got.O.Describe();
				CountOutcome(H, Out);
			}
			if (bTied)
			{
				H.Add(TEXT("tie_skipped"));
				continue;
			}
			H.Add(TEXT("follow_ups_spawned"), FollowUps);
			H.Add(TEXT("follow_ups_pool_full"), PoolFull);
			// Restore one snapshot and replay the recorded operations from its frame.
			const int32 Pick = R.RandRange(0, Handles.Num() - 1);
			B.Restore(Handles[Pick]);
			M = ModelSnapshots[Pick];
			H.Add(TEXT("restores"));
			for (int32 Frame = SnapshotFrames[Pick]; Frame <= Frames; ++Frame)
			{
				if (Frame > SnapshotFrames[Pick])
				{
					for (const FOp& Op : Ops[Frame])
					{
						if (Op.Kind != EOp::Snapshot && !Apply(B, M, Op))
						{
							Fail(Frame, TEXT("replayed operation"), TEXT("engine has no object for ") + Op.Text());
							return false;
						}
					}
				}
				B.Trace.Reset();
				B.Step();
				Oracle::FOutcome Out;
				int32 Dummy1 = 0, Dummy2 = 0;
				if (!StepModel(M, Out, Dummy1, Dummy2))
				{
					Fail(Frame, TEXT("replay model"), TEXT("tied ladder on replay"));
					return false;
				}
				const FObserved Got = Observe(B, M.Keys());
				FString Diff = DiffOutcome(Out, Got.O, false);
				if (!Diff.IsEmpty())
				{
					AddInfo(TEXT("model outcome: ") + Out.Describe());
					Fail(Frame, FString::Printf(TEXT("replay from snapshot before frame %d versus model"),
												SnapshotFrames[Pick]),
						 Diff);
					return false;
				}
				const FString Now = Got.O.Describe();
				if (Now != Records[Frame])
				{
					Fail(Frame, FString::Printf(TEXT("replay from snapshot before frame %d versus first run"),
												SnapshotFrames[Pick]),
						 FString::Printf(TEXT("first {%s} replay {%s}"), *Records[Frame], *Now));
					return false;
				}
				H.Add(TEXT("replayed_frames"));
				H.Add(TEXT("replayed_contacts"), Out.Contacts.Num());
			}
			++Episodes;
		}
	}
	AddInfo(FString::Printf(TEXT("FuzzStateful seeds=%s episodes=%d seconds=%.1f histogram: %s"),
							*FString::JoinBy(Seeds, TEXT(","), [](int32 S) { return FString::FromInt(S); }),
							Episodes, FPlatformTime::Seconds() - Start, *H.Text()));
	TestTrue(*FString::Printf(TEXT("FuzzStateful coverage floor: %d episodes completed, %d required"), Episodes,
							  Floor),
			 Episodes >= Floor);
	return !HasAnyErrors();
}
