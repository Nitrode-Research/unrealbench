#include "BattleObject.h"
#include <climits>
#include "PlayerObject.h"
#include "NightSkyEngine/Battle/Script/State.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"

namespace
{
	struct FContactPair
	{
		int32 Attacker;
		int32 Target;
		int32 ColPosX;
		int32 ColPosY;
		int32 Priority;
		int32 Damage;
		int64 Distance;
		bool bPlayer;
		FDetectedContact Contact;
	};

	struct FClashPair
	{
		int32 First;
		int32 Second;
		int32 ColPosX;
		int32 ColPosY;
	};
}

void ABattleObject::SetContactPriority(int32 Priority)
{
	ContactPriority = Priority;
}

void ABattleObject::AssignContactHitstop(int32 Value)
{
	Hitstop = Value;
	if (ContactHitstop) *ContactHitstop = FMath::Max(*ContactHitstop, Value);
}

void ABattleObject::ClearContactHistory(bool bAsTarget)
{
	if (ContactHistorySuperseded) *ContactHistorySuperseded = true;
	if (bAsTarget) ObjectsToIgnoreHitsFrom.Reset();
	if (!GameState) return;
	for (const auto Object : GameState->Objects) Object->ObjectsToIgnoreHitsFrom.Remove(this);
	for (const auto Fighter : GameState->Players) Fighter->ObjectsToIgnoreHitsFrom.Remove(this);
}

