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
	Super::BeginPlay();

	const FRTSMovementTuning MovementTuning = FRTSMovementTuning::Load();
	const FRTSUnitDefinition* UnitDefinition = bMilestone2Unit
		? FRTSMilestone2Configuration::Load().FindUnit(UnitType)
		: nullptr;
	GetCharacterMovement()->MaxWalkSpeed = UnitDefinition != nullptr
		? UnitDefinition->MovementSpeed
		: MovementTuning.UnitSpeed;
	GetCharacterMovement()->RotationRate = FRotator(
		0.0f,
		MovementTuning.TurnRateDegreesPerSecond,
		0.0f);
	if (UnitDefinition != nullptr)
	{
		HealthComponent->ConfigureMaximumHealth(UnitDefinition->MaximumHealth, true);
	}
	ApplyUnitPresentation();
	UpdateFactionPresentation();
	RegisterEconomyAndCommandIdentity();
	if (URTSUnitRegistrySubsystem* Registry = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSUnitRegistrySubsystem>()
		: nullptr)
	{
		Registry->RegisterUnit(*this);
	}
	if (URTSPresentationSubsystem* Presentation = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSPresentationSubsystem>()
		: nullptr)
	{
		Presentation->ObserveCombatUnit(*this);
	}
}

void ARTSCombatUnit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (URTSUnitRegistrySubsystem* Registry = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSUnitRegistrySubsystem>()
		: nullptr)
	{
		Registry->UnregisterUnit(*this);
	}
	UnregisterSupply();
	Super::EndPlay(EndPlayReason);
}

void ARTSCombatUnit::ConfigureMilestone2Unit(
	const ERTSUnitType NewUnitType,
	const int32 NewStableUnitId,
	const FGenericTeamId NewTeamId,
	const int64 AdoptedSupplyTransactionId)
{
	bMilestone2Unit = true;
	UnitType = NewUnitType;
	StableUnitId = NewStableUnitId;
	TeamId = NewTeamId;
	SupplyTransactionId = AdoptedSupplyTransactionId;
	if (UnitType == ERTSUnitType::InfantrySquad && ReclaimComponent == nullptr)
	{
		ReclaimComponent = NewObject<URTSReclaimComponent>(this, TEXT("ReclaimComponent"));
		AddInstanceComponent(ReclaimComponent);
		ReclaimComponent->RegisterComponent();
	}
	ApplyUnitPresentation();
	if (HasActorBegunPlay())
	{
		const FRTSUnitDefinition* UnitDefinition = FRTSMilestone2Configuration::Load().FindUnit(UnitType);
		if (UnitDefinition != nullptr)
		{
			HealthComponent->ConfigureMaximumHealth(UnitDefinition->MaximumHealth, true);
			GetCharacterMovement()->MaxWalkSpeed = UnitDefinition->MovementSpeed;
		}
		UpdateFactionPresentation();
		RegisterEconomyAndCommandIdentity();
		if (URTSUnitRegistrySubsystem* Registry = GetWorld() != nullptr
			? GetWorld()->GetSubsystem<URTSUnitRegistrySubsystem>()
			: nullptr)
		{
			Registry->RegisterUnit(*this);
		}
	}
}

void ARTSCombatUnit::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	TeamId = NewTeamId;
	if (HasActorBegunPlay())
	{
		UpdateFactionPresentation();
	}
}

FGenericTeamId ARTSCombatUnit::GetGenericTeamId() const
{
	return TeamId;
}

void ARTSCombatUnit::SetStableUnitId(const int32 NewStableUnitId)
{
	StableUnitId = NewStableUnitId;
	if (HasActorBegunPlay())
	{
		if (URTSUnitRegistrySubsystem* Registry = GetWorld() != nullptr
			? GetWorld()->GetSubsystem<URTSUnitRegistrySubsystem>()
			: nullptr)
		{
			Registry->RegisterUnit(*this);
		}
	}
}

int32 ARTSCombatUnit::GetStableUnitId() const
{
	return StableUnitId;
}

bool ARTSCombatUnit::IsMilestone2Unit() const { return bMilestone2Unit; }
ERTSUnitType ARTSCombatUnit::GetUnitType() const { return UnitType; }

