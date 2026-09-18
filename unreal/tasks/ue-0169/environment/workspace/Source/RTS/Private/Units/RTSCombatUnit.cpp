// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/RTSCombatUnit.h"

#include "Combat/RTSCombatTypes.h"
#include "Combat/RTSHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "Economy/RTSEconomySubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Match/RTSMatchSubsystem.h"
#include "Orders/RTSOrderTypes.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Presentation/RTSPresentationSubsystem.h"
#include "Reclaim/RTSReclaimComponent.h"
#include "Reclaim/RTSWreckageSubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "Units/RTSUnitController.h"
#include "Units/RTSUnitRegistrySubsystem.h"
#include "Units/RTSTeams.h"

ARTSCombatUnit::ARTSCombatUnit()
{
	PrimaryActorTick.bCanEverTick = false;
	AIControllerClass = ARTSUnitController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;

	GetCapsuleComponent()->InitCapsuleSize(70.0f, 80.0f);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);

	GreyboxMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GreyboxMesh"));
	GreyboxMesh->SetupAttachment(GetCapsuleComponent());
	GreyboxMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -45.0f));
	GreyboxMesh->SetRelativeScale3D(FVector(1.8f, 1.2f, 0.7f));
	GreyboxMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GreyboxTurret = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GreyboxTurret"));
	GreyboxTurret->SetupAttachment(GetCapsuleComponent());
	GreyboxTurret->SetRelativeLocation(FVector(25.0f, 0.0f, 15.0f));
	GreyboxTurret->SetRelativeScale3D(FVector(0.8f, 0.7f, 0.5f));
	GreyboxTurret->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	HealthComponent = CreateDefaultSubobject<URTSHealthComponent>(TEXT("HealthComponent"));
	OrderComponent = CreateDefaultSubobject<URTSUnitOrderComponent>(TEXT("OrderComponent"));
	GetCharacterMovement()->bOrientRotationToMovement = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		GreyboxMesh->SetStaticMesh(CubeMesh.Object);
		GreyboxTurret->SetStaticMesh(CubeMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FactionMaterial(
		TEXT("/Engine/TemplateResources/M_Template_Master.M_Template_Master"));
	if (FactionMaterial.Succeeded())
	{
		FactionMaterialParent = FactionMaterial.Object;
	}
}

void ARTSCombatUnit::BeginPlay()
{
	// Restore this contractor-owned implementation.
	Super::BeginPlay();
}

void ARTSCombatUnit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Restore this contractor-owned implementation.
	Super::EndPlay(EndPlayReason);
}

void ARTSCombatUnit::ConfigureMilestone2Unit(
	const ERTSUnitType NewUnitType,
	const int32 NewStableUnitId,
	const FGenericTeamId NewTeamId,
	const int64 AdoptedSupplyTransactionId)
{
	// Restore this contractor-owned implementation.
}

void ARTSCombatUnit::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	// Restore this contractor-owned implementation.
}

FGenericTeamId ARTSCombatUnit::GetGenericTeamId() const
{
	return TeamId;
}

void ARTSCombatUnit::SetStableUnitId(const int32 NewStableUnitId)
{
	// Restore this contractor-owned implementation.
}

int32 ARTSCombatUnit::GetStableUnitId() const
{
	return StableUnitId;
}

bool ARTSCombatUnit::IsMilestone2Unit() const { return bMilestone2Unit; }
ERTSUnitType ARTSCombatUnit::GetUnitType() const { return UnitType; }

float ARTSCombatUnit::GetAttackDamage() const
{
	// Restore this contractor-owned implementation.
	return {};
}

float ARTSCombatUnit::GetAttackRange() const
{
	// Restore this contractor-owned implementation.
	return {};
}

float ARTSCombatUnit::GetAcquisitionRange() const
{
	// Restore this contractor-owned implementation.
	return {};
}

float ARTSCombatUnit::GetWeaponCooldownSeconds() const
{
	// Restore this contractor-owned implementation.
	return {};
}

FVector ARTSCombatUnit::GetStatusAnchorWorldLocation() const
{
	// Restore this contractor-owned implementation.
	return {};
}

bool ARTSCombatUnit::IsAlive() const
{
	return HealthComponent != nullptr && !HealthComponent->GetSnapshot().bDead;
}

void ARTSCombatUnit::MarkDead()
{
	// Restore this contractor-owned implementation.
}

URTSHealthComponent* ARTSCombatUnit::GetHealthComponent() const
{
	return HealthComponent;
}

URTSReclaimComponent* ARTSCombatUnit::GetReclaimComponent() const
{
	return ReclaimComponent;
}

URTSUnitOrderComponent* ARTSCombatUnit::GetOrderComponent() const
{
	return OrderComponent;
}

FRTSCombatUnitEligibilityChanged& ARTSCombatUnit::OnSelectionEligibilityChanged()
{
	return SelectionEligibilityChanged;
}

void ARTSCombatUnit::HandleHealthDepleted()
{
	// Restore this contractor-owned implementation.
}

void ARTSCombatUnit::ApplyUnitPresentation()
{
	// Restore this contractor-owned implementation.
}

void ARTSCombatUnit::RegisterEconomyAndCommandIdentity()
{
	// Restore this contractor-owned implementation.
}

void ARTSCombatUnit::UnregisterSupply()
{
	// Restore this contractor-owned implementation.
}

void ARTSCombatUnit::UpdateFactionPresentation()
{
	// Restore this contractor-owned implementation.
}
