// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/RTSTurretCombatComponent.h"

#include "Combat/RTSHealthComponent.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Structures/RTSStructure.h"
#include "TimerManager.h"
#include "Units/RTSCombatUnit.h"

namespace
{
constexpr float CombatUpdateIntervalSeconds = 0.1f;
}

URTSTurretCombatComponent::URTSTurretCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URTSTurretCombatComponent::Initialize(
	ARTSStructure& InTurret,
	const float InDamage,
	const float InRange,
	const float InCooldownSeconds)
{
	Turret = &InTurret;
	Damage = InDamage;
	Range = InRange;
	CooldownSeconds = InCooldownSeconds;
	State = InTurret.IsConstructed() ? ERTSTurretState::Searching : ERTSTurretState::UnderConstruction;
}

void URTSTurretCombatComponent::NotifyConstructionCompleted()
{
	State = ERTSTurretState::Searching;
	StartCombatTimer();
}

FRTSTurretCombatSnapshot URTSTurretCombatComponent::GetSnapshot() const
{
	FRTSTurretCombatSnapshot Snapshot;
	Snapshot.State = State;
	Snapshot.Target = Target;
	Snapshot.TargetStableUnitId = Target.IsValid() ? Target->GetStableUnitId() : INDEX_NONE;
	Snapshot.AttackRange = Range;
	Snapshot.ShotsFired = ShotsFired;
	return Snapshot;
}

FRTSTurretWeaponFired& URTSTurretCombatComponent::OnWeaponFired()
{
	return WeaponFired;
}

void URTSTurretCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CombatTimer);
	}
	Target.Reset();
	Super::EndPlay(EndPlayReason);
}

void URTSTurretCombatComponent::StartCombatTimer()
{
	UWorld* World = GetWorld();
	if (World == nullptr || World->GetTimerManager().IsTimerActive(CombatTimer))
	{
		return;
	}
	World->GetTimerManager().SetTimer(
		CombatTimer,
		this,
		&URTSTurretCombatComponent::UpdateCombat,
		CombatUpdateIntervalSeconds,
		true);
}

void URTSTurretCombatComponent::UpdateCombat()
{
	UWorld* World = GetWorld();
	if (World == nullptr || !IsValid(Turret) || !Turret->IsAlive() || !Turret->IsConstructed())
	{
		return;
	}
	const URTSEconomySubsystem* Economy = World->GetSubsystem<URTSEconomySubsystem>();
	if (Economy == nullptr || Economy->GetSnapshot(Turret->GetGenericTeamId()).IsInBrownout())
	{
		Target.Reset();
		State = ERTSTurretState::Unpowered;
		return;
	}

	if (!Target.IsValid()
		|| !Target->IsAlive()
		|| FVector::DistSquared2D(Target->GetActorLocation(), Turret->GetActorLocation()) > FMath::Square(Range))
	{
		Target = AcquireTarget();
	}
	if (!Target.IsValid())
	{
		State = ERTSTurretState::Searching;
		return;
	}
	State = ERTSTurretState::Firing;
	const double CurrentSeconds = World->GetTimeSeconds();
	if (CurrentSeconds >= NextShotSeconds)
	{
		const FVector TargetLocation = Target->GetActorLocation();
		const int32 TargetStableUnitId = Target->GetStableUnitId();
		const FRTSDamageResult DamageResult = Target->GetHealthComponent()->ApplyDamage(Damage);
		if (DamageResult.bAccepted)
		{
			FRTSWeaponEvent WeaponEvent;
			WeaponEvent.SourceKind = ERTSWeaponSourceKind::DefensiveTurret;
			WeaponEvent.TargetKind = ERTSWeaponTargetKind::Unit;
			WeaponEvent.SourceTeamId = Turret->GetGenericTeamId();
			WeaponEvent.StableSourceId = Turret->GetStableStructureId();
			WeaponEvent.StableTargetId = TargetStableUnitId;
			WeaponEvent.SourceShotSequence = ShotsFired + 1;
			WeaponEvent.SourceWorldLocation = Turret->GetActorLocation();
			WeaponEvent.TargetWorldLocation = TargetLocation;
			WeaponEvent.AppliedDamage = DamageResult.AppliedDamage;
			WeaponEvent.bDestroyedTarget = DamageResult.bKilled;
			WeaponFired.Broadcast(WeaponEvent);
		}
		++ShotsFired;
		NextShotSeconds = CurrentSeconds + CooldownSeconds;
	}
}

ARTSCombatUnit* URTSTurretCombatComponent::AcquireTarget() const
{
	ARTSCombatUnit* BestTarget = nullptr;
	float BestDistanceSquared = FMath::Square(Range);
	for (TActorIterator<ARTSCombatUnit> UnitIterator(GetWorld()); UnitIterator; ++UnitIterator)
	{
		ARTSCombatUnit* Candidate = *UnitIterator;
		if (!Candidate->IsAlive()
			|| Candidate->GetGenericTeamId() == FGenericTeamId::NoTeam
			|| Candidate->GetGenericTeamId() == Turret->GetGenericTeamId())
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared2D(Candidate->GetActorLocation(), Turret->GetActorLocation());
		if (DistanceSquared < BestDistanceSquared
			|| (FMath::IsNearlyEqual(DistanceSquared, BestDistanceSquared)
				&& (BestTarget == nullptr || Candidate->GetStableUnitId() < BestTarget->GetStableUnitId())))
		{
			BestTarget = Candidate;
			BestDistanceSquared = DistanceSquared;
		}
	}
	return BestTarget;
}
