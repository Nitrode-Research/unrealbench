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
	StableWreckageId = NewStableWreckageId;
	SourceKind = ERTSWreckageSourceKind::Unit;
	SourceUnitType = NewSourceUnitType;
	OriginalTeamId = NewOriginalTeamId;
	ReclaimMaterialValue = FMath::Max(0, NewReclaimMaterialValue);

	FVector WorldSize(180.0f, 130.0f, 28.0f);
	switch (SourceUnitType)
	{
	case ERTSUnitType::CommandVehicle: WorldSize = FVector(280.0f, 200.0f, 35.0f); break;
	case ERTSUnitType::InfantrySquad: WorldSize = FVector(115.0f, 90.0f, 20.0f); break;
	case ERTSUnitType::LightVehicle: WorldSize = FVector(210.0f, 135.0f, 28.0f); break;
	case ERTSUnitType::HeavyVehicle: WorldSize = FVector(280.0f, 190.0f, 36.0f); break;
	}
	ApplyPresentation(WorldSize);
}

void ARTSWreckage::ConfigureFromStructure(
	const int32 NewStableWreckageId,
	const ERTSStructureType NewSourceStructureType,
	const FGenericTeamId NewOriginalTeamId,
	const int32 NewReclaimMaterialValue,
	const FIntPoint& SourceFootprintCells,
	const float PlacementCellSize)
{
	StableWreckageId = NewStableWreckageId;
	SourceKind = ERTSWreckageSourceKind::Structure;
	SourceStructureType = NewSourceStructureType;
	OriginalTeamId = NewOriginalTeamId;
	ReclaimMaterialValue = FMath::Max(0, NewReclaimMaterialValue);
	ApplyPresentation(FVector(
		SourceFootprintCells.X * PlacementCellSize * 0.8f,
		SourceFootprintCells.Y * PlacementCellSize * 0.8f,
		40.0f));
}

FRTSWreckageSnapshot ARTSWreckage::GetSnapshot() const
{
	FRTSWreckageSnapshot Snapshot;
	Snapshot.StableWreckageId = StableWreckageId;
	Snapshot.SourceKind = SourceKind;
	Snapshot.SourceUnitType = SourceUnitType;
	Snapshot.SourceStructureType = SourceStructureType;
	Snapshot.OriginalTeamId = OriginalTeamId;
	Snapshot.WorldLocation = GetActorLocation();
	Snapshot.ReclaimMaterialValue = ReclaimMaterialValue;
	Snapshot.bConsumed = bConsumed;
	if (const ARTSCombatUnit* Reclaimer = LeaseHolder.Get(); IsValid(Reclaimer))
	{
		Snapshot.LeaseHolderStableUnitId = Reclaimer->GetStableUnitId();
	}
	return Snapshot;
}

FVector ARTSWreckage::GetStatusAnchorWorldLocation() const
{
	const FBoxSphereBounds Bounds = WreckageMesh->Bounds;
	return GetActorLocation() + FVector(0.0f, 0.0f, Bounds.BoxExtent.Z + 45.0f);
}

bool ARTSWreckage::IsAvailable() const
{
	return !bConsumed && !IsActorBeingDestroyed();
}

FRTSReclaimResult ARTSWreckage::TryAcquireLease(ARTSCombatUnit& Reclaimer)
{
	FRTSReclaimResult Result;
	if (!IsAvailable())
	{
		Result.Refusal = ERTSReclaimRefusal::WreckageUnavailable;
		return Result;
	}
	if (!Reclaimer.IsAlive())
	{
		Result.Refusal = ERTSReclaimRefusal::ReclaimerUnavailable;
		return Result;
	}
	if (Reclaimer.GetWorld() != GetWorld())
	{
		Result.Refusal = ERTSReclaimRefusal::WrongWorld;
		return Result;
	}
	if (ARTSCombatUnit* Existing = LeaseHolder.Get(); IsValid(Existing) && Existing != &Reclaimer)
	{
		Result.Refusal = ERTSReclaimRefusal::AlreadyLeased;
		return Result;
	}
	LeaseHolder = &Reclaimer;
	Result.bAccepted = true;
	return Result;
}

void ARTSWreckage::ReleaseLease(ARTSCombatUnit& Reclaimer)
{
	if (LeaseHolder.Get() == &Reclaimer)
	{
		LeaseHolder.Reset();
	}
}

FRTSReclaimResult ARTSWreckage::TryCompleteReclaim(ARTSCombatUnit& Reclaimer)
{
	FRTSReclaimResult Result;
	if (!IsAvailable())
	{
		Result.Refusal = ERTSReclaimRefusal::WreckageUnavailable;
		return Result;
	}
	if (LeaseHolder.Get() != &Reclaimer || !Reclaimer.IsAlive())
	{
		Result.Refusal = ERTSReclaimRefusal::ReclaimerUnavailable;
		return Result;
	}
	URTSEconomySubsystem* Economy = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSEconomySubsystem>()
		: nullptr;
	if (Economy == nullptr)
	{
		Result.Refusal = ERTSReclaimRefusal::EconomyRejected;
		return Result;
	}

	FRTSEconomyDelta Payout;
	Payout.Materials = ReclaimMaterialValue;
	if (!Economy->TryCommit(Economy->AllocateTransactionId(), Reclaimer.GetGenericTeamId(), Payout).bAccepted)
	{
		Result.Refusal = ERTSReclaimRefusal::EconomyRejected;
		return Result;
	}

	// Consumption follows the successful atomic ledger commit. Every later attempt sees the guard
	// before it can allocate another transaction.
	bConsumed = true;
	LeaseHolder.Reset();
	if (URTSMatchSubsystem* Match = GetWorld()->GetSubsystem<URTSMatchSubsystem>())
	{
		Match->ReportMaterialsReclaimed(
			Reclaimer.GetGenericTeamId(),
			StableWreckageId,
			ReclaimMaterialValue);
	}
	Result.bAccepted = true;
	Result.bCompleted = true;
	Destroy();
	return Result;
}

void ARTSWreckage::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (URTSWreckageSubsystem* Wreckage = World->GetSubsystem<URTSWreckageSubsystem>())
		{
			Wreckage->UnregisterWreckage(*this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ARTSWreckage::ApplyPresentation(const FVector& WorldSize)
{
	WreckageMesh->SetRelativeLocation(FVector(0.0f, 0.0f, WorldSize.Z * 0.5f));
	WreckageMesh->SetRelativeRotation(FRotator(0.0f, 12.0f, 4.0f));
	WreckageMesh->SetRelativeScale3D(WorldSize / 100.0f);
	if (MaterialInstance == nullptr && MaterialParent != nullptr)
	{
		MaterialInstance = UMaterialInstanceDynamic::Create(MaterialParent, this);
	}
	if (MaterialInstance != nullptr)
	{
		MaterialInstance->SetVectorParameterValue(TEXT("DiffuseColor"), FLinearColor(0.22f, 0.12f, 0.055f));
		WreckageMesh->SetMaterial(0, MaterialInstance);
	}
}
