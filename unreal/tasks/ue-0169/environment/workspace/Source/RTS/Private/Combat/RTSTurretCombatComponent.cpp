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
	// Restore this contractor-owned implementation.
}

void URTSTurretCombatComponent::NotifyConstructionCompleted()
{
	// Restore this contractor-owned implementation.
}

FRTSTurretCombatSnapshot URTSTurretCombatComponent::GetSnapshot() const
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSTurretWeaponFired& URTSTurretCombatComponent::OnWeaponFired()
{
	return WeaponFired;
}

void URTSTurretCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Restore this contractor-owned implementation.
	Super::EndPlay(EndPlayReason);
}

void URTSTurretCombatComponent::StartCombatTimer()
{
	// Restore this contractor-owned implementation.
}

void URTSTurretCombatComponent::UpdateCombat()
{
	// Restore this contractor-owned implementation.
}

ARTSCombatUnit* URTSTurretCombatComponent::AcquireTarget() const
{
	// Restore this contractor-owned implementation.
	return {};
}
