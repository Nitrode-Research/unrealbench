#include "MatchModifiers.h"
#include "ModifierInteger.h"
#include "NightSkyGameState.h"
#include "Objects/PlayerObject.h"

namespace
{
bool IsPrintableAsciiIdentifier(const FString& Identifier)
{
	if (Identifier.IsEmpty())
		return false;
	for (TCHAR Character : Identifier)
		if (Character < 32 || Character > 126)
			return false;
	return true;
}
int32 FindDefinition(const FModifierConfiguration& Configuration, const FString& Identifier)
{
	return Configuration.Definitions.IndexOfByPredicate(
	    [&](const auto& Definition)
	    { return Definition.Identifier.Equals(Identifier, ESearchCase::CaseSensitive); });
}
bool IdentifiersEqual(const FString& Left, const FString& Right)
{
	return Left.Equals(Right, ESearchCase::CaseSensitive);
}
} // namespace

bool FModifierConfiguration::Validate(FString& Reason) const
{
	auto Reject = [&](const FString& RuleName, const FString& Message)
	{
		Reason = RuleName + TEXT(": ") + Message;
		return false;
	};
	for (int32 Index = 0; Index < Definitions.Num(); ++Index)
	{
		const auto& Definition = Definitions[Index];
		if (!IsPrintableAsciiIdentifier(Definition.Identifier))
			return Reject(Definition.Identifier,
			              TEXT("identifier must be nonempty printable ASCII"));
		if (Definition.Revision <= 0 || Definition.Drain < 0)
			return Reject(Definition.Identifier, TEXT("invalid revision or negative drain"));
		for (int32 PreviousIndex = 0; PreviousIndex < Index; ++PreviousIndex)
			if (IdentifiersEqual(Definitions[PreviousIndex].Identifier, Definition.Identifier))
				return Reject(Definition.Identifier, TEXT("duplicate definition identifier"));
		if (Definition.Subscription != EModifierEvent::Damage && Definition.Subscription != EModifierEvent::Meter)
			return Reject(Definition.Identifier, TEXT("invalid event subscription"));
		if (Definition.MatchAmount && Definition.Subscription == EModifierEvent::Damage && Definition.RequiredAmount < 0)
			return Reject(Definition.Identifier, TEXT("damage match amount must be nonnegative"));
		for (const auto& Operation : Definition.Operations)
		{
			if (uint8(Operation.Operation) > uint8(EModifierOperation::ChildMeter))
				return Reject(Definition.Identifier, TEXT("invalid operation"));
			if (Operation.Operation == EModifierOperation::Multiply &&
			    (Operation.Amount < 0 || Operation.Denominator <= 0))
				return Reject(
				    Definition.Identifier,
				    TEXT("multiplier requires nonnegative numerator and positive denominator"));
			if (Operation.Operation == EModifierOperation::ChildDamage && Operation.Amount < 0)
				return Reject(Definition.Identifier, TEXT("child damage must be nonnegative"));
		}
	}
	for (int32 Index = 0; Index < Schedule.Num(); ++Index)
	{
		const auto& Interval = Schedule[Index];
		const int32 DefinitionIndex = FindDefinition(*this, Interval.Identifier);
		if (DefinitionIndex == INDEX_NONE)
			return Reject(Interval.Identifier, TEXT("missing definition"));
		if (Definitions[DefinitionIndex].Revision != Interval.Revision)
			return Reject(Interval.Identifier, TEXT("incompatible revision"));
		if (Interval.Round <= 0 || Interval.Start < 0 || Interval.Duration == 0 ||
		    Interval.Duration < -1)
			return Reject(Interval.Identifier, TEXT("invalid round, start or duration"));
		const int64 IntervalEnd =
		    Interval.Duration == -1 ? MAX_int64 : int64(Interval.Start) + Interval.Duration;
		for (int32 PreviousIndex = 0; PreviousIndex < Index; ++PreviousIndex)
		{
			const auto& PreviousInterval = Schedule[PreviousIndex];
			if (Interval.Round != PreviousInterval.Round)
				continue;
			const int64 PreviousEnd =
			    PreviousInterval.Duration == -1
			        ? MAX_int64
			        : int64(PreviousInterval.Start) + PreviousInterval.Duration;
			if (int64(Interval.Start) >= PreviousEnd ||
			    int64(PreviousInterval.Start) >= IntervalEnd)
				continue;
			bool Conflict = IdentifiersEqual(Interval.Identifier, PreviousInterval.Identifier);
			const auto& PreviousDefinition =
			    Definitions[FindDefinition(*this, PreviousInterval.Identifier)];
			for (const auto& Group : Definitions[DefinitionIndex].ExclusiveGroups)
				for (const auto& PreviousGroup : PreviousDefinition.ExclusiveGroups)
					Conflict |= IdentifiersEqual(Group, PreviousGroup);
			if (Conflict)
				return Reject(Interval.Identifier + TEXT(" / ") + PreviousInterval.Identifier,
				              TEXT("overlapping intervals or exclusive groups"));
		}
	}
	Reason.Reset();
	return true;
}

