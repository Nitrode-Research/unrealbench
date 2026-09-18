// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Reclaim/RTSReclaimTypes.h"
#include "RTSWreckage.generated.h"

class ARTSCombatUnit;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UStaticMeshComponent;

/** Immutable death receipt plus the one-owner lease that guards an exactly-once reclaim payout. */
UCLASS()
class RTS_API ARTSWreckage final : public AActor
{
	GENERATED_BODY()

public:
	ARTSWreckage();
	void ConfigureFromUnit(
		int32 NewStableWreckageId,
		ERTSUnitType NewSourceUnitType,
		FGenericTeamId NewOriginalTeamId,
		int32 NewReclaimMaterialValue);
	void ConfigureFromStructure(
		int32 NewStableWreckageId,
		ERTSStructureType NewSourceStructureType,
		FGenericTeamId NewOriginalTeamId,
		int32 NewReclaimMaterialValue,
		const FIntPoint& SourceFootprintCells,
		float PlacementCellSize);

	FRTSWreckageSnapshot GetSnapshot() const;
	FVector GetStatusAnchorWorldLocation() const;
	bool IsAvailable() const;
	FRTSReclaimResult TryAcquireLease(ARTSCombatUnit& Reclaimer);
	void ReleaseLease(ARTSCombatUnit& Reclaimer);
	FRTSReclaimResult TryCompleteReclaim(ARTSCombatUnit& Reclaimer);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ApplyPresentation(const FVector& WorldSize);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> WreckageMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> MaterialParent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MaterialInstance;

	int32 StableWreckageId = INDEX_NONE;
	ERTSWreckageSourceKind SourceKind = ERTSWreckageSourceKind::Unit;
	ERTSUnitType SourceUnitType = ERTSUnitType::InfantrySquad;
	ERTSStructureType SourceStructureType = ERTSStructureType::Headquarters;
	FGenericTeamId OriginalTeamId = FGenericTeamId::NoTeam;
	int32 ReclaimMaterialValue = 0;
	bool bConsumed = false;
	TWeakObjectPtr<ARTSCombatUnit> LeaseHolder;
};
