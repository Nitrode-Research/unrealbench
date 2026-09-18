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
	// Restore this contractor-owned implementation.
	return {};
}
