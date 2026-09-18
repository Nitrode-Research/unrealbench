// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/RTSWorldOverlayActor.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ARTSWorldOverlayActor::ARTSWorldOverlayActor()
{
	PrimaryActorTick.bCanEverTick = false;

	OverlayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OverlayMesh"));
	SetRootComponent(OverlayMesh);
	OverlayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OverlayMesh->SetCanEverAffectNavigation(false);
	OverlayMesh->SetCastShadow(false);
	OverlayMesh->SetReceivesDecals(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		OverlayMesh->SetStaticMesh(PlaneMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> OverlayMaterialFinder(
		TEXT("/Game/RTS/Materials/M_RTSOverlayPrimitive.M_RTSOverlayPrimitive"));
	if (OverlayMaterialFinder.Succeeded())
	{
		OverlayMaterialParent = OverlayMaterialFinder.Object;
	}
}

bool ARTSWorldOverlayActor::ApplyDescriptor(const FRTSWorldOverlayDescriptor& Descriptor)
{
	if (OverlayMaterial == nullptr && OverlayMaterialParent != nullptr)
	{
		OverlayMaterial = UMaterialInstanceDynamic::Create(OverlayMaterialParent, this);
		OverlayMesh->SetMaterial(0, OverlayMaterial);
	}
	if (OverlayMaterial == nullptr)
	{
		return false;
	}

	const FVector2D FullSize = Descriptor.Mode == ERTSWorldOverlayMode::PlacementFootprint
		? Descriptor.HalfExtent * 2.0f
		: FVector2D(Descriptor.Radius * 2.0f);
	if (FullSize.X <= 0.0f || FullSize.Y <= 0.0f)
	{
		return false;
	}

	FTransform RenderTransform = Descriptor.WorldTransform;
	FVector Location = RenderTransform.GetLocation();
	Location.Z += 18.0f;
	RenderTransform.SetLocation(Location);
	RenderTransform.SetScale3D(FVector(FullSize.X / 100.0f, FullSize.Y / 100.0f, 1.0f));
	SetActorTransform(RenderTransform);
	OverlayMesh->SetTranslucentSortPriority(static_cast<int32>(Descriptor.Mode) + 1);
	OverlayMaterial->SetScalarParameterValue(TEXT("OverlayMode"), static_cast<float>(Descriptor.Mode));
	OverlayMaterial->SetScalarParameterValue(TEXT("OverlayValidity"), static_cast<float>(Descriptor.Validity));
	OverlayMaterial->SetScalarParameterValue(TEXT("OverlayFaction"), Descriptor.TeamId.GetId());
	OverlayMaterial->SetScalarParameterValue(TEXT("AnimationPhase"), Descriptor.AnimationPhaseSeconds);
	return true;
}
