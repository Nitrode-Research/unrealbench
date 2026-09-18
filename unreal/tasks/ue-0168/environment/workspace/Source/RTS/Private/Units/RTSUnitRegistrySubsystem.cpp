// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/RTSUnitRegistrySubsystem.h"

#include "Combat/RTSHealthComponent.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Units/RTSCombatUnit.h"

bool URTSUnitRegistrySubsystem::RegisterUnit(ARTSCombatUnit& Unit)
{
	const int32 StableUnitId = Unit.GetStableUnitId();
	if (StableUnitId == INDEX_NONE || Unit.GetWorld() != GetWorld())
	{
		return false;
	}
	for (auto Iterator = UnitsById.CreateIterator(); Iterator; ++Iterator)
	{
		if (Iterator.Value().Get() == &Unit && Iterator.Key() != StableUnitId)
		{
			Iterator.RemoveCurrent();
		}
	}
	ARTSCombatUnit* Existing = UnitsById.FindRef(StableUnitId).Get();
	if (IsValid(Existing) && Existing != &Unit)
	{
		return false;
	}
	UnitsById.Add(StableUnitId, &Unit);
	UnitChanged.Broadcast(MakeSnapshot(Unit));
	return true;
}

void URTSUnitRegistrySubsystem::UnregisterUnit(ARTSCombatUnit& Unit)
{
	const int32 StableUnitId = Unit.GetStableUnitId();
	if (UnitsById.FindRef(StableUnitId).Get() != &Unit)
	{
		return;
	}
	const FRTSUnitSnapshot FinalSnapshot = MakeSnapshot(Unit);
	UnitsById.Remove(StableUnitId);
	UnitChanged.Broadcast(FinalSnapshot);
}

TOptional<FRTSUnitSnapshot> URTSUnitRegistrySubsystem::Find(const int32 StableUnitId) const
{
	const ARTSCombatUnit* Unit = FindActor(StableUnitId);
	return IsValid(Unit)
		? TOptional<FRTSUnitSnapshot>(MakeSnapshot(*Unit))
		: TOptional<FRTSUnitSnapshot>();
}

ARTSCombatUnit* URTSUnitRegistrySubsystem::FindActor(const int32 StableUnitId) const
{
	ARTSCombatUnit* Unit = UnitsById.FindRef(StableUnitId).Get();
	return IsValid(Unit) ? Unit : nullptr;
}

TArray<FRTSUnitSnapshot> URTSUnitRegistrySubsystem::Query(
	const FGenericTeamId TeamId,
	const TOptional<ERTSUnitType> UnitType) const
{
	TArray<FRTSUnitSnapshot> Results;
	for (const TPair<int32, TWeakObjectPtr<ARTSCombatUnit>>& Entry : UnitsById)
	{
		const ARTSCombatUnit* Unit = Entry.Value.Get();
		if (!IsValid(Unit)
			|| Unit->GetGenericTeamId() != TeamId
			|| (UnitType.IsSet() && Unit->GetUnitType() != UnitType.GetValue()))
		{
			continue;
		}
		Results.Add(MakeSnapshot(*Unit));
	}
	Results.Sort([](const FRTSUnitSnapshot& Left, const FRTSUnitSnapshot& Right)
	{
		return Left.StableUnitId < Right.StableUnitId;
	});
	return Results;
}

FRTSUnitRegistryChanged& URTSUnitRegistrySubsystem::OnUnitChanged()
{
	return UnitChanged;
}

FRTSUnitSnapshot URTSUnitRegistrySubsystem::MakeSnapshot(const ARTSCombatUnit& Unit) const
{
	FRTSUnitSnapshot Snapshot;
	Snapshot.StableUnitId = Unit.GetStableUnitId();
	Snapshot.UnitType = Unit.GetUnitType();
	Snapshot.TeamId = Unit.GetGenericTeamId();
	Snapshot.WorldLocation = Unit.GetActorLocation();
	Snapshot.bAlive = Unit.IsAlive();
	Snapshot.bAvailableForOrders = Snapshot.bAlive && !Unit.IsActorBeingDestroyed();
	if (const URTSHealthComponent* Health = Unit.GetHealthComponent())
	{
		const FRTSHealthSnapshot HealthSnapshot = Health->GetSnapshot();
		Snapshot.CurrentHealth = HealthSnapshot.CurrentHealth;
		Snapshot.MaximumHealth = HealthSnapshot.MaximumHealth;
	}
	if (const URTSUnitOrderComponent* Orders = Unit.GetOrderComponent())
	{
		const FRTSOrderSnapshot OrderSnapshot = Orders->GetSnapshot();
		Snapshot.OrderKind = OrderSnapshot.Kind;
		Snapshot.OrderPhase = OrderSnapshot.Phase;
	}
	return Snapshot;
}
