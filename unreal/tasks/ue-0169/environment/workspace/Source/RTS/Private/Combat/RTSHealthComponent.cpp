// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/RTSHealthComponent.h"

#include "Units/RTSCombatUnit.h"

URTSHealthComponent::URTSHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URTSHealthComponent::BeginPlay()
{
	// Restore this contractor-owned implementation.
	Super::BeginPlay();
}

void URTSHealthComponent::ConfigureMaximumHealth(
	const float NewMaximumHealth,
	const bool bRestoreToMaximum)
{
	// Restore this contractor-owned implementation.
}

FRTSDamageResult URTSHealthComponent::ApplyDamage(const float RequestedDamage)
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSHealthSnapshot URTSHealthComponent::GetSnapshot() const
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSHealthChanged& URTSHealthComponent::OnHealthChanged()
{
	return HealthChanged;
}

FRTSDeath& URTSHealthComponent::OnDeath()
{
	return Death;
}
