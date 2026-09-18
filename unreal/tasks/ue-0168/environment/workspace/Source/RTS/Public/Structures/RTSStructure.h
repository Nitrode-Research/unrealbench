// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Combat/RTSCombatTypes.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GenericTeamAgentInterface.h"
#include "RTSStructure.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNavModifierComponent;
class USceneComponent;
class UStaticMeshComponent;
class URTSHealthComponent;
class URTSProductionComponent;
class URTSTurretCombatComponent;

/** Native structure identity and actor-local presentation. Construction behavior arrives later. */
UCLASS()
class RTS_API ARTSStructure final : public AActor, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ARTSStructure();
	void Configure(
		ERTSStructureType NewType,
		int32 NewStableStructureId,
		FGenericTeamId NewTeamId,
		const FRTSStructureDefinition& Definition,
		float PlacementCellSize,
		int32 NewStableDepositId = INDEX_NONE);

	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	ERTSStructureType GetStructureType() const;
	int32 GetStableStructureId() const;
	FIntPoint GetFootprintCells() const;
	float GetBuildAreaRadius() const;
	int32 GetPowerGeneration() const;
	int32 GetPowerDemand() const;
	int32 GetSupplyCapacity() const;
	int32 GetStableDepositId() const;
	bool IsAlive() const;
	bool IsConstructed() const;
	float GetConstructionProgress() const;
	FRTSDamageResult ApplyDamage(float RequestedDamage);
	URTSHealthComponent* GetHealthComponent() const;
	URTSProductionComponent* GetProductionComponent() const;
	URTSTurretCombatComponent* GetTurretCombatComponent() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void CompleteConstruction();
	void ApplyMaterialIncome();
	void ReleaseEconomyContribution();
	void HandleHealthDepleted();
	void UpdatePresentation();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> GreyboxMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> FactionAccentMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNavModifierComponent> NavigationModifier;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<URTSHealthComponent> HealthComponent;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<URTSProductionComponent> ProductionComponent;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<URTSTurretCombatComponent> TurretCombatComponent;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FactionMaterialParent;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FactionMaterialInstance;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StructureTypeMaterialInstance;

	ERTSStructureType StructureType = ERTSStructureType::Headquarters;
	FGenericTeamId TeamId = FGenericTeamId::NoTeam;
	int32 StableStructureId = INDEX_NONE;
	FIntPoint FootprintCells = FIntPoint::ZeroValue;
	float BuildAreaRadius = 0.0f;
	int32 PowerGeneration = 0;
	int32 PowerDemand = 0;
	int32 SupplyCapacity = 0;
	int32 MaterialIncomePerInterval = 0;
	int32 StableDepositId = INDEX_NONE;
	float MaximumHealth = 500.0f;
	float ConstructionSeconds = 0.0f;
	double ConstructionStartedAtSeconds = 0.0;
	int64 EconomyContributionTransactionId = 0;
	bool bConstructed = false;
	bool bEconomyContributionApplied = false;
	bool bHealthDepletionHandled = false;
	bool bWreckageCreated = false;
	FTimerHandle ConstructionTimer;
	FTimerHandle MaterialIncomeTimer;
};
