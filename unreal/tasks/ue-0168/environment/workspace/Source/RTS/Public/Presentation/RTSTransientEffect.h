// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Presentation/RTSPresentationTypes.h"
#include "RTSTransientEffect.generated.h"

class UAudioComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;

/** A strictly lifetime-bound visual and audio projection of one presentation request. */
UCLASS()
class RTS_API ARTSTransientEffect final : public AActor
{
	GENERATED_BODY()

public:
	ARTSTransientEffect();
	void Configure(const FRTSPresentationRequest& InRequest, USoundBase* Sound, float VolumeMultiplier);
	const FRTSPresentationRequest& GetRequest() const;

private:
	void ConfigureGeometry();
	void ApplyColors(const FLinearColor& PrimaryColor, const FLinearColor& AccentColor);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> PrimaryMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> SecondaryMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> AccentMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UAudioComponent> Audio;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> MaterialParent;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PrimaryMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AccentMaterial;

	FRTSPresentationRequest Request;
};
