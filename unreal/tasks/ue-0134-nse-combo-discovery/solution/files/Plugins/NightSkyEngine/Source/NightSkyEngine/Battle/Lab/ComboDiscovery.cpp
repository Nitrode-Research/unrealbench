#include "ComboDiscovery.h"

#include "NightSkyEngine/Battle/Misc/InputBuffer.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/BattleObject.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Battle/Script/State.h"
#include "NightSkyEngine/Battle/Script/StateMachine.h"
#include "NightSkyEngine/Battle/Script/Subroutine.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"
#include "NightSkyEngine/Network/FighterRunners.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"

namespace
{
struct FComboObjectData
{
	UObject* Object;
	TArray<uint8> Data;
	bool bSaveGame;

	FComboObjectData(UObject* InObject, bool bInSaveGame) : Object(InObject), bSaveGame(bInSaveGame)
	{
		FObjectWriter Writer(Data);
		Writer.ArIsSaveGame = bSaveGame;
		Object->GetClass()->SerializeBin(Writer, Object);
	}

	void Restore() const
	{
		FObjectReader Reader(Data);
		Reader.ArIsSaveGame = bSaveGame;
		Object->GetClass()->SerializeBin(Reader, Object);
	}
};

// The normal rollback buffer omits inactive objects and non-current scripts. It also
// contains shallow copies of the input buffer and screen target arrays.
struct FComboSnapshot
{
	ANightSkyGameState* Battle;
	FRollbackData Data;
	FBattleState BattleState;
	TArray<FInputBuffer> InputBuffers;
	TArray<FInputCondition> ThrowInputs;
	TArray<TArray<uint8>> PoolData;
	TArray<UState*> PoolStates;
	TArray<FComboObjectData> ExtraData;
	TArray<ABattleObject*> SortedObjects;

	explicit FComboSnapshot(ANightSkyGameState* InBattle) : Battle(InBattle), BattleState(InBattle->BattleState),
		SortedObjects(InBattle->SortedObjects)
	{
		int32 Checksum = 0;
		Battle->SaveGameState(Data, &Checksum);
		TSet<UObject*> SavedScripts;
		auto SaveScript = [this, &SavedScripts](UObject* Script)
		{
			if (Script && !SavedScripts.Contains(Script))
			{
				SavedScripts.Add(Script);
				ExtraData.Emplace(Script, false);
			}
		};
		for (APlayerObject* Player : Battle->Players)
		{
			InputBuffers.Add(Player->StoredInputBuffer);
			ThrowInputs.Add(Player->ProximityThrowInput);
			SaveScript(Player->PrimaryStateMachine.CurrentState);
			for (UState* State : Player->PrimaryStateMachine.States) SaveScript(State);
			for (const FStateMachine& Machine : Player->SubStateMachines)
			{
				SaveScript(Machine.CurrentState);
				for (UState* State : Machine.States) SaveScript(State);
			}
			for (UState* State : Player->ObjectStates) SaveScript(State);
			for (UState* State : Player->CommonObjectStates) SaveScript(State);
			for (USubroutine* Script : Player->Subroutines) SaveScript(Script);
			for (USubroutine* Script : Player->CommonSubroutines) SaveScript(Script);
		}
		for (ABattleObject* Object : Battle->Objects)
		{
			PoolData.AddDefaulted();
			PoolData.Last().SetNumUninitialized(SizeOfBattleObject);
			Object->SaveForRollback(PoolData.Last().GetData());
			PoolStates.Add(Object->ObjectState);
			ExtraData.Emplace(Object, true);
			SaveScript(Object->ObjectState);
		}
	}

