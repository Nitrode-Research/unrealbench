// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RTSMilestone2Configuration.generated.h"

UENUM()
enum class ERTSUnitType : uint8
{
	CommandVehicle,
	InfantrySquad,
	LightVehicle,
	HeavyVehicle
};

UENUM()
enum class ERTSStructureType : uint8
{
	Headquarters,
	MaterialExtractor,
	PowerGenerator,
	Factory,
	SupplyDepot,
	DefensiveTurret
};

USTRUCT()
struct RTS_API FRTSMilestone2WorldTuning
{
	GENERATED_BODY()

	float PlayableHalfExtent = 15000.0f;
	float VisualGroundHalfExtent = 22500.0f;
	float NavigationHalfExtent = 16500.0f;
	float NavigationVerticalHalfExtent = 2000.0f;
	float BaseCenterSeparation = 18000.0f;
	float MainApproachWidth = 2400.0f;
	float NarrowApproachWidth = 1000.0f;
	float PlacementCellSize = 100.0f;
	float CameraPanHalfExtent = 14000.0f;
	int32 MaterialDepositCount = 8;
};

USTRUCT()
struct RTS_API FRTSEconomyTuning
{
	GENERATED_BODY()

	int32 StartingMaterials = 1200;
	float MaterialIncomeIntervalSeconds = 1.0f;
};

USTRUCT()
struct RTS_API FRTSDeploymentTuning
{
	GENERATED_BODY()

	float StartingZoneRadius = 5000.0f;
	float HeadquartersBuildAreaRadius = 5500.0f;
	float SupplyDepotBuildAreaRadius = 3500.0f;
	float MaximumGroundSlopeDegrees = 10.0f;
};

USTRUCT()
struct RTS_API FRTSProductionTuning
{
	GENERATED_BODY()

	int32 MaximumQueueEntries = 5;
	float FactoryExitDepth = 600.0f;
};

USTRUCT()
struct RTS_API FRTSReclaimTuning
{
	GENERATED_BODY()

	float Range = 250.0f;
	float DurationSeconds = 4.0f;
	float MaterialFraction = 0.5f;
};

USTRUCT()
struct RTS_API FRTSUnitDefinition
{
	GENERATED_BODY()

	ERTSUnitType Type = ERTSUnitType::InfantrySquad;
	float MaximumHealth = 100.0f;
	float MovementSpeed = 450.0f;
	float AttackDamage = 10.0f;
	float AttackRange = 450.0f;
	float AcquisitionRange = 1200.0f;
	float WeaponCooldownSeconds = 0.75f;
	int32 MaterialCost = 100;
	int32 SupplyCost = 1;
	float ProductionSeconds = 5.0f;
};

USTRUCT()
struct RTS_API FRTSStructureDefinition
{
	GENERATED_BODY()

	ERTSStructureType Type = ERTSStructureType::Headquarters;
	FIntPoint FootprintCells = FIntPoint(1, 1);
	float MaximumHealth = 500.0f;
	int32 MaterialCost = 0;
	float ConstructionSeconds = 0.0f;
	int32 PowerGeneration = 0;
	int32 PowerDemand = 0;
	int32 SupplyCapacity = 0;
	float BuildAreaRadius = 0.0f;
	int32 MaterialIncomePerInterval = 0;
	float AttackDamage = 0.0f;
	float AttackRange = 0.0f;
	float WeaponCooldownSeconds = 0.0f;
};

/**
 * Closed, text-backed Milestone 2 tuning.
 *
 * Each named field binds an ini section to one typed identity in code. Section names therefore
 * locate tuning but never decide which unit or structure a runtime object represents.
 */
USTRUCT()
struct RTS_API FRTSMilestone2Configuration
{
	GENERATED_BODY()

	FRTSMilestone2WorldTuning World;
	FRTSEconomyTuning Economy;
	FRTSDeploymentTuning Deployment;
	FRTSProductionTuning Production;
	FRTSReclaimTuning Reclaim;

	FRTSUnitDefinition CommandVehicle;
	FRTSUnitDefinition InfantrySquad;
	FRTSUnitDefinition LightVehicle;
	FRTSUnitDefinition HeavyVehicle;

	FRTSStructureDefinition Headquarters;
	FRTSStructureDefinition MaterialExtractor;
	FRTSStructureDefinition PowerGenerator;
	FRTSStructureDefinition Factory;
	FRTSStructureDefinition SupplyDepot;
	FRTSStructureDefinition DefensiveTurret;

	static FRTSMilestone2Configuration Load();
	bool Validate(TArray<FString>& OutErrors) const;
	const FRTSUnitDefinition* FindUnit(ERTSUnitType Type) const;
	const FRTSStructureDefinition* FindStructure(ERTSStructureType Type) const;
};