float ARTSCombatUnit::GetAttackDamage() const
{
	const FRTSUnitDefinition* Definition = bMilestone2Unit
		? FRTSMilestone2Configuration::Load().FindUnit(UnitType)
		: nullptr;
	return Definition != nullptr ? Definition->AttackDamage : FRTSCombatTuning::Load().AttackDamage;
}

float ARTSCombatUnit::GetAttackRange() const
{
	const FRTSUnitDefinition* Definition = bMilestone2Unit
		? FRTSMilestone2Configuration::Load().FindUnit(UnitType)
		: nullptr;
	return Definition != nullptr ? Definition->AttackRange : FRTSCombatTuning::Load().AttackRange;
}

float ARTSCombatUnit::GetAcquisitionRange() const
{
	const FRTSUnitDefinition* Definition = bMilestone2Unit
		? FRTSMilestone2Configuration::Load().FindUnit(UnitType)
		: nullptr;
	return Definition != nullptr ? Definition->AcquisitionRange : FRTSCombatTuning::Load().AcquisitionRange;
}

float ARTSCombatUnit::GetWeaponCooldownSeconds() const
{
	const FRTSUnitDefinition* Definition = bMilestone2Unit
		? FRTSMilestone2Configuration::Load().FindUnit(UnitType)
		: nullptr;
	return Definition != nullptr
		? Definition->WeaponCooldownSeconds
		: FRTSCombatTuning::Load().WeaponCooldownSeconds;
}

FVector ARTSCombatUnit::GetStatusAnchorWorldLocation() const
{
	return GetActorLocation() + FVector(
		0.0f,
		0.0f,
		GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 30.0f);
}

bool ARTSCombatUnit::IsAlive() const
{
	return HealthComponent != nullptr && !HealthComponent->GetSnapshot().bDead;
}

void ARTSCombatUnit::MarkDead()
{
	if (HealthComponent == nullptr)
	{
		return;
	}
	HealthComponent->ApplyDamage(HealthComponent->GetSnapshot().CurrentHealth);
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
	if (!bDeathReported && bMilestone2Unit)
	{
		if (URTSMatchSubsystem* Match = GetWorld() != nullptr
			? GetWorld()->GetSubsystem<URTSMatchSubsystem>()
			: nullptr)
		{
			bDeathReported = Match->ReportUnitLost(TeamId, StableUnitId).bAccepted;
		}
	}
	if (UnitType == ERTSUnitType::CommandVehicle)
	{
		if (URTSMatchSubsystem* Match = GetWorld() != nullptr
			? GetWorld()->GetSubsystem<URTSMatchSubsystem>()
			: nullptr)
		{
			Match->ReportCommandVehicleDestroyed(*this);
		}
	}
	if (!bWreckageCreated && bMilestone2Unit)
	{
		if (URTSWreckageSubsystem* Wreckage = GetWorld() != nullptr
			? GetWorld()->GetSubsystem<URTSWreckageSubsystem>()
			: nullptr)
		{
			bWreckageCreated = Wreckage->CreateFromUnit(*this) != nullptr;
		}
	}
	UnregisterSupply();
	OrderComponent->Cancel();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GreyboxMesh->SetRelativeRotation(FRotator(0.0f, 0.0f, 75.0f));
	GreyboxMesh->SetRelativeScale3D(FVector(1.8f, 1.2f, 0.15f));
	GreyboxTurret->SetVisibility(false);
	SelectionEligibilityChanged.Broadcast(this);

	if (const UWorld* World = GetWorld(); World != nullptr && World->HasBegunPlay())
	{
		SetLifeSpan(FRTSCombatTuning::Load().DeathCleanupDelaySeconds);
	}
}