FString FModifierConfiguration::Canonical() const
{
	// Length-prefixed fields prevent delimiter ambiguities. Sort all unordered collections.
	TArray<FModifierDefinition> SortedDefinitions = Definitions;
	SortedDefinitions.Sort(
	    [](const auto& Left, const auto& Right)
	    { return Left.Identifier.Compare(Right.Identifier, ESearchCase::CaseSensitive) < 0; });
	TArray<FModifierInterval> SortedSchedule = Schedule;
	SortedSchedule.Sort(
	    [](const auto& Left, const auto& Right)
	    {
		    if (Left.Round != Right.Round)
			    return Left.Round < Right.Round;
		    if (Left.Start != Right.Start)
			    return Left.Start < Right.Start;
		    return Left.Identifier.Compare(Right.Identifier, ESearchCase::CaseSensitive) < 0;
	    });
	FString CanonicalText = TEXT("NSE-modifiers-v1;");
	auto AppendField = [&](const FString& Value)
	{ CanonicalText += FString::FromInt(Value.Len()) + TEXT(":") + Value; };
	auto AppendNumber = [&](int64 Value) { AppendField(LexToString(Value)); };
	AppendNumber(SortedDefinitions.Num());
	for (const auto& Value : SortedDefinitions)
	{
		AppendField(Value.Identifier);
		AppendField(Value.DisplayName);
		AppendNumber(Value.Revision);
		AppendNumber(Value.Priority);
		auto Groups = Value.ExclusiveGroups;
		Groups.Sort([](const FString& Left, const FString& Right)
		            { return Left.Compare(Right, ESearchCase::CaseSensitive) < 0; });
		AppendNumber(Groups.Num());
		for (const auto& Group : Groups)
			AppendField(Group);
		AppendNumber(int32(Value.Subscription));
		AppendNumber(Value.MatchAmount);
		AppendNumber(Value.RequiredAmount);
		AppendNumber(Value.Drain);
		AppendNumber(Value.ConvertDamage);
		AppendNumber(Value.RestrictMovement);
		AppendNumber(Value.SuddenDeath);
		AppendField(Value.OwnedAttack.ToString());
		AppendNumber(Value.Operations.Num());
		for (const auto& Operation : Value.Operations)
		{
			AppendNumber(int32(Operation.Operation));
			AppendNumber(Operation.Amount);
			AppendNumber(Operation.Denominator);
			AppendNumber(Operation.ToAttacker);
		}
	}
	AppendNumber(SortedSchedule.Num());
	for (const auto& Value : SortedSchedule)
	{
		AppendField(Value.Identifier);
		AppendNumber(Value.Revision);
		AppendNumber(Value.Round);
		AppendNumber(Value.Start);
		AppendNumber(Value.Duration);
	}
	return CanonicalText;
}

