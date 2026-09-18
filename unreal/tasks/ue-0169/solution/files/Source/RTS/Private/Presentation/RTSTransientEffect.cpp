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
	Request = InRequest;
	const bool bAnchorsAtTarget = Request.Kind == ERTSPresentationEventKind::MoveAcknowledged
		|| Request.Kind == ERTSPresentationEventKind::AttackAcknowledged;
	SetActorLocation(bAnchorsAtTarget ? Request.TargetWorldLocation : Request.SourceWorldLocation);
	ConfigureGeometry();

	if (Sound != nullptr)
	{
		Audio->SetSound(Sound);
		Audio->SetVolumeMultiplier(FMath::Clamp(InVolumeMultiplier, 0.0f, 1.0f));
		Audio->Play();
	}
	SetLifeSpan(FMath::Max(0.05f, Request.LifetimeSeconds));
}

const FRTSPresentationRequest& ARTSTransientEffect::GetRequest() const
{
	return Request;
}

void ARTSTransientEffect::ConfigureGeometry()
{
	PrimaryMesh->SetVisibility(true);
	SecondaryMesh->SetVisibility(true);
	AccentMesh->SetVisibility(true);
	PrimaryMesh->SetStaticMesh(CubeMesh);
	SecondaryMesh->SetStaticMesh(SphereMesh);
	AccentMesh->SetStaticMesh(CylinderMesh);
	PrimaryMesh->SetRelativeTransform(FTransform::Identity);
	SecondaryMesh->SetRelativeTransform(FTransform::Identity);
	AccentMesh->SetRelativeTransform(FTransform::Identity);

	FLinearColor PrimaryColor = TeamColor(Request.TeamId);
	FLinearColor AccentColor = FLinearColor(0.8f, 0.95f, 1.0f);
	switch (Request.Kind)
	{
	case ERTSPresentationEventKind::SelectionAcknowledged:
		PrimaryMesh->SetStaticMesh(CylinderMesh);
		PrimaryMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 14.0f));
		PrimaryMesh->SetRelativeScale3D(FVector(1.35f, 1.35f, 0.035f));
		SecondaryMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 52.0f));
		SecondaryMesh->SetRelativeScale3D(FVector(0.18f));
		AccentMesh->SetVisibility(false);
		break;
	case ERTSPresentationEventKind::MoveAcknowledged:
		PrimaryColor = FLinearColor(1.0f, 0.72f, 0.05f);
		AccentColor = FLinearColor(1.0f, 0.92f, 0.45f);
		PrimaryMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 18.0f));
		PrimaryMesh->SetRelativeScale3D(FVector(2.2f, 0.12f, 0.06f));
		SecondaryMesh->SetStaticMesh(CubeMesh);
		SecondaryMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 18.0f));
		SecondaryMesh->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
		SecondaryMesh->SetRelativeScale3D(FVector(2.2f, 0.12f, 0.06f));
		AccentMesh->SetVisibility(false);
		break;
	case ERTSPresentationEventKind::AttackAcknowledged:
		PrimaryColor = FLinearColor(1.0f, 0.08f, 0.025f);
		AccentColor = FLinearColor(1.0f, 0.48f, 0.08f);
		PrimaryMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 22.0f));
		PrimaryMesh->SetRelativeRotation(FRotator(0.0f, 45.0f, 0.0f));
		PrimaryMesh->SetRelativeScale3D(FVector(2.3f, 0.13f, 0.07f));
		SecondaryMesh->SetStaticMesh(CubeMesh);
		SecondaryMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 22.0f));
		SecondaryMesh->SetRelativeRotation(FRotator(0.0f, -45.0f, 0.0f));
		SecondaryMesh->SetRelativeScale3D(FVector(2.3f, 0.13f, 0.07f));
		AccentMesh->SetVisibility(false);
		break;
	case ERTSPresentationEventKind::WeaponImpact:
	{
		const FVector Beam = Request.TargetWorldLocation - Request.SourceWorldLocation;
		const float BeamLength = FMath::Max(10.0f, Beam.Size());
		PrimaryMesh->SetRelativeLocation(Beam * 0.5f + FVector(0.0f, 0.0f, 70.0f));
		PrimaryMesh->SetRelativeRotation(Beam.Rotation());
		PrimaryMesh->SetRelativeScale3D(FVector(BeamLength / 100.0f, 0.055f, 0.055f));
		SecondaryMesh->SetRelativeLocation(Beam + FVector(0.0f, 0.0f, 65.0f));
		SecondaryMesh->SetRelativeScale3D(FVector(0.34f));
		AccentMesh->SetStaticMesh(SphereMesh);
		AccentMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 72.0f));
		AccentMesh->SetRelativeScale3D(FVector(0.2f));
		AccentColor = FLinearColor(1.0f, 0.62f, 0.08f);
		break;
	}
	case ERTSPresentationEventKind::Destruction:
		PrimaryColor = FLinearColor(1.0f, 0.22f, 0.02f);
		AccentColor = FLinearColor(1.0f, 0.72f, 0.08f);
		PrimaryMesh->SetStaticMesh(SphereMesh);
		PrimaryMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 105.0f));
		PrimaryMesh->SetRelativeScale3D(FVector(1.5f));
		SecondaryMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 105.0f));
		SecondaryMesh->SetRelativeScale3D(FVector(0.8f));
		AccentMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 18.0f));
		AccentMesh->SetRelativeScale3D(FVector(2.3f, 2.3f, 0.04f));
		break;
	case ERTSPresentationEventKind::ConstructionCompleted:
	case ERTSPresentationEventKind::ProductionCompleted:
	case ERTSPresentationEventKind::ReclaimCompleted:
		PrimaryColor = Request.Kind == ERTSPresentationEventKind::ReclaimCompleted
			? FLinearColor(0.1f, 1.0f, 0.48f)
			: Request.Kind == ERTSPresentationEventKind::ProductionCompleted
				? FLinearColor(0.08f, 0.72f, 1.0f)
				: FLinearColor(0.25f, 1.0f, 0.82f);
		AccentColor = FLinearColor(0.88f, 1.0f, 0.92f);
		PrimaryMesh->SetStaticMesh(CylinderMesh);
		PrimaryMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 16.0f));
		PrimaryMesh->SetRelativeScale3D(FVector(1.7f, 1.7f, 0.04f));
		SecondaryMesh->SetStaticMesh(CubeMesh);
		SecondaryMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 170.0f));
		SecondaryMesh->SetRelativeScale3D(FVector(0.12f, 0.12f, 3.0f));
		AccentMesh->SetStaticMesh(SphereMesh);
		AccentMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 320.0f));
		AccentMesh->SetRelativeScale3D(FVector(0.38f));
		break;
	case ERTSPresentationEventKind::Victory:
	case ERTSPresentationEventKind::Defeat:
		PrimaryColor = Request.Kind == ERTSPresentationEventKind::Victory
			? FLinearColor(0.2f, 1.0f, 0.3f)
			: FLinearColor(1.0f, 0.04f, 0.025f);
		AccentColor = Request.Kind == ERTSPresentationEventKind::Victory
			? FLinearColor(0.9f, 1.0f, 0.2f)
			: FLinearColor(0.45f, 0.02f, 0.01f);
		PrimaryMesh->SetStaticMesh(CylinderMesh);
		PrimaryMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 24.0f));
		PrimaryMesh->SetRelativeScale3D(FVector(5.0f, 5.0f, 0.05f));
		SecondaryMesh->SetRelativeLocation(FVector(-170.0f, 0.0f, 300.0f));
		SecondaryMesh->SetRelativeScale3D(FVector(0.22f, 0.22f, 6.0f));
		AccentMesh->SetStaticMesh(CubeMesh);
		AccentMesh->SetRelativeLocation(FVector(170.0f, 0.0f, 300.0f));
		AccentMesh->SetRelativeScale3D(FVector(0.22f, 0.22f, 6.0f));
		break;
	}
	ApplyColors(PrimaryColor, AccentColor);
}

void ARTSTransientEffect::ApplyColors(
	const FLinearColor& PrimaryColor,
	const FLinearColor& AccentColor)
{
	if (PrimaryMaterial == nullptr && MaterialParent != nullptr)
	{
		PrimaryMaterial = UMaterialInstanceDynamic::Create(MaterialParent, this);
	}
	if (AccentMaterial == nullptr && MaterialParent != nullptr)
	{
		AccentMaterial = UMaterialInstanceDynamic::Create(MaterialParent, this);
	}
	if (PrimaryMaterial != nullptr)
	{
		PrimaryMaterial->SetVectorParameterValue(TEXT("DiffuseColor"), PrimaryColor);
		PrimaryMesh->SetMaterial(0, PrimaryMaterial);
	}
	if (AccentMaterial != nullptr)
	{
		AccentMaterial->SetVectorParameterValue(TEXT("DiffuseColor"), AccentColor);
		SecondaryMesh->SetMaterial(0, AccentMaterial);
		AccentMesh->SetMaterial(0, AccentMaterial);
	}
}