void ARTSCombatUnit::ApplyUnitPresentation()
{
	if (!bMilestone2Unit)
	{
		return;
	}

	switch (UnitType)
	{
	case ERTSUnitType::CommandVehicle:
		GetCapsuleComponent()->SetCapsuleSize(100.0f, 100.0f);
		GreyboxMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -35.0f));
		GreyboxMesh->SetRelativeScale3D(FVector(2.8f, 2.0f, 0.9f));
		GreyboxTurret->SetRelativeLocation(FVector(35.0f, 0.0f, 25.0f));
		GreyboxTurret->SetRelativeScale3D(FVector(1.1f, 0.9f, 0.6f));
		break;
	case ERTSUnitType::InfantrySquad:
		GetCapsuleComponent()->SetCapsuleSize(55.0f, 70.0f);
		// The squad token needs a taller screen silhouette than the vehicle hulls at the fixed RTS
		// camera. Keep the navigation capsule unchanged: this is readability, not a larger unit.
		GreyboxMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -25.0f));
		GreyboxMesh->SetRelativeScale3D(FVector(1.35f, 1.05f, 0.9f));
		GreyboxTurret->SetRelativeLocation(FVector(15.0f, 0.0f, 35.0f));
		GreyboxTurret->SetRelativeScale3D(FVector(0.45f, 0.4f, 0.45f));
		break;
	case ERTSUnitType::LightVehicle:
		GetCapsuleComponent()->SetCapsuleSize(75.0f, 80.0f);
		GreyboxMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -42.0f));
		GreyboxMesh->SetRelativeScale3D(FVector(2.1f, 1.35f, 0.65f));
		GreyboxTurret->SetRelativeLocation(FVector(25.0f, 0.0f, 12.0f));
		GreyboxTurret->SetRelativeScale3D(FVector(0.75f, 0.6f, 0.45f));
		break;
	case ERTSUnitType::HeavyVehicle:
		GetCapsuleComponent()->SetCapsuleSize(105.0f, 100.0f);
		GreyboxMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -32.0f));
		GreyboxMesh->SetRelativeScale3D(FVector(2.8f, 1.9f, 0.95f));
		GreyboxTurret->SetRelativeLocation(FVector(30.0f, 0.0f, 28.0f));
		GreyboxTurret->SetRelativeScale3D(FVector(1.15f, 0.95f, 0.65f));
		break;
	}
}

void ARTSCombatUnit::RegisterEconomyAndCommandIdentity()
{
	if (!bMilestone2Unit || SupplyTransactionId != 0 || GetWorld() == nullptr)
	{
		return;
	}
	const FRTSUnitDefinition* Definition = FRTSMilestone2Configuration::Load().FindUnit(UnitType);
	URTSEconomySubsystem* Economy = GetWorld()->GetSubsystem<URTSEconomySubsystem>();
	if (Definition != nullptr && Economy != nullptr)
	{
		FRTSEconomyDelta Delta;
		Delta.SupplyUsed = Definition->SupplyCost;
		const int64 TransactionId = Economy->AllocateTransactionId();
		if (Economy->TryCommit(TransactionId, TeamId, Delta).bAccepted)
		{
			SupplyTransactionId = TransactionId;
		}
	}
	if (UnitType == ERTSUnitType::CommandVehicle)
	{
		if (URTSMatchSubsystem* Match = GetWorld()->GetSubsystem<URTSMatchSubsystem>())
		{
			Match->RegisterCommandVehicle(*this);
		}
	}
}

void ARTSCombatUnit::UnregisterSupply()
{
	if (SupplyTransactionId == 0 || GetWorld() == nullptr)
	{
		return;
	}
	if (URTSEconomySubsystem* Economy = GetWorld()->GetSubsystem<URTSEconomySubsystem>())
	{
		Economy->TryRollback(SupplyTransactionId);
	}
	SupplyTransactionId = 0;
}

void ARTSCombatUnit::UpdateFactionPresentation()
{
	const FLinearColor FactionColor = TeamId == RTSTeams::Player
		? FLinearColor(0.05f, 0.25f, 0.95f)
		: TeamId == RTSTeams::Enemy
			? FLinearColor(0.9f, 0.04f, 0.025f)
			: FLinearColor(0.35f, 0.35f, 0.35f);

	if (FactionMaterialInstance == nullptr && FactionMaterialParent != nullptr)
	{
		FactionMaterialInstance = UMaterialInstanceDynamic::Create(FactionMaterialParent, this);
	}
	if (FactionMaterialInstance == nullptr)
	{
		return;
	}

	FactionMaterialInstance->SetVectorParameterValue(TEXT("DiffuseColor"), FactionColor);
	for (UStaticMeshComponent* PresentationMesh : { GreyboxMesh.Get(), GreyboxTurret.Get() })
	{
		PresentationMesh->SetMaterial(0, FactionMaterialInstance);
	}
}