	void Restore()
	{
		// LoadGameState must see a zero frame delta: Advance already undoes replay recording.
		Battle->BattleState = BattleState;
		FMemory::Memcpy(Data.BattleStateBuffer.GetData(), &Battle->BattleState.BattleStateSync, SizeOfBattleState);
		for (int32 Index = 0; Index < Battle->Players.Num(); ++Index)
		{
			APlayerObject* Player = Battle->Players[Index];
			// Restore the state pointer before LoadGameState loads that state's data.
			Player->LoadForRollbackBP(Data.PlayerData[Index]);
			Player->StoredInputBuffer = InputBuffers[Index];
			const SIZE_T Offset = reinterpret_cast<const uint8*>(&Player->StoredInputBuffer)
				- reinterpret_cast<const uint8*>(&Player->PlayerSync);
			FMemory::Memcpy(Data.CharBuffer[Index].GetData() + Offset, &Player->StoredInputBuffer, sizeof(FInputBuffer));
			Player->ProximityThrowInput = ThrowInputs[Index];
			const SIZE_T ThrowOffset = reinterpret_cast<const uint8*>(&Player->ProximityThrowInput)
				- reinterpret_cast<const uint8*>(&Player->PlayerSync);
			FMemory::Memcpy(Data.CharBuffer[Index].GetData() + ThrowOffset, &Player->ProximityThrowInput, sizeof(FInputCondition));
		}
		Battle->LoadGameState(Data);
		for (int32 Index = 0; Index < Battle->Objects.Num(); ++Index)
		{
			// Inactive slots may have no owner, so LoadForRollback cannot be used on them.
			FMemory::Memcpy(&Battle->Objects[Index]->ObjSync, PoolData[Index].GetData(), SizeOfBattleObject);
			Battle->Objects[Index]->ObjectState = PoolStates[Index];
		}
		for (const FComboObjectData& Extra : ExtraData) Extra.Restore();
		Battle->SortedObjects = SortedObjects;
		Battle->BattleState.ActiveObjectCount = BattleState.ActiveObjectCount;
	}
};

struct FComboRestore
{
	FComboSnapshot Snapshot;
	explicit FComboRestore(ANightSkyGameState* Battle) : Snapshot(Battle) {}
	~FComboRestore() { Snapshot.Restore(); }
};

bool IsIdle(const APlayerObject* Player)
{
	return Player->PrimaryStateMachine.CurrentState->StateType == EStateType::Standing;
}

bool IsFrozen(const ANightSkyGameState* Battle, const APlayerObject* Player)
{
	return !(Player->MiscFlags & MISC_IgnoreSuperFreeze)
		&& ((Battle->BattleState.SuperFreezeCaller == Player && Battle->BattleState.SuperFreezeSelfDuration > 0)
			|| (Battle->BattleState.SuperFreezeCaller != Player && Battle->BattleState.SuperFreezeDuration > 0));
}

// Player::Update decrements hitstop in Super::Update, then decrements stun when
// the remaining hitstop is zero. Collision runs after both players have updated.
// A knocked-down defender has zeroed StunTime yet cannot act, so the flag holds it.
bool HoldsThroughFrame(const ANightSkyGameState* Battle, const APlayerObject* Defender)
{
	if (!Defender->CheckIsStunned()) return false;
	return IsFrozen(Battle, Defender) || Defender->Hitstop > 1 || Defender->StunTime > 1
		|| (Defender->PlayerFlags & (PLF_IsDead | PLF_IsThrowLock | PLF_IsKnockedDown));
}

struct FComboContinuity
{
	bool bEarlierHit = false;
	bool bEscaped = false;

	explicit FComboContinuity(const APlayerObject* Attacker, const APlayerObject* Defender)
	{
		// Blockstun never anchors an earlier health-removing hit, including when
		// the attacker's counter is stale from a previous exchange.
		const EStateType DefenderType = Defender->PrimaryStateMachine.CurrentState->StateType;
		bEarlierHit = DefenderType != EStateType::Blockstun
			&& (Attacker->ComboCounter > 0 || DefenderType == EStateType::Hitstun);
		bEscaped = bEarlierHit && !Defender->CheckIsStunned();
	}

