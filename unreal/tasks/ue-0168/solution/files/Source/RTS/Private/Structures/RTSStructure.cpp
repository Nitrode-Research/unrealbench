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
	StructureType = NewType;
	StableStructureId = NewStableStructureId;
	TeamId = NewTeamId;
	FootprintCells = Definition.FootprintCells;
	BuildAreaRadius = Definition.BuildAreaRadius;
	PowerGeneration = Definition.PowerGeneration;
	PowerDemand = Definition.PowerDemand;
	SupplyCapacity = Definition.SupplyCapacity;
	MaterialIncomePerInterval = Definition.MaterialIncomePerInterval;
	StableDepositId = NewStableDepositId;
	MaximumHealth = Definition.MaximumHealth;
	ConstructionSeconds = Definition.ConstructionSeconds;
	HealthComponent->ConfigureMaximumHealth(MaximumHealth, true);
	if (StructureType == ERTSStructureType::Factory && ProductionComponent == nullptr)
	{
		ProductionComponent = NewObject<URTSProductionComponent>(this, TEXT("ProductionComponent"));
		AddInstanceComponent(ProductionComponent);
		ProductionComponent->RegisterComponent();
		ProductionComponent->Initialize(*this);
	}
	if (StructureType == ERTSStructureType::DefensiveTurret && TurretCombatComponent == nullptr)
	{
		TurretCombatComponent = NewObject<URTSTurretCombatComponent>(this, TEXT("TurretCombatComponent"));
		AddInstanceComponent(TurretCombatComponent);
		TurretCombatComponent->RegisterComponent();
		TurretCombatComponent->Initialize(
			*this,
			Definition.AttackDamage,
			Definition.AttackRange,
			Definition.WeaponCooldownSeconds);
	}

	const FVector FootprintSize(
		FootprintCells.X * PlacementCellSize,
		FootprintCells.Y * PlacementCellSize,
		500.0f);
	GreyboxMesh->SetRelativeLocation(FVector(0.0f, 0.0f, FootprintSize.Z * 0.5f));
	GreyboxMesh->SetRelativeScale3D(FootprintSize / 100.0f);
	// Type owns the broad silhouette color; this shallow roof keeps team ownership independently
	// readable when several differently colored structure types share one base.
	FactionAccentMesh->SetRelativeLocation(FVector(0.0f, 0.0f, FootprintSize.Z + 30.0f));
	FactionAccentMesh->SetRelativeScale3D(FVector(
		FootprintSize.X * 0.82f / 100.0f,
		FootprintSize.Y * 0.82f / 100.0f,
		0.6f));

	UpdatePresentation();
	if (URTSPresentationSubsystem* Presentation = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSPresentationSubsystem>()
		: nullptr)
	{
		Presentation->ObserveStructure(*this);
	}
}

void ARTSStructure::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	TeamId = NewTeamId;
	if (HasActorBegunPlay())
	{
		UpdatePresentation();
	}
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
	if (bConstructed || ConstructionSeconds <= 0.0f)
	{
		return 1.0f;
	}
	const UWorld* World = GetWorld();
	return World != nullptr
		? FMath::Clamp(
			static_cast<float>((World->GetTimeSeconds() - ConstructionStartedAtSeconds) / ConstructionSeconds),
			0.0f,
			1.0f)
		: 0.0f;
}

FRTSDamageResult ARTSStructure::ApplyDamage(const float RequestedDamage)
{
	const FRTSDamageResult Result = HealthComponent != nullptr
		? HealthComponent->ApplyDamage(RequestedDamage)
		: FRTSDamageResult();
	if (Result.bKilled)
	{
		HandleHealthDepleted();
	}
	return Result;
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
	Super::BeginPlay();
	// Configure() sizes the collision mesh before deferred spawning registers components. Cache the
	// finished footprint here so DynamicModifiersOnly never publishes the constructor-sized bounds.
	NavigationModifier->UpdateNavigationBounds();
	NavigationModifier->RefreshNavigationModifiers();
	HealthComponent->ConfigureMaximumHealth(MaximumHealth, true);
	ConstructionStartedAtSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	if (ConstructionSeconds <= 0.0f)
	{
		CompleteConstruction();
	}
	else if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ConstructionTimer,
			this,
			&ARTSStructure::CompleteConstruction,
			ConstructionSeconds,
			false);
	}
	UpdatePresentation();
}

void ARTSStructure::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ConstructionTimer);
		World->GetTimerManager().ClearTimer(MaterialIncomeTimer);
		if (URTSStructureSubsystem* Structures = World->GetSubsystem<URTSStructureSubsystem>())
		{
			Structures->UnregisterStructure(*this);
		}
	}
	ReleaseEconomyContribution();
	Super::EndPlay(EndPlayReason);
}

