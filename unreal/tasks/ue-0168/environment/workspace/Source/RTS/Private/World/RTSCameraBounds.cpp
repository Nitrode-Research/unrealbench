// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/RTSCameraBounds.h"

ARTSCameraBounds::ARTSCameraBounds()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ARTSCameraBounds::SetPlanarHalfExtent(const FVector2D& NewHalfExtent)
{
	PlanarHalfExtent.X = FMath::Max(0.0f, NewHalfExtent.X);
	PlanarHalfExtent.Y = FMath::Max(0.0f, NewHalfExtent.Y);
}

FVector2D ARTSCameraBounds::GetPlanarHalfExtent() const
{
	return PlanarHalfExtent;
}