	void BeforeFrame(const ANightSkyGameState* Battle, const APlayerObject* Defender)
	{
		if (bEarlierHit && !HoldsThroughFrame(Battle, Defender)) bEscaped = true;
	}

	void AfterFrame(const APlayerObject* Defender, bool bHit)
	{
		if (bHit) bEarlierHit = true;
		if (bEarlierHit && !Defender->CheckIsStunned()) bEscaped = true;
	}
};

struct FComboFrame
{
	FGameplayTag State;
	int32 ActionTime = 0;
	int32 Damage = 0;
	bool bBegan = false;
	bool bContact = false;
	bool bHit = false;
	bool bIdle = false;
};

FComboFrame Advance(ANightSkyGameState* Battle, int32 Input, int32 OpponentInput, FComboContinuity& Continuity)
{
	APlayerObject* Attacker = Battle->GetMainPlayer(true);
	APlayerObject* Defender = Battle->GetMainPlayer(false);
	const FGameplayTag PreviousState = Attacker->PrimaryStateMachine.CurrentState->Name;
	const int32 PreviousTime = Attacker->ActionTime;
	const bool bHadContact = Attacker->CheckHasHit();
	const bool bCanUpdate = !IsFrozen(Battle, Attacker) && Attacker->Hitstop <= 1;
	const int32 Health = Defender->CurrentHealth;
	const int32 ComboCounter = Attacker->ComboCounter;
	Continuity.BeforeFrame(Battle, Defender);
	Battle->UpdateGameState(Input, OpponentInput, true);
	// Match UpdateGameState's recording condition, including during resimulation.
	if (Battle->GameInstance->FighterRunner == Multiplayer && !Battle->GameInstance->IsReplay)
	{
		Battle->GameInstance->RollbackReplay(1);
	}
	FComboFrame Frame;
	Frame.State = Attacker->PrimaryStateMachine.CurrentState->Name;
	Frame.ActionTime = Attacker->ActionTime;
	Frame.bBegan = Frame.State != PreviousState
		|| (bCanUpdate && Frame.ActionTime == 1 && PreviousTime >= 1);
	Frame.Damage = Health - Defender->CurrentHealth;
	Frame.bContact = Frame.Damage > 0 || (Attacker->CheckHasHit() && (!bHadContact || Frame.bBegan));
	Frame.bHit = Frame.bContact && Frame.Damage > 0 && Attacker->ComboCounter > ComboCounter;
	Frame.bIdle = IsIdle(Attacker);
	Continuity.AfterFrame(Defender, Frame.bHit);
	return Frame;
}

bool ComesBefore(const FComboTrial& A, const FComboTrial& B)
{
	if (A.TotalDamage != B.TotalDamage) return A.TotalDamage > B.TotalDamage;
	if (A.Steps.Num() != B.Steps.Num()) return A.Steps.Num() < B.Steps.Num();
	if (A.Steps.Last().HitFrame != B.Steps.Last().HitFrame) return A.Steps.Last().HitFrame < B.Steps.Last().HitFrame;
	for (int32 Index = 0; Index < A.Steps.Num(); ++Index)
	{
		if (A.Steps[Index].MoveIndex != B.Steps[Index].MoveIndex)
			return A.Steps[Index].MoveIndex < B.Steps[Index].MoveIndex;
		if (A.Steps[Index].Kind != B.Steps[Index].Kind) return A.Steps[Index].Kind == EComboStepKind::Cancel;
	}
	return false;
}

struct FComboSearch
{
	ANightSkyGameState* Battle;
	const FComboSearchRequest& Request;
	FComboSearchResult Result;
	int32 SimulatedFrames = 0;
	bool bExhausted = false;

	FComboSearch(ANightSkyGameState* InBattle, const FComboSearchRequest& InRequest) : Battle(InBattle), Request(InRequest) {}

