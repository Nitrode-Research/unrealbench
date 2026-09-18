// Copyright Epic Games, Inc. All Rights Reserved.

#include "Structures/RTSDeploymentViewSubsystem.h"

#include "Units/RTSCombatUnit.h"

void URTSDeploymentViewSubsystem::BeginHeadquartersPreview(ARTSCombatUnit& CommandVehicle)
{
	// Restore this contractor-owned implementation.
}

void URTSDeploymentViewSubsystem::CancelPreview()
{
	// Restore this contractor-owned implementation.
}

bool URTSDeploymentViewSubsystem::IsPreviewActive() const
{
	return PreviewCommandVehicle.IsValid();
}

ARTSCombatUnit* URTSDeploymentViewSubsystem::GetCommandVehicle() const
{
	return PreviewCommandVehicle.Get();
}

void URTSDeploymentViewSubsystem::SetLastResult(const FRTSHeadquartersDeploymentResult& Result)
{
	// Restore this contractor-owned implementation.
}

TOptional<FRTSHeadquartersDeploymentResult> URTSDeploymentViewSubsystem::GetLastResult() const
{
	return LastResult;
}

void URTSDeploymentViewSubsystem::BeginStructurePreview(
	const ERTSStructureType StructureType,
	const FGenericTeamId TeamId)
{
	// Restore this contractor-owned implementation.
}

void URTSDeploymentViewSubsystem::UpdateStructurePreviewLocation(
	const FVector& DesiredWorldLocation)
{
	// Restore this contractor-owned implementation.
}

bool URTSDeploymentViewSubsystem::IsStructurePreviewActive() const
{
	return PlacementRequest.IsSet();
}

TOptional<FRTSPlacementRequest> URTSDeploymentViewSubsystem::GetPlacementRequest() const
{
	return PlacementRequest;
}

void URTSDeploymentViewSubsystem::SetLastPlacementResult(const FRTSPlacementResult& Result)
{
	// Restore this contractor-owned implementation.
}

TOptional<FRTSPlacementResult> URTSDeploymentViewSubsystem::GetLastPlacementResult() const
{
	return LastPlacementResult;
}