FModifierConfiguration FModifierConfiguration::FourRulePreset()
{
	FModifierConfiguration Preset;
	for (const FString& Identifier : {FString(TEXT("Drain")), FString(TEXT("Conversion")),
	                                  FString(TEXT("Restriction")), FString(TEXT("SuddenDeath"))})
	{
		FModifierDefinition Definition;
		Definition.Identifier = Identifier;
		Definition.DisplayName = Identifier;
		Definition.Drain = Identifier == TEXT("Drain") ? 5 : 0;
		Definition.ConvertDamage = Identifier == TEXT("Conversion");
		Definition.RestrictMovement = Identifier == TEXT("Restriction");
		Definition.SuddenDeath = Identifier == TEXT("SuddenDeath");
		Preset.Definitions.Add(Definition);
		FModifierInterval Interval;
		Interval.Identifier = Identifier;
		Interval.Duration = Identifier == TEXT("Conversion") ? 120 : 600;
		Preset.Schedule.Add(Interval);
	}
	return Preset;
}

bool FMatchModifierState::Configure(const FModifierConfiguration& ConfigurationToApply,
                                    FString& Reason)
{
	if (!ConfigurationToApply.Validate(Reason))
	{
		Accepted = false;
		Rejection = Reason;
		return false;
	}
	Configuration = ConfigurationToApply;
	Accepted = true;
	Rejection.Reset();
	Round = 0;
	Frame = -1;
	RoundWinner = 0;
	NextOwnershipToken = 1;
	Active.Reset();
	ActiveIntervals.Reset();
	RemovedIntervals.Reset();
	DeactivateNextStep.Reset();
	Owned.Reset();
	HealthDamageSides = 0;
	return true;
}

void FMatchModifierState::RemoveOwned(ANightSkyGameState& Game, const FString& Identifier)
{
	for (int32 OwnedIndex = Owned.Num() - 1; OwnedIndex >= 0; --OwnedIndex)
	{
		if (!IdentifiersEqual(Owned[OwnedIndex].Owner, Identifier))
			continue;
		for (auto* Object : Game.Objects)
			if (Object && Object->IsActive && Object->ModifierOwnershipToken == Owned[OwnedIndex].Token)
				Object->ResetObject();
		Owned.RemoveAt(OwnedIndex);
	}
}

void FMatchModifierState::EndRound(ANightSkyGameState& Game)
{
	while (!Owned.IsEmpty())
	{
		const FString Owner = Owned.Last().Owner;
		RemoveOwned(Game, Owner);
	}
	Active.Reset();
	ActiveIntervals.Reset();
	DeactivateNextStep.Reset();
	RemovedIntervals.Reset();
	HealthDamageSides = 0;
}
void FMatchModifierState::BeginRound(ANightSkyGameState& Game, int32 Number)
{
	P1WinsAtRoundStart = Game.BattleState.P1RoundsWon;
	P2WinsAtRoundStart = Game.BattleState.P2RoundsWon;
	EndRound(Game);
	Round = Number;
	Frame = -1;
	RoundWinner = 0;
}
void FMatchModifierState::Deactivate(const FString& Identifier)
{
	if (!DeactivateNextStep.ContainsByPredicate([&](const FString& Pending)
	    { return IdentifiersEqual(Pending, Identifier); }))
		DeactivateNextStep.Add(Identifier);
}