	bool Step(int32 Input, FComboContinuity& Continuity, FComboFrame& Frame)
	{
		if (SimulatedFrames >= Request.MaxSimulatedFrames)
		{
			bExhausted = true;
			return false;
		}
		++SimulatedFrames;
		Frame = Advance(Battle, Input, Request.OpponentInput, Continuity);
		return true;
	}

	void AddTrial(const FComboTrial& Trial)
	{
		int32 Index = 0;
		while (Index < Result.Trials.Num() && !ComesBefore(Trial, Result.Trials[Index])) ++Index;
		if (Index >= Request.MaxResults) return;
		Result.Trials.Insert(Trial, Index);
		if (Result.Trials.Num() > Request.MaxResults) Result.Trials.Pop();
	}

	void FollowMove(FComboTrial Trial, FComboContinuity Continuity, FComboFrame Frame)
	{
		for (;;)
		{
			if (Frame.bContact)
			{
				if (!Frame.bHit || Continuity.bEscaped || Frame.State != Trial.Steps.Last().State) return;
				Trial.Steps.Last().HitFrame = Trial.Inputs.Num();
				Trial.Steps.Last().Damage = Frame.Damage;
				Trial.TotalDamage += Frame.Damage;
				// Children branch from the first contact's frame; the step's remaining contacts resolve
				// under neutral input and their sum completes its damage.
				FComboSnapshot Contact(Battle);
				if (Trial.Steps.Num() < Request.MaxSteps) Explore(Trial, Continuity, true);
				Contact.Restore();
				ResolveGroup(MoveTemp(Trial), Continuity);
				return;
			}
			if (Continuity.bEscaped || Frame.bIdle || Frame.State != Trial.Steps.Last().State
				|| Trial.Inputs.Num() >= Request.MaxFrames) return;
			if (!Step(INP_Neutral, Continuity, Frame)) return;
			Trial.Inputs.Add(INP_Neutral);
		}
	}

	// Records a completed step's route; its tape ends at the step's first contact.
	void AddLeaf(const FComboTrial& Trial)
	{
		FComboTrial Leaf = Trial;
		Leaf.Inputs.SetNum(Leaf.Steps.Last().HitFrame);
		AddTrial(Leaf);
	}

	// Follows a begun step's remaining contacts under neutral input. The group ends when the move
	// ends or the frame limit arrives; a later contact after escape drops the route.
	void ResolveGroup(FComboTrial Trial, FComboContinuity Continuity)
	{
		for (;;)
		{
			if (Trial.Inputs.Num() >= Request.MaxFrames)
			{
				AddLeaf(Trial);
				return;
			}
			FComboFrame Frame;
			if (!Step(INP_Neutral, Continuity, Frame)) return;
			Trial.Inputs.Add(INP_Neutral);
			if (Frame.bContact)
			{
				if (!Frame.bHit || Continuity.bEscaped || Frame.State != Trial.Steps.Last().State) return;
				Trial.Steps.Last().Damage += Frame.Damage;
				Trial.TotalDamage += Frame.Damage;
				continue;
			}
			// An escape between contacts does not itself end the attack. Keep
			// checking so a later non-combo contact cannot hide beyond the escape.
			if (Frame.bIdle || Frame.State != Trial.Steps.Last().State)
			{
				AddLeaf(Trial);
				return;
			}
		}
	}

