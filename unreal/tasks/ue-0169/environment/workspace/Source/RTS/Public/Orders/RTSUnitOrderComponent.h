// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AITypes.h"
#include "Combat/RTSCombatTypes.h"
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "Navigation/PathFollowingComponent.h"
#include "Orders/RTSOrderTypes.h"
#include "RTSUnitOrderComponent.generated.h"

DECLARE_MULTICAST_DELEGATE(FRTSOrderChanged);
DECLARE_MULTICAST_DELEGATE_OneParam(FRTSUnitWeaponFired, const FRTSWeaponEvent&);

/** Owns one unit's current order and bounded movement/combat execution. */
UCLASS()
class RTS_API URTSUnitOrderComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSUnitOrderComponent();
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void Issue(const FRTSOrderRequest& Request);
	void Cancel();
	FRTSOrderSnapshot GetSnapshot() const;
	FRTSOrderChanged& OnOrderChanged();
	FRTSUnitWeaponFired& OnWeaponFired();

	void HandleMoveCompleted(FAIRequestID RequestId, const FPathFollowingResult& Result);

private:
	void StopActiveNavigation();
	void StartMove();
	void RetryMove();
	void CompleteMove();
	void FailMove(ERTSOrderFailure Failure);
	void UpdateAttack(float DeltaTime);
	void StartChase();
	void EnterAttacking();
	void ClearAttackTarget();
	bool IsAttackTargetAvailable() const;
	bool IsAttackTargetInRange(const class ARTSCombatUnit& Unit) const;
	FVector GetAttackTargetLocation() const;
	FGenericTeamId GetAttackTargetTeam() const;
	FRTSDamageResult ApplyDamageToAttackTarget(float Damage) const;
	void UpdateReclaim(float DeltaTime);
	void StartReclaimApproach();
	void EnterReclaiming();
	void ClearReclaimTarget(ERTSOrderFailure Failure);
	void TryAcquireIdleTarget();

	FRTSOrderRequest CurrentRequest;
	FRTSOrderSnapshot Snapshot;
	FAIRequestID ActiveMoveRequestId = FAIRequestID::InvalidRequest;
	FTimerHandle RetryTimer;
	int32 RetryCount = 0;
	double NextAllowedAttackTimeSeconds = 0.0;
	int32 NextWeaponEventSequence = 1;
	FRTSOrderChanged OrderChanged;
	FRTSUnitWeaponFired WeaponFired;
};
