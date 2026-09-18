// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/RTSTransientEffect.h"

#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "Units/RTSTeams.h"

namespace
{
void PrepareMesh(UStaticMeshComponent& Mesh)
{
	Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh.SetCanEverAffectNavigation(false);
	Mesh.SetCastShadow(false);
}

FLinearColor TeamColor(const FGenericTeamId TeamId)
{
	if (TeamId == RTSTeams::Player)
	{
		return FLinearColor(0.02f, 0.65f, 1.0f);
	}
	if (TeamId == RTSTeams::Enemy)
	{
		return FLinearColor(1.0f, 0.05f, 0.025f);
	}
	return FLinearColor(0.85f, 0.85f, 0.85f);
}
}

ARTSTransientEffect::ARTSTransientEffect()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	PrimaryMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PrimaryMesh"));
	PrimaryMesh->SetupAttachment(SceneRoot);
	SecondaryMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SecondaryMesh"));
	SecondaryMesh->SetupAttachment(SceneRoot);
	AccentMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AccentMesh"));
	AccentMesh->SetupAttachment(SceneRoot);
	Audio = CreateDefaultSubobject<UAudioComponent>(TEXT("Audio"));
	Audio->SetupAttachment(SceneRoot);
	Audio->bAutoActivate = false;
	Audio->bAutoDestroy = false;
	Audio->bAllowSpatialization = true;

	PrepareMesh(*PrimaryMesh);
	PrepareMesh(*SecondaryMesh);
	PrepareMesh(*AccentMesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Engine/TemplateResources/M_Template_Master.M_Template_Master"));
	CubeMesh = Cube.Object;
	SphereMesh = Sphere.Object;
	CylinderMesh = Cylinder.Object;
	MaterialParent = Material.Object;
}

void ARTSTransientEffect::Configure(
	const FRTSPresentationRequest& InRequest,
	USoundBase* Sound,
	const float InVolumeMultiplier)
{
	// Restore this contractor-owned implementation.
}

const FRTSPresentationRequest& ARTSTransientEffect::GetRequest() const
{
	return Request;
}

void ARTSTransientEffect::ConfigureGeometry()
{
	// Restore this contractor-owned implementation.
}

void ARTSTransientEffect::ApplyColors(
	const FLinearColor& PrimaryColor,
	const FLinearColor& AccentColor)
{
	// Restore this contractor-owned implementation.
}