	// Each prefix has at most two children per move. For four moves and three
	// steps there are at most 73 expanded prefixes: 73 * 90 * (1 + 4 + 8)
	// bounds neutral frames, start probes and move follow-through at 85,410 updates.
	void Explore(FComboTrial Prefix, FComboContinuity Continuity, bool bSourceHit)
	{
		TArray<bool> CancelFound;
		TArray<bool> LinkFound;
		CancelFound.Init(false, Request.Moves.Num());
		LinkFound.Init(false, Request.Moves.Num());
		bool bIdle = IsIdle(Battle->GetMainPlayer(true));
		bool bInitialAction = Prefix.Steps.IsEmpty() && !bIdle;
		const FGameplayTag SourceState = Battle->GetMainPlayer(true)->PrimaryStateMachine.CurrentState->Name;
		while (Prefix.Inputs.Num() < Request.MaxFrames
			&& (!Continuity.bEscaped || (Prefix.Steps.IsEmpty() && !bIdle && !bSourceHit)))
		{
			FComboSnapshot Before(Battle);
			FComboContinuity NeutralContinuity = Continuity;
			FComboFrame Neutral;
			if (!Step(INP_Neutral, NeutralContinuity, Neutral)) return;
			if (Neutral.bBegan || Neutral.bIdle) bInitialAction = false;
			FComboSnapshot AfterNeutral(Battle);
			// Kind is judged by this neutral frame's END, not its starting state.
			const bool bLink = Neutral.bIdle;
			const bool bSourceRunning = Neutral.State == SourceState && !Neutral.bBegan;
			if (bLink || (bSourceHit && bSourceRunning))
			{
				for (int32 Index = 0; Index < Request.Moves.Num(); ++Index)
				{
					if (bLink ? LinkFound[Index] : CancelFound[Index]) continue;
					Before.Restore();
					FComboContinuity BranchContinuity = Continuity;
					FComboFrame Frame;
					if (!Step(Request.Moves[Index].Input, BranchContinuity, Frame)) return;
					if (Frame.State != Request.Moves[Index].State || Frame.ActionTime != 1 || !Frame.bBegan) continue;
					if (bLink) LinkFound[Index] = true;
					else CancelFound[Index] = true;
					FComboTrial Trial = Prefix;
					FComboStep NewStep;
					NewStep.State = Request.Moves[Index].State;
					NewStep.MoveIndex = Index;
					NewStep.Kind = bLink ? EComboStepKind::Link : EComboStepKind::Cancel;
					NewStep.BeginFrame = Prefix.Inputs.Num() + 1;
					Trial.Steps.Add(NewStep);
					Trial.Inputs.Add(Request.Moves[Index].Input);
					FollowMove(MoveTemp(Trial), BranchContinuity, Frame);
					if (bExhausted) return;
				}
			}
			AfterNeutral.Restore();
			Prefix.Inputs.Add(INP_Neutral);
			Continuity = NeutralContinuity;
			bIdle = bLink;
			if (Neutral.bContact)
			{
				if (Prefix.Steps.IsEmpty())
				{
					// Only the situation's in-progress action may contribute an unlisted hit.
					if (!bInitialAction || !Neutral.bHit) return;
					bSourceHit = true;
					Continuity.bEscaped = !Battle->GetMainPlayer(false)->CheckIsStunned();
				}
				else
				{
					// A later contact by the last step's move joins its group; any other breaks the branch.
					if (!Neutral.bHit || Continuity.bEscaped
						|| Neutral.State != Prefix.Steps.Last().State) return;
					Prefix.Steps.Last().Damage += Neutral.Damage;
					Prefix.TotalDamage += Neutral.Damage;
				}
			}
			bool bAllFound = bIdle;
			for (bool bFound : LinkFound) bAllFound &= bFound;
			if (bAllFound) return;
		}
	}
};
} // namespace

FComboSearchResult UComboDiscovery::DiscoverCombos(ANightSkyGameState* Battle, const FComboSearchRequest& Request)
{
	FComboSearchResult Result;
	if (Request.MaxSimulatedFrames < 1) return Result;
	TSet<FGameplayTag> Names;
	for (const FComboMove& Move : Request.Moves)
	{
		if (Names.Contains(Move.State) || !Battle->GetMainPlayer(true)->PrimaryStateMachine.StateNames.Contains(Move.State))
			return Result;
		Names.Add(Move.State);
	}
	if (Request.Moves.IsEmpty() || Request.MaxSteps < 1 || Request.MaxResults < 1 || Request.MaxFrames < 1)
	{
		Result.bComplete = true;
		return Result;
	}
	FComboRestore Restore(Battle);
	FComboSearch Search(Battle, Request);
	FComboContinuity Continuity(Battle->GetMainPlayer(true), Battle->GetMainPlayer(false));
	Search.Explore(FComboTrial(), Continuity, Battle->GetMainPlayer(true)->CheckHasHit() && Continuity.bEarlierHit);
	Search.Result.bComplete = !Search.bExhausted;
	return MoveTemp(Search.Result);
}

