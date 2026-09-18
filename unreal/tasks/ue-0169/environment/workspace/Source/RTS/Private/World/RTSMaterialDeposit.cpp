// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/RTSMaterialDeposit.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ARTSMaterialDeposit::ARTSMaterialDeposit()
{
	PrimaryActorTick.bCanEverTick = false;
	DepositMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DepositMesh"));
	SetRootComponent(DepositMesh);
	DepositMesh->SetMobility(EComponentMobility::Static);
	DepositMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	// Deposits must remain legible from the normal RTS camera. The map anchors sit 100 cm above
	// ground, so lowering a broad, flattened sphere produces a grounded mound rather than a small
	// floating engine primitive.
	DepositMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -35.0f));
	DepositMesh->SetRelativeScale3D(FVector(5.0f, 5.0f, 1.3f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		DepositMesh->SetStaticMesh(SphereMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DepositMaterial(
		TEXT("/Engine/TemplateResources/M_Template_Master.M_Template_Master"));
	if (DepositMaterial.Succeeded())
	{
		DepositMaterialParent = DepositMaterial.Object;
	}
}

void ARTSMaterialDeposit::BeginPlay()
{
	Super::BeginPlay();
	UpdatePresentation();
}

void ARTSMaterialDeposit::SetStableDepositId(const int32 NewStableDepositId)
{
	StableDepositId = NewStableDepositId;
}

int32 ARTSMaterialDeposit::GetStableDepositId() const
{
	return StableDepositId;
}

void ARTSMaterialDeposit::UpdatePresentation()
{
	if (DepositMaterialInstance == nullptr && DepositMaterialParent != nullptr)
	{
		DepositMaterialInstance = UMaterialInstanceDynamic::Create(DepositMaterialParent, this);
	}
	if (DepositMaterialInstance == nullptr)
	{
		return;
	}

	DepositMaterialInstance->SetVectorParameterValue(
		TEXT("DiffuseColor"),
		FLinearColor(0.12f, 0.035f, 0.008f));
	DepositMesh->SetMaterial(0, DepositMaterialInstance);
}
