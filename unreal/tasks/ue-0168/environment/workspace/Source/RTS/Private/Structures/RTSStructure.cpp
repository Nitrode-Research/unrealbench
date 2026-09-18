// Copyright Epic Games, Inc. All Rights Reserved.

#include "Structures/RTSStructure.h"

#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSTurretCombatComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Engine/World.h"
#include "NavAreas/NavArea_Null.h"
#include "NavModifierComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Match/RTSMatchSubsystem.h"
#include "Production/RTSProductionComponent.h"
#include "Presentation/RTSPresentationSubsystem.h"
#include "Reclaim/RTSWreckageSubsystem.h"
#include "Structures/RTSStructureSubsystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Units/RTSTeams.h"

namespace
{
FLinearColor StructureTypeColor(const ERTSStructureType StructureType)
{
	switch (StructureType)
	{
	case ERTSStructureType::Headquarters: return FLinearColor(0.95f, 0.42f, 0.04f);
	case ERTSStructureType::MaterialExtractor: return FLinearColor(0.62f, 0.28f, 0.08f);
	case ERTSStructureType::PowerGenerator: return FLinearColor(0.95f, 0.78f, 0.04f);
	case ERTSStructureType::Factory: return FLinearColor(0.48f, 0.10f, 0.78f);
	case ERTSStructureType::SupplyDepot: return FLinearColor(0.04f, 0.65f, 0.52f);
	case ERTSStructureType::DefensiveTurret: return FLinearColor(0.72f, 0.04f, 0.03f);
	}
	return FLinearColor(0.3f, 0.3f, 0.3f);
}
}

ARTSStructure::ARTSStructure()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	GreyboxMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GreyboxMesh"));
	GreyboxMesh->SetupAttachment(SceneRoot);
	GreyboxMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	GreyboxMesh->SetCanEverAffectNavigation(true);
	FactionAccentMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FactionAccentMesh"));
	FactionAccentMesh->SetupAttachment(SceneRoot);
	FactionAccentMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	NavigationModifier = CreateDefaultSubobject<UNavModifierComponent>(TEXT("NavigationModifier"));
	NavigationModifier->SetAreaClass(UNavArea_Null::StaticClass());
	HealthComponent = CreateDefaultSubobject<URTSHealthComponent>(TEXT("HealthComponent"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		GreyboxMesh->SetStaticMesh(CubeMesh.Object);
		FactionAccentMesh->SetStaticMesh(CubeMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FactionMaterial(
		TEXT("/Engine/TemplateResources/M_Template_Master.M_Template_Master"));
	if (FactionMaterial.Succeeded())
	{
		FactionMaterialParent = FactionMaterial.Object;
	}
}

void ARTSStructure::Configure(
	const ERTSStructureType NewType,
	const int32 NewStableStructureId,
	const FGenericTeamId NewTeamId,
	const FRTSStructureDefinition& Definition,
	const float PlacementCellSize,
	const int32 NewStableDepositId)
{
	// Restore this contractor-owned implementation.
}

void ARTSStructure::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	// Restore this contractor-owned implementation.
}

FGenericTeamId ARTSStructure::GetGenericTeamId() const { return TeamId; }
ERTSStructureType ARTSStructure::GetStructureType() const { return StructureType; }
int32 ARTSStructure::GetStableStructureId() const { return StableStructureId; }
FIntPoint ARTSStructure::GetFootprintCells() const { return FootprintCells; }
float ARTSStructure::GetBuildAreaRadius() const { return BuildAreaRadius; }
int32 ARTSStructure::GetPowerGeneration() const { return PowerGeneration; }
int32 ARTSStructure::GetPowerDemand() const { return PowerDemand; }
int32 ARTSStructure::GetSupplyCapacity() const { return SupplyCapacity; }
int32 ARTSStructure::GetStableDepositId() const { return StableDepositId; }

bool ARTSStructure::IsAlive() const
{
	return HealthComponent != nullptr && !HealthComponent->GetSnapshot().bDead;
}

bool ARTSStructure::IsConstructed() const
{
	return bConstructed;
}

float ARTSStructure::GetConstructionProgress() const
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSDamageResult ARTSStructure::ApplyDamage(const float RequestedDamage)
{
	// Restore this contractor-owned implementation.
	return {};
}

URTSHealthComponent* ARTSStructure::GetHealthComponent() const
{
	return HealthComponent;
}

URTSProductionComponent* ARTSStructure::GetProductionComponent() const
{
	return ProductionComponent;
}

URTSTurretCombatComponent* ARTSStructure::GetTurretCombatComponent() const
{
	return TurretCombatComponent;
}

void ARTSStructure::BeginPlay()
{
	// Restore this contractor-owned implementation.
	Super::BeginPlay();
}

void ARTSStructure::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Restore this contractor-owned implementation.
	Super::EndPlay(EndPlayReason);
}

void ARTSStructure::CompleteConstruction()
{
	// Restore this contractor-owned implementation.
}

void ARTSStructure::ApplyMaterialIncome()
{
	// Restore this contractor-owned implementation.
}

void ARTSStructure::ReleaseEconomyContribution()
{
	// Restore this contractor-owned implementation.
}

void ARTSStructure::HandleHealthDepleted()
{
	// Restore this contractor-owned implementation.
}

void ARTSStructure::UpdatePresentation()
{
	// Restore this contractor-owned implementation.
}
