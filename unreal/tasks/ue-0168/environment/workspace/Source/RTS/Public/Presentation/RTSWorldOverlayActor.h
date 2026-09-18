// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Presentation/RTSWorldOverlayTypes.h"
#include "RTSWorldOverlayActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

/** Native render adapter. Overlay meaning remains entirely in the typed descriptor and HLSL. */
UCLASS(NotPlaceable)
class RTS_API ARTSWorldOverlayActor final : public AActor
{
	GENERATED_BODY()

public:
	ARTSWorldOverlayActor();
	bool ApplyDescriptor(const FRTSWorldOverlayDescriptor& Descriptor);

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> OverlayMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> OverlayMaterialParent;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> OverlayMaterial;
};