FComboTrialReport UComboDiscovery::ValidateTrial(ANightSkyGameState* Battle, const FComboTrial& Trial, int32 OpponentInput)
{
	FComboRestore Restore(Battle);
	FComboTrialReport Report;
	APlayerObject* Attacker = Battle->GetMainPlayer(true);
	APlayerObject* Defender = Battle->GetMainPlayer(false);
	FComboContinuity Continuity(Attacker, Defender);
	const int32 StartingHealth = Defender->CurrentHealth;
	bool bInitialAction = !IsIdle(Attacker);
	// A step's contacts run from its press until the tape's next press.
	TArray<int32> Presses;
	for (int32 F = 1; F <= Trial.Inputs.Num(); ++F)
	{
		if (Trial.Inputs[F - 1] != INP_Neutral) Presses.Add(F);
	}
	int32 ContactIndex = 0;
	int32 GroupDamage = 0;
	int32 GroupHits = 0;
	const int64 TapeFrames = Trial.Inputs.Num();
	for (int64 FrameNumber = 1; ; ++FrameNumber)
	{
		// A started attack may have arbitrarily long startup/recovery. Resolve
		// it fully; lingering defender stun must not extend an ended attack.
		if (Battle->BattleState.BattlePhase != EBattlePhase::Battle
			|| (FrameNumber > TapeFrames && IsIdle(Attacker))) break;
		const int32 Input = FrameNumber <= TapeFrames ? Trial.Inputs[static_cast<int32>(FrameNumber - 1)] : INP_Neutral;
		const FComboFrame Frame = Advance(Battle, Input, OpponentInput, Continuity);
		if (Frame.bBegan || Frame.bIdle) bInitialAction = false;
		if (bInitialAction && Frame.bHit) Continuity.bEscaped = !Defender->CheckIsStunned();
		if (Report.FailedStep != -1) continue;
		Report.ObservedDamage = StartingHealth - Defender->CurrentHealth;
		// The tape's next press closes the current contact group, even one no step backs.
		while (ContactIndex < Trial.Steps.Num() && Presses.IsValidIndex(ContactIndex + 1)
			&& FrameNumber >= Presses[ContactIndex + 1])
		{
			if (GroupHits == 0 || GroupDamage != Trial.Steps[ContactIndex].Damage)
			{
				Report.FailedStep = ContactIndex;
				break;
			}
			++ContactIndex;
			GroupDamage = 0;
			GroupHits = 0;
		}
		if (Report.FailedStep != -1) continue;
		if (!Frame.bContact || (bInitialAction && Frame.bHit)) continue;
		if (!Presses.IsValidIndex(ContactIndex) || !Trial.Steps.IsValidIndex(ContactIndex) || !Frame.bHit
			|| Continuity.bEscaped || Trial.Steps[ContactIndex].State != Frame.State)
		{
			Report.FailedStep = ContactIndex;
			continue;
		}
		++GroupHits;
		GroupDamage += Frame.Damage;
	}
	for (; Report.FailedStep == -1 && ContactIndex < Trial.Steps.Num(); ++ContactIndex)
	{
		if (GroupHits == 0 || GroupDamage != Trial.Steps[ContactIndex].Damage)
		{
			Report.FailedStep = ContactIndex;
			Report.ObservedDamage = StartingHealth - Defender->CurrentHealth;
			break;
		}
		GroupDamage = 0;
		GroupHits = 0;
	}
	if (Report.FailedStep == -1) Report.bCompleted = true;
	return Report;
}
