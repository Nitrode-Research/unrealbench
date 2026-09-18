// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GenericTeamAgentInterface.h"
#include "RTSStartingZone.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UInstancedStaticMeshComponent;
class USceneComponent;

/** Typed authored boundary for legal command-vehicle starts and later HQ deployment. */
UCLASS()
class RTS_API ARTSStartingZone final : public AActor, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ARTSStartingZone();

	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	void SetRadius(float NewRadius);
	float GetRadius() const;

protected:
	virtual void BeginPlay() override;

private:
	void UpdatePresentation();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> BoundaryMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BoundaryMaterialParent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BoundaryMaterialInstance;

	UPROPERTY(VisibleInstanceOnly, Category = "RTS Starting Zone")
	FGenericTeamId TeamId = FGenericTeamId::NoTeam;

	UPROPERTY(VisibleInstanceOnly, Category = "RTS Starting Zone")
	float Radius = 5000.0f;
};
