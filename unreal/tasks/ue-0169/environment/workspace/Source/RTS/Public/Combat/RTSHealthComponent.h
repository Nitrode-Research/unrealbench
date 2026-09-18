// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Combat/RTSCombatTypes.h"
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "RTSHealthComponent.generated.h"

class ARTSCombatUnit;

DECLARE_MULTICAST_DELEGATE_OneParam(FRTSHealthChanged, const FRTSHealthSnapshot&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRTSDeath, ARTSCombatUnit*);

/** Owns authoritative health, damage acceptance, and exactly-once death for one unit. */
UCLASS()
class RTS_API URTSHealthComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSHealthComponent();
	virtual void BeginPlay() override;

	void ConfigureMaximumHealth(float NewMaximumHealth, bool bRestoreToMaximum);
	FRTSDamageResult ApplyDamage(float RequestedDamage);
	FRTSHealthSnapshot GetSnapshot() const;
	FRTSHealthChanged& OnHealthChanged();
	FRTSDeath& OnDeath();

private:
	float MaximumHealth = 100.0f;
	float CurrentHealth = 100.0f;
	bool bDead = false;
	FRTSHealthChanged HealthChanged;
	FRTSDeath Death;
};
