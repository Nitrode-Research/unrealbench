// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "GameFramework/Character.h"
#include "GenericTeamAgentInterface.h"
#include "RTSCombatUnit.generated.h"

class ARTSCombatUnit;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class URTSHealthComponent;
class URTSReclaimComponent;
class URTSUnitOrderComponent;
DECLARE_MULTICAST_DELEGATE_OneParam(FRTSCombatUnitEligibilityChanged, ARTSCombatUnit*);

/** Native greybox combat unit shared by both Milestone 1 teams. */
UCLASS()
class RTS_API ARTSCombatUnit final : public ACharacter, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ARTSCombatUnit();
	void ConfigureMilestone2Unit(
		ERTSUnitType NewUnitType,
		int32 NewStableUnitId,
		FGenericTeamId NewTeamId,
		int64 AdoptedSupplyTransactionId = 0);

	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FGenericTeamId GetGenericTeamId() const override;

	void SetStableUnitId(int32 NewStableUnitId);
	int32 GetStableUnitId() const;
	bool IsMilestone2Unit() const;
	ERTSUnitType GetUnitType() const;
	float GetAttackDamage() const;
	float GetAttackRange() const;
	float GetAcquisitionRange() const;
	float GetWeaponCooldownSeconds() const;
	FVector GetStatusAnchorWorldLocation() const;
	bool IsAlive() const;
	void MarkDead();
	URTSHealthComponent* GetHealthComponent() const;
	URTSReclaimComponent* GetReclaimComponent() const;
	URTSUnitOrderComponent* GetOrderComponent() const;
	FRTSCombatUnitEligibilityChanged& OnSelectionEligibilityChanged();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class URTSHealthComponent;
	void HandleHealthDepleted();
	void ApplyUnitPresentation();
	void RegisterEconomyAndCommandIdentity();
	void UnregisterSupply();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> GreyboxMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> GreyboxTurret;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FactionMaterialParent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FactionMaterialInstance;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<URTSHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<URTSUnitOrderComponent> OrderComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<URTSReclaimComponent> ReclaimComponent;

	UPROPERTY(VisibleInstanceOnly, Category = "RTS Unit")
	FGenericTeamId TeamId = FGenericTeamId::NoTeam;

	UPROPERTY(VisibleInstanceOnly, Category = "RTS Unit")
	int32 StableUnitId = INDEX_NONE;

	UPROPERTY(VisibleInstanceOnly, Category = "RTS Unit")
	ERTSUnitType UnitType = ERTSUnitType::InfantrySquad;

	UPROPERTY(VisibleInstanceOnly, Category = "RTS Unit")
	bool bMilestone2Unit = false;

	int64 SupplyTransactionId = 0;
	bool bWreckageCreated = false;
	bool bDeathReported = false;

	FRTSCombatUnitEligibilityChanged SelectionEligibilityChanged;

	void UpdateFactionPresentation();
};
