// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/RTSHealthComponent.h"

#include "Units/RTSCombatUnit.h"

URTSHealthComponent::URTSHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URTSHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	ConfigureMaximumHealth(FRTSCombatTuning::Load().MaximumHealth, true);
}

void URTSHealthComponent::ConfigureMaximumHealth(
	const float NewMaximumHealth,
	const bool bRestoreToMaximum)
{
	if (bDead)
	{
		return;
	}

	MaximumHealth = FMath::Max(1.0f, NewMaximumHealth);
	CurrentHealth = bRestoreToMaximum
		? MaximumHealth
		: FMath::Clamp(CurrentHealth, 0.0f, MaximumHealth);
}

FRTSDamageResult URTSHealthComponent::ApplyDamage(const float RequestedDamage)
{
	FRTSDamageResult Result;
	Result.RemainingHealth = CurrentHealth;
	if (bDead || RequestedDamage <= 0.0f)
	{
		return Result;
	}

	Result.bAccepted = true;
	Result.AppliedDamage = FMath::Min(RequestedDamage, CurrentHealth);
	CurrentHealth = FMath::Max(0.0f, CurrentHealth - Result.AppliedDamage);
	Result.RemainingHealth = CurrentHealth;
	Result.bKilled = CurrentHealth <= 0.0f;
	if (Result.bKilled)
	{
		bDead = true;
	}

	HealthChanged.Broadcast(GetSnapshot());
	if (Result.bKilled)
	{
		ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner());
		if (Unit != nullptr)
		{
			Unit->HandleHealthDepleted();
		}
		Death.Broadcast(Unit);
	}
	return Result;
}

FRTSHealthSnapshot URTSHealthComponent::GetSnapshot() const
{
	FRTSHealthSnapshot Snapshot;
	Snapshot.MaximumHealth = MaximumHealth;
	Snapshot.CurrentHealth = CurrentHealth;
	Snapshot.bDead = bDead;
	return Snapshot;
}

FRTSHealthChanged& URTSHealthComponent::OnHealthChanged()
{
	return HealthChanged;
}

FRTSDeath& URTSHealthComponent::OnDeath()
{
	return Death;
}
