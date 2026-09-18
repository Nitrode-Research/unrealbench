// Copyright Epic Games, Inc. All Rights Reserved.

#include "Structures/RTSStructureSubsystem.h"

#include "CollisionQueryParams.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "Match/RTSMatchSubsystem.h"
#include "NavigationSystem.h"
#include "Structures/RTSStructure.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"
#include "World/RTSMaterialDeposit.h"
#include "World/RTSStartingZone.h"

FRTSHeadquartersDeploymentPreview URTSStructureSubsystem::EvaluateHeadquartersDeployment(
	const ARTSCombatUnit& CommandVehicle) const
{
	return EvaluateHeadquartersDeploymentAt(CommandVehicle, CommandVehicle.GetActorLocation());
}

FRTSHeadquartersDeploymentPreview URTSStructureSubsystem::EvaluateHeadquartersDeploymentAt(
	const ARTSCombatUnit& CommandVehicle,
	const FVector& ProposedWorldLocation) const
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSHeadquartersDeploymentResult URTSStructureSubsystem::TryDeployHeadquarters(
	ARTSCombatUnit& CommandVehicle)
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSPlacementPreview URTSStructureSubsystem::EvaluatePlacement(
	const FRTSPlacementRequest& Request) const
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSPlacementResult URTSStructureSubsystem::TryPlace(const FRTSPlacementRequest& Request)
{
	// Restore this contractor-owned implementation.
	return {};
}

TOptional<FRTSStructureSnapshot> URTSStructureSubsystem::Find(const int32 StableStructureId) const
{
	// Restore this contractor-owned implementation.
	return {};
}

ARTSStructure* URTSStructureSubsystem::FindActor(const int32 StableStructureId) const
{
	// Restore this contractor-owned implementation.
	return {};
}

TArray<FRTSStructureSnapshot> URTSStructureSubsystem::Query(
	const FGenericTeamId TeamId,
	const TOptional<ERTSStructureType> StructureType) const
{
	// Restore this contractor-owned implementation.
	return {};
}

TOptional<FRTSStartingZoneSnapshot> URTSStructureSubsystem::FindStartingZone(
	const FGenericTeamId TeamId) const
{
	// Restore this contractor-owned implementation.
	return {};
}

TArray<FRTSMaterialDepositSnapshot> URTSStructureSubsystem::QueryMaterialDeposits() const
{
	// Restore this contractor-owned implementation.
	return {};
}

void URTSStructureSubsystem::NotifyConstructionCompleted(ARTSStructure& Structure)
{
	// Restore this contractor-owned implementation.
}

void URTSStructureSubsystem::NotifyStructureDestroyed(ARTSStructure& Structure)
{
	// Restore this contractor-owned implementation.
}

void URTSStructureSubsystem::UnregisterStructure(ARTSStructure& Structure)
{
	// Restore this contractor-owned implementation.
}

FRTSStructureChanged& URTSStructureSubsystem::OnStructureChanged()
{
	return StructureChanged;
}

FRTSConstructionCompleted& URTSStructureSubsystem::OnConstructionCompleted()
{
	return ConstructionCompleted;
}

FRTSStructureDestroyed& URTSStructureSubsystem::OnStructureDestroyed()
{
	return StructureDestroyed;
}

bool URTSStructureSubsystem::RegisterStructure(
	ARTSStructure& Structure,
	const int32 StableDepositId)
{
	// Restore this contractor-owned implementation.
	return {};
}

TArray<FIntPoint> URTSStructureSubsystem::BuildFootprintCells(
	const FVector& SnappedLocation,
	const FIntPoint& FootprintCells) const
{
	// Restore this contractor-owned implementation.
	return {};
}

bool URTSStructureSubsystem::IsFootprintInsideBuildArea(
	const FGenericTeamId TeamId,
	const TConstArrayView<FIntPoint> FootprintCells) const
{
	// Restore this contractor-owned implementation.
	return {};
}

bool URTSStructureSubsystem::HasBlockingOverlap(
	const FVector& GroundLocation,
	const FIntPoint& FootprintCells,
	const AActor* IgnoredActor) const
{
	// Restore this contractor-owned implementation.
	return {};
}

ARTSMaterialDeposit* URTSStructureSubsystem::FindDepositForPlacement(
	const FVector& DesiredLocation,
	const FIntPoint& FootprintCells) const
{
	// Restore this contractor-owned implementation.
	return {};
}

bool URTSStructureSubsystem::HasFactoryExit(
	const FVector& GroundLocation,
	const FIntPoint& FootprintCells,
	const TConstArrayView<FIntPoint> AdditionalOccupiedCells,
	const AActor* IgnoredFactory) const
{
	return FindFactoryExit(GroundLocation, FootprintCells, AdditionalOccupiedCells, IgnoredFactory).IsSet();
}

TOptional<FVector> URTSStructureSubsystem::FindFactorySpawnLocation(const ARTSStructure& Factory) const
{
	// Restore this contractor-owned implementation.
	return {};
}

int32 URTSStructureSubsystem::AllocateProducedUnitId()
{
	// Restore this contractor-owned implementation.
	return {};
}

TOptional<FVector> URTSStructureSubsystem::FindFactoryExit(
	const FVector& GroundLocation,
	const FIntPoint& FootprintCells,
	const TConstArrayView<FIntPoint> AdditionalOccupiedCells,
	const AActor* IgnoredFactory) const
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSStructureSnapshot URTSStructureSubsystem::MakeSnapshot(const ARTSStructure& Structure) const
{
	// Restore this contractor-owned implementation.
	return {};
}

bool URTSStructureSubsystem::IsSupportedTeam(const FGenericTeamId TeamId)
{
	return TeamId == RTSTeams::Player || TeamId == RTSTeams::Enemy;
}
