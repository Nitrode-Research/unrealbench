#pragma once
#include "CoreMinimal.h"

// Independent interpreter for the bounded small-value seeded corpus.
// Wide numeric-domain cases use fixed mathematical expectations in NumericDomain.
// Its records are test data, not production definitions.
namespace ModifierOracle
{
enum class EKind
{
	Damage,
	Meter
};
enum class EAction
{
	Add,
	Scale,
	Cancel,
	DamageChild,
	MeterChild
};
struct FAction
{
	EAction Kind;
	int32 Value = 0, Denominator = 1;
	bool Attacker = false;
};
struct FRule
{
	FString Id;
	int32 Priority = 0;
	EKind Kind = EKind::Damage;
	TArray<FAction> Actions;
	bool Convert = false;
	bool MatchAmount = false;
	int32 RequiredAmount = 0;
	int32 Start = 0, End = 8, Drain = 0;
};
struct FState
{
	int32 Health[2] = {1000, 1000}, Meter[2] = {0, 0}, Frame = -1;
	bool SimulateOrdinaryMeter = false;
	int32 PercentOnHit[2] = {100, 100}, PercentOnReceive[2] = {100, 100};
	TArray<FRule> Rules;
	TSet<FString> Disabled;
	TArray<int32> Order() const
	{
		TArray<int32> Out;
		for (int32 I = 0; I < Rules.Num(); ++I)
			if (Frame >= Rules[I].Start && Frame < Rules[I].End && !Disabled.Contains(Rules[I].Id))
				Out.Add(I);
		Out.Sort(
		    [&](int32 A, int32 B)
		    {
			    return Rules[A].Priority == Rules[B].Priority
			               ? Rules[A].Id.Compare(Rules[B].Id, ESearchCase::CaseSensitive) < 0
			               : Rules[A].Priority < Rules[B].Priority;
		    });
		return Out;
	}
	struct FChild
	{
		EKind Kind;
		int32 Target, Amount;
		TSet<FString> Ancestors;
	};
	void Event(EKind Kind, int32 Target, int32 Attacker, int32 Amount,
	           TSet<FString> Ancestors = {}, TArray<FChild>* Deferred = nullptr)
	{
		const int32 OriginalAmount = Amount;
		TArray<FChild> Children;
		bool Convert = false;
		for (int32 Index : Order())
		{
			const auto& Rule = Rules[Index];
			if (Rule.Kind != Kind || Ancestors.Contains(Rule.Id) ||
			    (Rule.MatchAmount && Rule.RequiredAmount != OriginalAmount))
				continue;
			Ancestors.Add(Rule.Id);
			Convert |= Rule.Convert && Kind == EKind::Damage;
			for (const auto& Action : Rule.Actions)
			{
				switch (Action.Kind)
				{
				case EAction::Add:
					Amount += Action.Value;
					break;
				case EAction::Scale:
				{
					// C++ truncates negative division; the contract requires mathematical floor.
					const int64 Product = int64(Amount) * Action.Value;
					Amount = int32(Product / Action.Denominator -
					               (Product < 0 && Product % Action.Denominator != 0));
					break;
				}
				case EAction::Cancel:
					return;
				case EAction::DamageChild:
					Children.Add(
					    {EKind::Damage, Action.Attacker ? Attacker : Target, Action.Value});
					break;
				case EAction::MeterChild:
					Children.Add({EKind::Meter, Action.Attacker ? Attacker : Target, Action.Value});
					break;
				}
				if (Kind == EKind::Damage)
					Amount = FMath::Max(0, Amount);
			}
		}
		for (auto& Child : Children)
			Child.Ancestors = Ancestors;
		if (Kind == EKind::Meter)
			Meter[Target] = FMath::Clamp(Meter[Target] + Amount, 0, 100);
		else if (Convert)
			// Commit the debit now; append its authored children after children
			// emitted by damage handlers, preserving global emission order.
			Event(EKind::Meter, Target, Attacker, -Amount, Ancestors, &Children);
		else
		{
			Health[Target] = FMath::Max(0, Health[Target] - Amount);
			if (SimulateOrdinaryMeter)
			{
				// Both gains retain the damage source, even when recipient == source.
				// Their authored children join the queue only after both commits.
				Event(EKind::Meter, Target, Attacker, Amount * PercentOnReceive[Target] / 100, Ancestors, &Children);
				Event(EKind::Meter, Attacker, Attacker, Amount * PercentOnHit[Attacker] / 100, Ancestors, &Children);
			}
		}
		if (Deferred)
			Deferred->Append(Children);
		else
			for (const auto& Child : Children)
				Event(Child.Kind, Child.Target, Attacker, Child.Amount, Child.Ancestors);
	}
	void Step()
	{
		++Frame;
		for (int32 Index : Order())
			if (Rules[Index].Drain)
				for (int32 Team = 0; Team < 2; ++Team)
					Event(EKind::Meter, Team, 1 - Team, -Rules[Index].Drain);
	}
};
} // namespace ModifierOracle