void ARTSStructure::CompleteConstruction()
{
	if (bConstructed || !IsAlive())
	{
		return;
	}

	UWorld* World = GetWorld();
	URTSEconomySubsystem* Economy = World != nullptr ? World->GetSubsystem<URTSEconomySubsystem>() : nullptr;
	if (Economy == nullptr)
	{
		return;
	}

	FRTSEconomyDelta Contribution;
	Contribution.PowerGeneration = PowerGeneration;
	Contribution.PowerDemand = PowerDemand;
	Contribution.SupplyCapacity = SupplyCapacity;
	if (PowerGeneration != 0 || PowerDemand != 0 || SupplyCapacity != 0)
	{
		const int64 TransactionId = Economy->AllocateTransactionId();
		if (!Economy->TryCommit(TransactionId, TeamId, Contribution).bAccepted)
		{
			return;
		}
		EconomyContributionTransactionId = TransactionId;
	}
	bEconomyContributionApplied = true;
	bConstructed = true;

	if (MaterialIncomePerInterval > 0)
	{
		const float Interval = FRTSMilestone2Configuration::Load().Economy.MaterialIncomeIntervalSeconds;
		World->GetTimerManager().SetTimer(
			MaterialIncomeTimer,
			this,
			&ARTSStructure::ApplyMaterialIncome,
			Interval,
			true);
	}
	if (URTSStructureSubsystem* Structures = World->GetSubsystem<URTSStructureSubsystem>())
	{
		Structures->NotifyConstructionCompleted(*this);
	}
	if (ProductionComponent != nullptr)
	{
		ProductionComponent->NotifyConstructionCompleted();
	}
	if (TurretCombatComponent != nullptr)
	{
		TurretCombatComponent->NotifyConstructionCompleted();
	}
}

void ARTSStructure::ApplyMaterialIncome()
{
	if (!bConstructed || !IsAlive() || MaterialIncomePerInterval <= 0)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (URTSEconomySubsystem* Economy = World->GetSubsystem<URTSEconomySubsystem>())
		{
			// Extractor income is the guaranteed way out of a brownout. Shedding it can permanently
			// strand a faction that has already spent below the cost of another Generator.
			FRTSEconomyDelta Income;
			Income.Materials = MaterialIncomePerInterval;
			Economy->TryCommit(Economy->AllocateTransactionId(), TeamId, Income);
		}
	}
}

void ARTSStructure::ReleaseEconomyContribution()
{
	if (!bEconomyContributionApplied)
	{
		return;
	}
	bEconomyContributionApplied = false;
	if (EconomyContributionTransactionId != 0)
	{
		if (UWorld* World = GetWorld())
		{
			if (URTSEconomySubsystem* Economy = World->GetSubsystem<URTSEconomySubsystem>())
			{
				Economy->TryRollback(EconomyContributionTransactionId);
			}
		}
		EconomyContributionTransactionId = 0;
	}
}

void ARTSStructure::HandleHealthDepleted()
{
	if (bHealthDepletionHandled)
	{
		return;
	}
	bHealthDepletionHandled = true;
	if (URTSStructureSubsystem* Structures = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
		: nullptr)
	{
		Structures->NotifyStructureDestroyed(*this);
	}
	if (StructureType == ERTSStructureType::Headquarters)
	{
		if (URTSMatchSubsystem* Match = GetWorld() != nullptr
			? GetWorld()->GetSubsystem<URTSMatchSubsystem>()
			: nullptr)
		{
			Match->ReportHeadquartersDestroyed(*this);
		}
	}
	if (!bWreckageCreated)
	{
		if (URTSWreckageSubsystem* Wreckage = GetWorld() != nullptr
			? GetWorld()->GetSubsystem<URTSWreckageSubsystem>()
			: nullptr)
		{
			bWreckageCreated = Wreckage->CreateFromStructure(*this) != nullptr;
		}
	}
	SetActorEnableCollision(false);
	Destroy();
}

void ARTSStructure::UpdatePresentation()
{
	if (FactionMaterialInstance == nullptr && FactionMaterialParent != nullptr)
	{
		FactionMaterialInstance = UMaterialInstanceDynamic::Create(FactionMaterialParent, this);
	}
	if (StructureTypeMaterialInstance == nullptr && FactionMaterialParent != nullptr)
	{
		StructureTypeMaterialInstance = UMaterialInstanceDynamic::Create(FactionMaterialParent, this);
	}
	if (FactionMaterialInstance == nullptr
		|| StructureTypeMaterialInstance == nullptr)
	{
		return;
	}

	const FLinearColor FactionColor = TeamId == RTSTeams::Player
		? FLinearColor(0.03f, 0.20f, 0.95f)
		: TeamId == RTSTeams::Enemy
			? FLinearColor(0.9f, 0.04f, 0.025f)
			: FLinearColor(0.3f, 0.3f, 0.3f);
	FactionMaterialInstance->SetVectorParameterValue(TEXT("DiffuseColor"), FactionColor);
	StructureTypeMaterialInstance->SetVectorParameterValue(
		TEXT("DiffuseColor"),
		StructureTypeColor(StructureType));
	GreyboxMesh->SetMaterial(0, StructureTypeMaterialInstance);
	FactionAccentMesh->SetMaterial(0, FactionMaterialInstance);
}
