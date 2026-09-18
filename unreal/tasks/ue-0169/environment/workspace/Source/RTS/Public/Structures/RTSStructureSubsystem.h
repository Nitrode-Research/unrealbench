// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Structures/RTSStructureTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/RTSWorldTypes.h"
#include "RTSStructureSubsystem.generated.h"

class AActor;
class ARTSCombatUnit;
class ARTSMaterialDeposit;
class ARTSStructure;

DECLARE_MULTICAST_DELEGATE_OneParam(FRTSStructureChanged, const FRTSStructureSnapshot&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRTSConstructionCompleted, const FRTSStructureSnapshot&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRTSStructureDestroyed, const FRTSStructureSnapshot&);

/** Owns authoritative placement evaluation and the atomic Command Vehicle to HQ transition. */
UCLASS()
class RTS_API URTSStructureSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	FRTSHeadquartersDeploymentPreview EvaluateHeadquartersDeployment(
		const ARTSCombatUnit& CommandVehicle) const;
	FRTSHeadquartersDeploymentPreview EvaluateHeadquartersDeploymentAt(
		const ARTSCombatUnit& CommandVehicle,
		const FVector& ProposedWorldLocation) const;
	FRTSHeadquartersDeploymentResult TryDeployHeadquarters(ARTSCombatUnit& CommandVehicle);
	FRTSPlacementPreview EvaluatePlacement(const FRTSPlacementRequest& Request) const;
	FRTSPlacementResult TryPlace(const FRTSPlacementRequest& Request);
	TOptional<FRTSStructureSnapshot> Find(int32 StableStructureId) const;
	ARTSStructure* FindActor(int32 StableStructureId) const;
	TArray<FRTSStructureSnapshot> Query(
		FGenericTeamId TeamId,
		TOptional<ERTSStructureType> StructureType = {}) const;
	TOptional<FRTSStartingZoneSnapshot> FindStartingZone(FGenericTeamId TeamId) const;
	TArray<FRTSMaterialDepositSnapshot> QueryMaterialDeposits() const;
	TOptional<FVector> FindFactorySpawnLocation(const ARTSStructure& Factory) const;
	int32 AllocateProducedUnitId();
	void NotifyConstructionCompleted(ARTSStructure& Structure);
	void NotifyStructureDestroyed(ARTSStructure& Structure);
	void UnregisterStructure(ARTSStructure& Structure);
	FRTSStructureChanged& OnStructureChanged();
	FRTSConstructionCompleted& OnConstructionCompleted();
	FRTSStructureDestroyed& OnStructureDestroyed();

private:
	struct FRegisteredStructure
	{
		TWeakObjectPtr<ARTSStructure> Actor;
		TArray<FIntPoint> OccupiedCells;
		int32 StableDepositId = INDEX_NONE;
	};

	bool RegisterStructure(ARTSStructure& Structure, int32 StableDepositId);
	TArray<FIntPoint> BuildFootprintCells(
		const FVector& SnappedLocation,
		const FIntPoint& FootprintCells) const;
	bool IsFootprintInsideBuildArea(
		FGenericTeamId TeamId,
		TConstArrayView<FIntPoint> FootprintCells) const;
	bool HasBlockingOverlap(
		const FVector& GroundLocation,
		const FIntPoint& FootprintCells,
		const AActor* IgnoredActor) const;
	ARTSMaterialDeposit* FindDepositForPlacement(
		const FVector& DesiredLocation,
		const FIntPoint& FootprintCells) const;
	bool HasFactoryExit(
		const FVector& GroundLocation,
		const FIntPoint& FootprintCells,
		TConstArrayView<FIntPoint> AdditionalOccupiedCells,
		const AActor* IgnoredFactory) const;
	TOptional<FVector> FindFactoryExit(
		const FVector& GroundLocation,
		const FIntPoint& FootprintCells,
		TConstArrayView<FIntPoint> AdditionalOccupiedCells,
		const AActor* IgnoredFactory) const;
	FRTSStructureSnapshot MakeSnapshot(const ARTSStructure& Structure) const;
	static bool IsSupportedTeam(FGenericTeamId TeamId);

	int32 NextStableStructureId = 1;
	int32 NextProducedUnitId = INDEX_NONE;
	TMap<int32, FRegisteredStructure> StructuresById;
	TMap<FIntPoint, int32> OccupancyByCell;
	TMap<int32, int32> StructureByDepositId;
	FRTSStructureChanged StructureChanged;
	FRTSConstructionCompleted ConstructionCompleted;
	FRTSStructureDestroyed StructureDestroyed;
};
