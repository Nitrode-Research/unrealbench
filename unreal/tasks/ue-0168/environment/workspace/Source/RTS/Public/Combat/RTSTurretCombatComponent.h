// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Combat/RTSCombatTypes.h"
#include "Combat/RTSTurretCombatTypes.h"
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "RTSTurretCombatComponent.generated.h"

class ARTSCombatUnit;
class ARTSStructure;

DECLARE_MULTICAST_DELEGATE_OneParam(FRTSTurretWeaponFired, const FRTSWeaponEvent&);

/** Actor-local powered turret targeting with deterministic hostile-unit selection. */
UCLASS()
class RTS_API URTSTurretCombatComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSTurretCombatComponent();
	void Initialize(ARTSStructure& InTurret, float InDamage, float InRange, float InCooldownSeconds);
	void NotifyConstructionCompleted();
	FRTSTurretCombatSnapshot GetSnapshot() const;
	FRTSTurretWeaponFired& OnWeaponFired();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void StartCombatTimer();
	void UpdateCombat();
	ARTSCombatUnit* AcquireTarget() const;

	UPROPERTY(Transient)
	TObjectPtr<ARTSStructure> Turret;
	UPROPERTY(Transient)
	TWeakObjectPtr<ARTSCombatUnit> Target;
	FTimerHandle CombatTimer;
	ERTSTurretState State = ERTSTurretState::UnderConstruction;
	float Damage = 0.0f;
	float Range = 0.0f;
	float CooldownSeconds = 0.8f;
	double NextShotSeconds = 0.0;
	int32 ShotsFired = 0;
	FRTSTurretWeaponFired WeaponFired;
};
