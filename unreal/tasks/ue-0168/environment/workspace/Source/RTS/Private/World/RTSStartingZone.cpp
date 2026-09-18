// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/RTSStartingZone.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Units/RTSTeams.h"

ARTSStartingZone::ARTSStartingZone()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	SetRootComponent(SceneRoot);
	BoundaryMesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BoundaryMesh"));
	BoundaryMesh->SetupAttachment(SceneRoot);
	BoundaryMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundaryMesh->SetMobility(EComponentMobility::Static);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		BoundaryMesh->SetStaticMesh(CubeMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BoundaryMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BoundaryMaterial.Succeeded())
	{
		BoundaryMaterialParent = BoundaryMaterial.Object;
	}
	SetRadius(Radius);
}

void ARTSStartingZone::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	TeamId = NewTeamId;
	if (HasActorBegunPlay())
	{
		UpdatePresentation();
	}
}

FGenericTeamId ARTSStartingZone::GetGenericTeamId() const
{
	return TeamId;
}

void ARTSStartingZone::SetRadius(const float NewRadius)
{
	Radius = FMath::Max(0.0f, NewRadius);
	BoundaryMesh->ClearInstances();
	constexpr int32 SegmentCount = 32;
	constexpr float SegmentFillRatio = 0.82f;
	constexpr float BoundaryWidth = 150.0f;
	const float SegmentLength = 2.0f * UE_PI * Radius / SegmentCount * SegmentFillRatio;
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		const float AngleRadians = 2.0f * UE_PI * SegmentIndex / SegmentCount;
		const FVector SegmentLocation(
			FMath::Cos(AngleRadians) * Radius,
			FMath::Sin(AngleRadians) * Radius,
			0.0f);
		const FRotator SegmentRotation(0.0f, FMath::RadiansToDegrees(AngleRadians) + 90.0f, 0.0f);
		const FVector SegmentScale(SegmentLength / 100.0f, BoundaryWidth / 100.0f, 0.05f);
		BoundaryMesh->AddInstance(FTransform(SegmentRotation, SegmentLocation, SegmentScale));
	}
}

float ARTSStartingZone::GetRadius() const
{
	return Radius;
}

void ARTSStartingZone::BeginPlay()
{
	Super::BeginPlay();
	UpdatePresentation();
}

void ARTSStartingZone::UpdatePresentation()
{
	if (BoundaryMaterialInstance == nullptr && BoundaryMaterialParent != nullptr)
	{
		// BasicShapeMaterial is cooked with instanced-static-mesh usage. The template faction
		// material is not, so packaged builds silently replaced this boundary with the default
		// material even though editor rendering looked correct.
		BoundaryMaterialInstance = UMaterialInstanceDynamic::Create(BoundaryMaterialParent, this);
	}
	if (BoundaryMaterialInstance == nullptr)
	{
		return;
	}

	const FLinearColor ZoneColor = TeamId == RTSTeams::Player
		? FLinearColor(0.03f, 0.20f, 1.0f)
		: TeamId == RTSTeams::Enemy
			? FLinearColor(1.0f, 0.04f, 0.02f)
			: FLinearColor(0.15f, 0.15f, 0.15f);
	BoundaryMaterialInstance->SetVectorParameterValue(TEXT("Color"), ZoneColor);
	BoundaryMesh->SetMaterial(0, BoundaryMaterialInstance);
}