void FMatchModifierState::BeginFrame(ANightSkyGameState& Game)
{
	if (!Accepted)
		return;
	++Frame;
	HealthDamageSides = 0;
	for (int32 ActiveIndex = ActiveIntervals.Num() - 1; ActiveIndex >= 0; --ActiveIndex)
	{
		const int32 IntervalIndex = ActiveIntervals[ActiveIndex];
		const auto& Interval = Configuration.Schedule[IntervalIndex];
		const bool Requested = DeactivateNextStep.ContainsByPredicate([&](const FString& Pending)
		    { return IdentifiersEqual(Pending, Interval.Identifier); });
		const bool Expired =
		    Interval.Round != Round ||
		    (Interval.Duration != -1 && int64(Frame) >= int64(Interval.Start) + Interval.Duration);
		if (Requested || Expired)
		{
			RemoveOwned(Game, Interval.Identifier);
			Active.Remove(FindDefinition(Configuration, Interval.Identifier));
			ActiveIntervals.RemoveAt(ActiveIndex);
			if (Requested)
				RemovedIntervals.AddUnique(IntervalIndex);
		}
	}
	TArray<int32> Starting;
	TArray<int32> Candidates;
	for (int32 IntervalIndex = 0; IntervalIndex < Configuration.Schedule.Num(); ++IntervalIndex)
		Candidates.Add(IntervalIndex);
	Candidates.Sort(
	    [&](int32 LeftIndex, int32 RightIndex)
	    {
		    const auto& LeftDefinition = Configuration.Definitions[FindDefinition(
		        Configuration, Configuration.Schedule[LeftIndex].Identifier)];
		    const auto& RightDefinition = Configuration.Definitions[FindDefinition(
		        Configuration, Configuration.Schedule[RightIndex].Identifier)];
		    return LeftDefinition.Priority != RightDefinition.Priority
		               ? LeftDefinition.Priority < RightDefinition.Priority
		               : LeftDefinition.Identifier.Compare(RightDefinition.Identifier,
		                                                   ESearchCase::CaseSensitive) < 0;
	    });
	for (int32 IntervalIndex : Candidates)
	{
		const auto& Interval = Configuration.Schedule[IntervalIndex];
		if (Interval.Round != Round || Interval.Start > Frame ||
		    RemovedIntervals.Contains(IntervalIndex))
			continue;
		if (Interval.Duration != -1 && int64(Frame) >= int64(Interval.Start) + Interval.Duration)
			continue;
		if (DeactivateNextStep.ContainsByPredicate([&](const FString& Pending)
		    { return IdentifiersEqual(Pending, Interval.Identifier); }))
		{
			RemovedIntervals.AddUnique(IntervalIndex);
			continue;
		}
		const int32 DefinitionIndex = FindDefinition(Configuration, Interval.Identifier);
		if (!Active.Contains(DefinitionIndex))
		{
			Active.Add(DefinitionIndex);
			ActiveIntervals.Add(IntervalIndex);
			Starting.Add(DefinitionIndex);
		}
	}
	Active.Sort(
	    [&](int32 LeftIndex, int32 RightIndex)
	    {
		    const auto& LeftDefinition = Configuration.Definitions[LeftIndex];
		    const auto& RightDefinition = Configuration.Definitions[RightIndex];
		    return LeftDefinition.Priority != RightDefinition.Priority
		               ? LeftDefinition.Priority < RightDefinition.Priority
		               : LeftDefinition.Identifier.Compare(RightDefinition.Identifier,
		                                                   ESearchCase::CaseSensitive) < 0;
	    });
	DeactivateNextStep.Reset();
	for (int32 DefinitionIndex : Starting)
	{
			const auto& Definition = Configuration.Definitions[DefinitionIndex];
			if (Definition.OwnedAttack.IsValid())
				for (int32 Team = 0; Team < 2; ++Team)
				{
					auto* Fighter = Game.BattleState.MainPlayer[Team];
					if (!Fighter)
						continue;
					FModifierOwnedObject Entry;
					Entry.Owner = Definition.Identifier;
					Entry.Token = NextOwnershipToken++;
					ActivatingOwnershipToken = Entry.Token;
					auto* Object = Fighter->AddBattleObject(Definition.OwnedAttack);
					ActivatingOwnershipToken = 0;
					Entry.ObjectIndex = Game.Objects.IndexOfByKey(Object);
					Owned.Add(Entry);
				}
	}

	for (int32 IntervalIndex : Active)
		if (Configuration.Definitions[IntervalIndex].Drain > 0)
			for (int32 Team = 0; Team < 2; ++Team)
				Meter(Game, Team, -Configuration.Definitions[IntervalIndex].Drain);
}