void ABattleObject::ResolveContacts(const TArray<ABattleObject*>& PhaseObjects)
{
	// Canonical traversal also orders callbacks across different targets and clashes.
	// Never use pool slots, spawn-generated object names or pointer addresses as keys.
	struct FParticipant
	{
		ABattleObject* Object;
		bool bPlayer;
		int32 Side;
		FString StateName;
		FString StateClass;
		int32 X, Y, Z;
		EObjDir Facing;
	};
	TArray<FParticipant> Participants;
	Participants.Reserve(PhaseObjects.Num());
	for (auto* Object : PhaseObjects)
	{
		const UState* State = Object->IsPlayer ? Object->Player->PrimaryStateMachine.CurrentState : Object->ObjectState.Get();
		Participants.Add({Object, Object->IsPlayer, Object->Player->PlayerIndex,
			State ? State->Name.ToString() : FString(), State ? State->GetClass()->GetPathName() : FString(),
			Object->PosX, Object->PosY, Object->PosZ, Object->Direction});
	}
	Participants.Sort([](const FParticipant& A, const FParticipant& B)
	{
		if (A.bPlayer != B.bPlayer) return A.bPlayer;
		if (A.Side != B.Side) return A.Side < B.Side;
		if (A.StateName != B.StateName) return A.StateName < B.StateName;
		if (A.StateClass != B.StateClass) return A.StateClass < B.StateClass;
		if (A.X != B.X) return A.X < B.X;
		if (A.Y != B.Y) return A.Y < B.Y;
		if (A.Z != B.Z) return A.Z < B.Z;
		return A.Facing < B.Facing;
	});
	TArray<ABattleObject*> Objects;
	Objects.Reserve(Participants.Num());
	for (const auto& Participant : Participants) Objects.Add(Participant.Object);

	TArray<FContactPair> Contacts;
	TArray<FClashPair> Clashes;
	TArray<FIntPoint> EligiblePairs;
	TArray<bool> Clashing;
	TArray<bool> Landed;
	TArray<bool> HistorySuperseded;
	TArray<int32> Hitstops;
	Clashing.Init(false, Objects.Num());
	Landed.Init(false, Objects.Num());
	HistorySuperseded.Init(false, Objects.Num());
	Hitstops.Init(INT_MIN, Objects.Num());

	const auto CheckOverlap = [](ABattleObject* Attacker, ABattleObject* Target, EBoxType TargetType,
		int32& ColPosX, int32& ColPosY)
	{
		const int32 PreviousX = Attacker->ColPosX;
		const int32 PreviousY = Attacker->ColPosY;
		const bool bOverlap = Attacker->CheckBoxOverlap(Target, BOX_Hit, FGameplayTag::EmptyTag,
			TargetType, FGameplayTag::EmptyTag);
		ColPosX = Attacker->ColPosX;
		ColPosY = Attacker->ColPosY;
		Attacker->ColPosX = PreviousX;
		Attacker->ColPosY = PreviousY;
		return bOverlap;
	};

	// No application or collision callbacks run until every pair has been tested.
	for (int32 i = 0; i < Objects.Num(); i++)
	{
		auto Attacker = Objects[i];
		for (int32 j = 0; j < Objects.Num(); j++)
		{
			auto Target = Objects[j];
			if (Attacker->Player->PlayerIndex == Target->Player->PlayerIndex
				|| !(Attacker->Player->PlayerFlags & PLF_IsOnScreen)
				|| !(Target->Player->PlayerFlags & PLF_IsOnScreen)) continue;
			EligiblePairs.Add(FIntPoint(i, j));
			if (!(Attacker->AttackFlags & ATK_IsAttacking)
				|| !(Attacker->AttackFlags & ATK_HitActive)) continue;

			int32 ColPosX;
			int32 ColPosY;
			if (i < j && Attacker->IsPlayer == Target->IsPlayer
				&& Target->AttackFlags & ATK_IsAttacking && Target->AttackFlags & ATK_HitActive
				&& CheckOverlap(Attacker, Target, BOX_Hit, ColPosX, ColPosY))
			{
				Clashes.Add({i, j, ColPosX, ColPosY});
				Clashing[i] = true;
				Clashing[j] = true;
			}

			if (Target->ObjectsToIgnoreHitsFrom.Contains(Attacker) || Target->Player->IsInvulnerable(Attacker)) continue;
			if (!CheckOverlap(Attacker, Target, BOX_Hurt, ColPosX, ColPosY)) continue;

			FContactPair Pair;
			Pair.Attacker = i;
			Pair.Target = j;
			Pair.ColPosX = ColPosX;
			Pair.ColPosY = ColPosY;
			Pair.Priority = Attacker->ContactPriority;
			Pair.Damage = Attacker->GetNormalContactDamage();
			Pair.Distance = FMath::Abs(static_cast<int64>(Attacker->PosX) - Target->PosX);
			Pair.bPlayer = Attacker->IsPlayer;
			Pair.Contact.AttackFlags = Attacker->AttackFlags;
			Pair.Contact.bCounter = (Target->AttackFlags & ATK_IsAttacking) != 0;
			if (Target->IsPlayer)
			{
				Pair.Contact.bBlocked = Target->Player->IsCorrectBlock(Attacker->HitCommon.BlockType, true,
					&Pair.Contact.GuardMeter);
				Pair.Contact.BlockState = Target->Player->GetBlockState();
			}
			Contacts.Add(Pair);
		}
	}

	Contacts.Sort([](const FContactPair& A, const FContactPair& B)
	{
		if (A.Target != B.Target) return A.Target < B.Target;
		if (A.Priority != B.Priority) return A.Priority > B.Priority;
		if (A.bPlayer != B.bPlayer) return A.bPlayer;
		if (A.Damage != B.Damage) return A.Damage > B.Damage;
		if (A.Distance != B.Distance) return A.Distance < B.Distance;
		// The published four keys win; an exact tie uses the authored canonical order.
		return A.Attacker < B.Attacker;
	});

	for (int32 i = 0; i < Objects.Num(); i++)
	{
		Objects[i]->ContactHitstop = &Hitstops[i];
		Objects[i]->ContactHistorySuperseded = &HistorySuperseded[i];
	}
	for (const auto& Pair : EligiblePairs)
		Objects[Pair.X]->HandleCustomCollision_PreHit(Objects[Pair.Y]);
	for (const auto& Clash : Clashes)
	{
		auto First = Objects[Clash.First];
		First->ColPosX = Clash.ColPosX;
		First->ColPosY = Clash.ColPosY;
		First->HandleClashCollision(Objects[Clash.Second], true);
	}
	for (auto& Pair : Contacts)
	{
		if (Clashing[Pair.Attacker]) continue;
		auto Attacker = Objects[Pair.Attacker];
		auto Target = Objects[Pair.Target];
		Attacker->ColPosX = Pair.ColPosX;
		Attacker->ColPosY = Pair.ColPosY;
		Pair.Contact.bPreserveReaction = Landed[Pair.Target];
		// Queued contacts still belong to the activation detected before callbacks ran.
		Pair.Contact.bRecordHistory = !HistorySuperseded[Pair.Attacker];
		Attacker->HandleHitCollision(Target, &Pair.Contact);
		Landed[Pair.Target] = Landed[Pair.Target] || Pair.Contact.bLanded;
	}
	for (const auto& Pair : EligiblePairs)
		Objects[Pair.X]->HandleCustomCollision_PostHit(Objects[Pair.Y]);
	for (int32 i = 0; i < Objects.Num(); i++)
	{
		Objects[i]->ContactHitstop = nullptr;
		Objects[i]->ContactHistorySuperseded = nullptr;
		if (Hitstops[i] != INT_MIN) Objects[i]->Hitstop = Hitstops[i];
	}
}
