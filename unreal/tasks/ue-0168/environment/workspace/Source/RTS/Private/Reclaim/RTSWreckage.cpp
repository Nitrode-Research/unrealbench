// Copyright Epic Games, Inc. All Rights Reserved.

#include "Reclaim/RTSWreckage.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Match/RTSMatchSubsystem.h"
#include "Reclaim/RTSWreckageSubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "Units/RTSCombatUnit.h"

ARTSWreckage::ARTSWreckage()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	WreckageMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WreckageMesh"));
	WreckageMesh->SetupAttachment(SceneRoot);
	WreckageMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WreckageMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	WreckageMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	WreckageMesh->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		WreckageMesh->SetStaticMesh(CubeMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WreckageMaterial(
		TEXT("/Engine/TemplateResources/M_Template_Master.M_Template_Master"));
	if (WreckageMaterial.Succeeded())
	{
		MaterialParent = WreckageMaterial.Object;
	}
}

void ARTSWreckage::ConfigureFromUnit(
	const int32 NewStableWreckageId,
	const ERTSUnitType NewSourceUnitType,
	const FGenericTeamId NewOriginalTeamId,
	const int32 NewReclaimMaterialValue)
{
	// Restore this contractor-owned implementation.
}

void ARTSWreckage::ConfigureFromStructure(
	const int32 NewStableWreckageId,
	const ERTSStructureType NewSourceStructureType,
	const FGenericTeamId NewOriginalTeamId,
	const int32 NewReclaimMaterialValue,
	const FIntPoint& SourceFootprintCells,
	const float PlacementCellSize)
{
	// Restore this contractor-owned implementation.
}

FRTSWreckageSnapshot ARTSWreckage::GetSnapshot() const
{
	// Restore this contractor-owned implementation.
	return {};
}

FVector ARTSWreckage::GetStatusAnchorWorldLocation() const
{
	// Restore this contractor-owned implementation.
	return {};
}

bool ARTSWreckage::IsAvailable() const
{
	return !bConsumed && !IsActorBeingDestroyed();
}

FRTSReclaimResult ARTSWreckage::TryAcquireLease(ARTSCombatUnit& Reclaimer)
{
	// Restore this contractor-owned implementation.
	return {};
}

void ARTSWreckage::ReleaseLease(ARTSCombatUnit& Reclaimer)
{
	// Restore this contractor-owned implementation.
}

FRTSReclaimResult ARTSWreckage::TryCompleteReclaim(ARTSCombatUnit& Reclaimer)
{
	// Restore this contractor-owned implementation.
	return {};
}

void ARTSWreckage::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Restore this contractor-owned implementation.
	Super::EndPlay(EndPlayReason);
}

void ARTSWreckage::ApplyPresentation(const FVector& WorldSize)
{
	// Restore this contractor-owned implementation.
}
