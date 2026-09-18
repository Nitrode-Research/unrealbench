// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTSCameraBounds.generated.h"

/** Map-owned planar camera constraint; gameplay maps provide exactly one. */
UCLASS()
class RTS_API ARTSCameraBounds final : public AActor
{
	GENERATED_BODY()

public:
	ARTSCameraBounds();

	void SetPlanarHalfExtent(const FVector2D& NewHalfExtent);
	FVector2D GetPlanarHalfExtent() const;

private:
	UPROPERTY(VisibleInstanceOnly, Category = "RTS Camera")
	FVector2D PlanarHalfExtent = FVector2D(4000.0f, 4000.0f);
};