bool FMatchModifierState::MovementRestricted() const
{
	for (int32 DefinitionIndex : Active)
		if (Configuration.Definitions[DefinitionIndex].RestrictMovement)
			return true;
	return false;
}
bool FMatchModifierState::HasSuddenDeath() const
{
	for (int32 DefinitionIndex : Active)
		if (Configuration.Definitions[DefinitionIndex].SuddenDeath)
			return true;
	return false;
}
TArray<FModifierDisplay> FMatchModifierState::Display() const
{
	TArray<FModifierDisplay> DisplayedRules;
	for (int32 DefinitionIndex : Active)
	{
		const auto& Definition = Configuration.Definitions[DefinitionIndex];
		FModifierDisplay DisplayEntry;
		DisplayEntry.Identifier = Definition.Identifier;
		DisplayEntry.Name =
		    Definition.DisplayName.IsEmpty() ? Definition.Identifier : Definition.DisplayName;
		DisplayEntry.Revision = Definition.Revision;
		for (const auto& Interval : Configuration.Schedule)
			if (IdentifiersEqual(Interval.Identifier, Definition.Identifier) &&
			    Interval.Round == Round && Interval.Start <= Frame &&
			    (Interval.Duration == -1 || int64(Interval.Start) + Interval.Duration > Frame))
				DisplayEntry.RemainingFrames =
				    Interval.Duration == -1
				        ? -1
				        : int32(int64(Interval.Start) + Interval.Duration - Frame);
		DisplayedRules.Add(DisplayEntry);
	}
	return DisplayedRules;
}

