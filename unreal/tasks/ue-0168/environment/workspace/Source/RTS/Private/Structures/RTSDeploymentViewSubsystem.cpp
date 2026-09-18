// Copyright Epic Games, Inc. All Rights Reserved.

#include "Structures/RTSDeploymentViewSubsystem.h"

#include "Units/RTSCombatUnit.h"

void URTSDeploymentViewSubsystem::BeginHeadquartersPreview(ARTSCombatUnit& CommandVehicle)
{
	PreviewCommandVehicle = &CommandVehicle;
	LastResult.Reset();
	PlacementRequest.Reset();
}

void URTSDeploymentViewSubsystem::CancelPreview()
{
	PreviewCommandVehicle.Reset();
	PlacementRequest.Reset();
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
	LastResult = Result;
	PreviewCommandVehicle.Reset();
}

TOptional<FRTSHeadquartersDeploymentResult> URTSDeploymentViewSubsystem::GetLastResult() const
{
	return LastResult;
}

void URTSDeploymentViewSubsystem::BeginStructurePreview(
	const ERTSStructureType StructureType,
	const FGenericTeamId TeamId)
{
	PreviewCommandVehicle.Reset();
	FRTSPlacementRequest Request;
	Request.StructureType = StructureType;
	Request.TeamId = TeamId;
	PlacementRequest = Request;
	LastPlacementResult.Reset();
}

void URTSDeploymentViewSubsystem::UpdateStructurePreviewLocation(
	const FVector& DesiredWorldLocation)
{
	if (PlacementRequest.IsSet())
	{
		PlacementRequest->DesiredWorldLocation = DesiredWorldLocation;
	}
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
	LastPlacementResult = Result;
	PlacementRequest.Reset();
}

TOptional<FRTSPlacementResult> URTSDeploymentViewSubsystem::GetLastPlacementResult() const
{
	return LastPlacementResult;
}