int32 FMatchModifierState::Dispatch(ANightSkyGameState& Game, EModifierEvent EventKind,
                                    int32 TargetTeam, int32 SourceTeam, FModifierInteger Amount,
                                    TArray<int32> Ancestry, APlayerObject* Victim,
                                    APlayerObject* Attacker, bool Guard,
                                    TArray<FModifierPendingEvent>* DeferredChildren)
{
	struct FChildEvent
	{
		EModifierEvent EventKind;
		int32 TargetTeam;
		int32 Amount;
	};
	TArray<FChildEvent> PendingChildren;
	TArray<FModifierPendingEvent> MeterChildren;
	if (TargetTeam < 0 || TargetTeam > 1 || SourceTeam < 0 || SourceTeam > 1)
		return 0;
	if (EventKind == EModifierEvent::Damage)
		if (Amount.IsNegative()) Amount = 0;
	// SourceTeam remains the initiating attacker through resource receipts and descendants.
	// Changing the recipient, including ToAttacker, never changes that source.
	const FModifierInteger OriginalAmount = Amount;
	bool bConvertDamage = false;
	for (int32 DefinitionIndex : Active)
	{
		const auto& Definition = Configuration.Definitions[DefinitionIndex];
		if (Ancestry.Contains(DefinitionIndex) || Definition.Subscription != EventKind ||
		    (Definition.MatchAmount && OriginalAmount.Compare(Definition.RequiredAmount) != 0))
			continue;
		Ancestry.Add(DefinitionIndex);
		if (EventKind == EModifierEvent::Damage)
			bConvertDamage |= Definition.ConvertDamage;
		for (const auto& Operation : Definition.Operations)
		{
			switch (Operation.Operation)
			{
			case EModifierOperation::Cancel:
				return 0;
			case EModifierOperation::Add:
				Amount.Add(Operation.Amount);
				break;
			case EModifierOperation::Multiply:
			{
				Amount.Multiply(Operation.Amount);
				Amount.Divide(Operation.Denominator);
				break;
			}
			case EModifierOperation::ChildDamage:
				PendingChildren.Add({EModifierEvent::Damage,
				                     Operation.ToAttacker ? SourceTeam : TargetTeam,
				                     Operation.Amount});
				break;
			case EModifierOperation::ChildMeter:
				PendingChildren.Add({EModifierEvent::Meter,
				                     Operation.ToAttacker ? SourceTeam : TargetTeam,
				                     Operation.Amount});
				break;
			}
			if (EventKind == EModifierEvent::Damage)
				if (Amount.IsNegative()) Amount = 0;
		}
	}
	int32 CommittedDamageAmount = 0;
	if (EventKind == EModifierEvent::Meter)
		{
		Amount.Add(Game.BattleState.Meter[TargetTeam]);
		Game.BattleState.Meter[TargetTeam] = Amount.Bounded(0, Game.BattleState.MaxMeter[TargetTeam]);
	}
	else
	{

		if (!Victim)
			Victim = Game.BattleState.MainPlayer[TargetTeam];
		if (!Attacker)
			Attacker = Game.BattleState.MainPlayer[SourceTeam];
		if (bConvertDamage)
			Dispatch(Game, EModifierEvent::Meter, TargetTeam, SourceTeam, Amount.Negated(), Ancestry, nullptr, nullptr, false, &MeterChildren);
		else if (Victim)
		{
			const int32 HealthBefore = FMath::Max(0, Victim->CurrentHealth);
			const int32 HealthLost = Amount.Bounded(0, HealthBefore);
			Victim->CurrentHealth = HealthBefore - HealthLost;
			CommittedDamageAmount = Amount.Bounded(0, MAX_int32);
			if (HealthLost > 0)
				HealthDamageSides |= 1 << SourceTeam;
			FModifierInteger ReceivedMeter = Amount;
			ReceivedMeter.Multiply(Guard ? Victim->MeterPercentOnReceiveHitGuard : Victim->MeterPercentOnReceiveHit);
			ReceivedMeter.Divide(100, false);
			ReceivedMeter.Divide(Victim->GetMeterCooldownTimer() > 0 ? 10 : 1, false);
			if (!ReceivedMeter.IsZero())
				Dispatch(Game, EModifierEvent::Meter, TargetTeam, SourceTeam, ReceivedMeter, Ancestry, nullptr, nullptr, false, &MeterChildren);
			if (Attacker)
			{
				FModifierInteger AttackerMeterGain = Amount;
				AttackerMeterGain.Multiply(Guard ? Attacker->MeterPercentOnHitGuard : Attacker->MeterPercentOnHit);
				AttackerMeterGain.Divide(100, false);
				AttackerMeterGain.Divide(Attacker->GetMeterCooldownTimer() > 0 ? 10 : 1, false);
				if (!AttackerMeterGain.IsZero())
					Dispatch(Game, EModifierEvent::Meter, SourceTeam, SourceTeam, AttackerMeterGain, Ancestry, nullptr, nullptr, false, &MeterChildren);
			}
		}
	}
	// The parent's ordinary meter changes are now committed. Damage handlers emitted
	// their children before the ordinary meter handlers emitted theirs.
	// Each recursive dispatch finishes its subtree before the next sibling begins.
	for (const auto& Child : PendingChildren)
	{
		if (DeferredChildren)
			DeferredChildren->Add({Child.EventKind, Child.TargetTeam, SourceTeam, Child.Amount, Ancestry});
		else
			Dispatch(Game, Child.EventKind, Child.TargetTeam, SourceTeam, Child.Amount, Ancestry);
	}
	for (const auto& Child : MeterChildren)
		Dispatch(Game, Child.Kind, Child.TargetTeam, Child.SourceTeam, Child.Amount, Child.Ancestry);
	return CommittedDamageAmount;
}
int32 FMatchModifierState::Damage(ANightSkyGameState& Game, APlayerObject& Victim,
                                  APlayerObject& Attacker, int32 Amount)
{
	return Dispatch(Game, EModifierEvent::Damage, Victim.PlayerIndex, Attacker.PlayerIndex, Amount,
	                {}, &Victim, &Attacker);
}
int32 FMatchModifierState::GuardDamage(ANightSkyGameState& Game, APlayerObject& Victim,
                                     APlayerObject& Attacker, int32 Amount)
{
	return Dispatch(Game, EModifierEvent::Damage, Victim.PlayerIndex, Attacker.PlayerIndex, Amount,
	                {}, &Victim, &Attacker, true);
}
void FMatchModifierState::Meter(ANightSkyGameState& Game, int32 Team, int32 Amount)
{
	Dispatch(Game, EModifierEvent::Meter, Team, 1 - Team, Amount, {});
}

void FMatchModifierState::SpendMeter(ANightSkyGameState& Game, int32 Team, int32 Amount)
{
	Dispatch(Game, EModifierEvent::Meter, Team, 1 - Team, FModifierInteger(Amount).Negated(), {});
}
