// Copyright Epic Games, Inc. All Rights Reserved.

#include "Configuration/RTSMilestone2Configuration.h"

#include "Misc/ConfigCacheIni.h"

namespace
{
void ReadFloat(const TCHAR* Section, const TCHAR* Key, float& Value)
{
	GConfig->GetFloat(Section, Key, Value, GGameIni);
}

void ReadInt(const TCHAR* Section, const TCHAR* Key, int32& Value)
{
	GConfig->GetInt(Section, Key, Value, GGameIni);
}

void LoadUnit(const TCHAR* Section, const ERTSUnitType Type, FRTSUnitDefinition& Definition)
{
	Definition.Type = Type;
	ReadFloat(Section, TEXT("MaximumHealth"), Definition.MaximumHealth);
	ReadFloat(Section, TEXT("MovementSpeed"), Definition.MovementSpeed);
	ReadFloat(Section, TEXT("AttackDamage"), Definition.AttackDamage);
	ReadFloat(Section, TEXT("AttackRange"), Definition.AttackRange);
	ReadFloat(Section, TEXT("AcquisitionRange"), Definition.AcquisitionRange);
	ReadFloat(Section, TEXT("WeaponCooldownSeconds"), Definition.WeaponCooldownSeconds);
	ReadInt(Section, TEXT("MaterialCost"), Definition.MaterialCost);
	ReadInt(Section, TEXT("SupplyCost"), Definition.SupplyCost);
	ReadFloat(Section, TEXT("ProductionSeconds"), Definition.ProductionSeconds);
}

void LoadStructure(
	const TCHAR* Section,
	const ERTSStructureType Type,
	FRTSStructureDefinition& Definition)
{
	Definition.Type = Type;
	ReadInt(Section, TEXT("FootprintWidthCells"), Definition.FootprintCells.X);
	ReadInt(Section, TEXT("FootprintHeightCells"), Definition.FootprintCells.Y);
	ReadFloat(Section, TEXT("MaximumHealth"), Definition.MaximumHealth);
	ReadInt(Section, TEXT("MaterialCost"), Definition.MaterialCost);
	ReadFloat(Section, TEXT("ConstructionSeconds"), Definition.ConstructionSeconds);
	ReadInt(Section, TEXT("PowerGeneration"), Definition.PowerGeneration);
	ReadInt(Section, TEXT("PowerDemand"), Definition.PowerDemand);
	ReadInt(Section, TEXT("SupplyCapacity"), Definition.SupplyCapacity);
	ReadFloat(Section, TEXT("BuildAreaRadius"), Definition.BuildAreaRadius);
	ReadInt(Section, TEXT("MaterialIncomePerInterval"), Definition.MaterialIncomePerInterval);
	ReadFloat(Section, TEXT("AttackDamage"), Definition.AttackDamage);
	ReadFloat(Section, TEXT("AttackRange"), Definition.AttackRange);
	ReadFloat(Section, TEXT("WeaponCooldownSeconds"), Definition.WeaponCooldownSeconds);
}

void ValidateUnit(
	const FRTSUnitDefinition& Definition,
	const ERTSUnitType ExpectedType,
	const TCHAR* Label,
	TArray<FString>& Errors)
{
	if (Definition.Type != ExpectedType)
	{
		Errors.Add(FString::Printf(TEXT("%s has the wrong typed identity."), Label));
	}
	if (Definition.MaximumHealth <= 0.0f || Definition.MovementSpeed <= 0.0f)
	{
		Errors.Add(FString::Printf(TEXT("%s requires positive health and movement speed."), Label));
	}
	if (Definition.AttackDamage <= 0.0f
		|| Definition.AttackRange <= 0.0f
		|| Definition.AcquisitionRange < Definition.AttackRange
		|| Definition.WeaponCooldownSeconds <= 0.0f)
	{
		Errors.Add(FString::Printf(TEXT("%s has an invalid combat profile."), Label));
	}
	if (Definition.MaterialCost < 0 || Definition.SupplyCost < 0 || Definition.ProductionSeconds < 0.0f)
	{
		Errors.Add(FString::Printf(TEXT("%s has a negative economy value."), Label));
	}
}

void ValidateStructure(
	const FRTSStructureDefinition& Definition,
	const ERTSStructureType ExpectedType,
	const TCHAR* Label,
	TArray<FString>& Errors)
{
	if (Definition.Type != ExpectedType)
	{
		Errors.Add(FString::Printf(TEXT("%s has the wrong typed identity."), Label));
	}
	if (Definition.FootprintCells.X <= 0
		|| Definition.FootprintCells.Y <= 0
		|| Definition.MaximumHealth <= 0.0f)
	{
		Errors.Add(FString::Printf(TEXT("%s requires a positive footprint and health."), Label));
	}
	if (Definition.MaterialCost < 0
		|| Definition.ConstructionSeconds < 0.0f
		|| Definition.PowerGeneration < 0
		|| Definition.PowerDemand < 0
		|| Definition.SupplyCapacity < 0
		|| Definition.BuildAreaRadius < 0.0f
		|| Definition.MaterialIncomePerInterval < 0
		|| Definition.AttackDamage < 0.0f
		|| Definition.AttackRange < 0.0f
		|| Definition.WeaponCooldownSeconds < 0.0f)
	{
		Errors.Add(FString::Printf(TEXT("%s has a negative construction or economy value."), Label));
	}
	if (Definition.PowerGeneration > 0 && Definition.PowerDemand > 0)
	{
		Errors.Add(FString::Printf(TEXT("%s cannot generate and demand nominal Power simultaneously."), Label));
	}
	if (ExpectedType == ERTSStructureType::DefensiveTurret
		&& (Definition.AttackDamage <= 0.0f
			|| Definition.AttackRange <= 0.0f
			|| Definition.WeaponCooldownSeconds <= 0.0f))
	{
		Errors.Add(TEXT("Defensive Turret requires a positive combat profile."));
	}
}
}

FRTSMilestone2Configuration FRTSMilestone2Configuration::Load()
{
	FRTSMilestone2Configuration Configuration;

	constexpr TCHAR WorldSection[] = TEXT("RTS.Milestone2.World");
	ReadFloat(WorldSection, TEXT("PlayableHalfExtent"), Configuration.World.PlayableHalfExtent);
	ReadFloat(WorldSection, TEXT("VisualGroundHalfExtent"), Configuration.World.VisualGroundHalfExtent);
	ReadFloat(WorldSection, TEXT("NavigationHalfExtent"), Configuration.World.NavigationHalfExtent);
	ReadFloat(WorldSection, TEXT("NavigationVerticalHalfExtent"), Configuration.World.NavigationVerticalHalfExtent);
	ReadFloat(WorldSection, TEXT("BaseCenterSeparation"), Configuration.World.BaseCenterSeparation);
	ReadFloat(WorldSection, TEXT("MainApproachWidth"), Configuration.World.MainApproachWidth);
	ReadFloat(WorldSection, TEXT("NarrowApproachWidth"), Configuration.World.NarrowApproachWidth);
	ReadFloat(WorldSection, TEXT("PlacementCellSize"), Configuration.World.PlacementCellSize);
	ReadFloat(WorldSection, TEXT("CameraPanHalfExtent"), Configuration.World.CameraPanHalfExtent);
	ReadInt(WorldSection, TEXT("MaterialDepositCount"), Configuration.World.MaterialDepositCount);

	constexpr TCHAR EconomySection[] = TEXT("RTS.Milestone2.Economy");
	ReadInt(EconomySection, TEXT("StartingMaterials"), Configuration.Economy.StartingMaterials);
	ReadFloat(
		EconomySection,
		TEXT("MaterialIncomeIntervalSeconds"),
		Configuration.Economy.MaterialIncomeIntervalSeconds);

	constexpr TCHAR DeploymentSection[] = TEXT("RTS.Milestone2.Deployment");
	ReadFloat(DeploymentSection, TEXT("StartingZoneRadius"), Configuration.Deployment.StartingZoneRadius);
	ReadFloat(
		DeploymentSection,
		TEXT("HeadquartersBuildAreaRadius"),
		Configuration.Deployment.HeadquartersBuildAreaRadius);
	ReadFloat(
		DeploymentSection,
		TEXT("SupplyDepotBuildAreaRadius"),
		Configuration.Deployment.SupplyDepotBuildAreaRadius);
	ReadFloat(
		DeploymentSection,
		TEXT("MaximumGroundSlopeDegrees"),
		Configuration.Deployment.MaximumGroundSlopeDegrees);

	constexpr TCHAR ProductionSection[] = TEXT("RTS.Milestone2.Production");
	ReadInt(ProductionSection, TEXT("MaximumQueueEntries"), Configuration.Production.MaximumQueueEntries);
	ReadFloat(ProductionSection, TEXT("FactoryExitDepth"), Configuration.Production.FactoryExitDepth);

	constexpr TCHAR ReclaimSection[] = TEXT("RTS.Milestone2.Reclaim");
	ReadFloat(ReclaimSection, TEXT("Range"), Configuration.Reclaim.Range);
	ReadFloat(ReclaimSection, TEXT("DurationSeconds"), Configuration.Reclaim.DurationSeconds);
	ReadFloat(ReclaimSection, TEXT("MaterialFraction"), Configuration.Reclaim.MaterialFraction);

	LoadUnit(TEXT("RTS.Milestone2.Unit.CommandVehicle"), ERTSUnitType::CommandVehicle, Configuration.CommandVehicle);
	LoadUnit(TEXT("RTS.Milestone2.Unit.InfantrySquad"), ERTSUnitType::InfantrySquad, Configuration.InfantrySquad);
	LoadUnit(TEXT("RTS.Milestone2.Unit.LightVehicle"), ERTSUnitType::LightVehicle, Configuration.LightVehicle);
	LoadUnit(TEXT("RTS.Milestone2.Unit.HeavyVehicle"), ERTSUnitType::HeavyVehicle, Configuration.HeavyVehicle);

	LoadStructure(TEXT("RTS.Milestone2.Structure.Headquarters"), ERTSStructureType::Headquarters, Configuration.Headquarters);
	LoadStructure(TEXT("RTS.Milestone2.Structure.MaterialExtractor"), ERTSStructureType::MaterialExtractor, Configuration.MaterialExtractor);
	LoadStructure(TEXT("RTS.Milestone2.Structure.PowerGenerator"), ERTSStructureType::PowerGenerator, Configuration.PowerGenerator);
	LoadStructure(TEXT("RTS.Milestone2.Structure.Factory"), ERTSStructureType::Factory, Configuration.Factory);
	LoadStructure(TEXT("RTS.Milestone2.Structure.SupplyDepot"), ERTSStructureType::SupplyDepot, Configuration.SupplyDepot);
	LoadStructure(TEXT("RTS.Milestone2.Structure.DefensiveTurret"), ERTSStructureType::DefensiveTurret, Configuration.DefensiveTurret);
	return Configuration;
}

bool FRTSMilestone2Configuration::Validate(TArray<FString>& OutErrors) const
{
	OutErrors.Reset();
	if (World.PlayableHalfExtent <= 0.0f
		|| World.VisualGroundHalfExtent < World.NavigationHalfExtent
		|| World.NavigationHalfExtent < World.PlayableHalfExtent
		|| World.NavigationVerticalHalfExtent <= 0.0f
		|| World.CameraPanHalfExtent <= 0.0f
		|| World.CameraPanHalfExtent > World.PlayableHalfExtent)
	{
		OutErrors.Add(TEXT("World extents do not contain playable, navigation, visual, and camera bounds."));
	}
	if (World.BaseCenterSeparation <= 0.0f
		|| World.MainApproachWidth <= 140.0f
		|| World.NarrowApproachWidth <= 140.0f
		|| World.MainApproachWidth <= World.NarrowApproachWidth
		|| World.PlacementCellSize <= 0.0f
		|| World.MaterialDepositCount <= 0)
	{
		OutErrors.Add(TEXT("World layout tuning cannot produce the required two-route battlefield."));
	}
	if (Economy.StartingMaterials < 0 || Economy.MaterialIncomeIntervalSeconds <= 0.0f)
	{
		OutErrors.Add(TEXT("Economy tuning requires non-negative starting Materials and a positive income interval."));
	}
	if (Deployment.StartingZoneRadius <= 0.0f
		|| World.BaseCenterSeparation * 0.5f + Deployment.StartingZoneRadius > World.PlayableHalfExtent
		|| Deployment.HeadquartersBuildAreaRadius <= 0.0f
		|| Deployment.SupplyDepotBuildAreaRadius <= 0.0f
		|| Deployment.MaximumGroundSlopeDegrees < 0.0f
		|| Deployment.MaximumGroundSlopeDegrees >= 90.0f)
	{
		OutErrors.Add(TEXT("Deployment radii and slope tolerance are invalid."));
	}
	if (Production.MaximumQueueEntries <= 0 || Production.FactoryExitDepth <= 0.0f)
	{
		OutErrors.Add(TEXT("Production requires a bounded queue and positive Factory exit depth."));
	}
	if (Reclaim.Range <= 0.0f
		|| Reclaim.DurationSeconds <= 0.0f
		|| Reclaim.MaterialFraction <= 0.0f
		|| Reclaim.MaterialFraction > 1.0f)
	{
		OutErrors.Add(TEXT("Reclaim range, duration, or Material fraction is invalid."));
	}

	ValidateUnit(CommandVehicle, ERTSUnitType::CommandVehicle, TEXT("Command Vehicle"), OutErrors);
	ValidateUnit(InfantrySquad, ERTSUnitType::InfantrySquad, TEXT("Infantry Squad"), OutErrors);
	ValidateUnit(LightVehicle, ERTSUnitType::LightVehicle, TEXT("Light Vehicle"), OutErrors);
	ValidateUnit(HeavyVehicle, ERTSUnitType::HeavyVehicle, TEXT("Heavy Vehicle"), OutErrors);

	ValidateStructure(Headquarters, ERTSStructureType::Headquarters, TEXT("Headquarters"), OutErrors);
	ValidateStructure(MaterialExtractor, ERTSStructureType::MaterialExtractor, TEXT("Material Extractor"), OutErrors);
	ValidateStructure(PowerGenerator, ERTSStructureType::PowerGenerator, TEXT("Power Generator"), OutErrors);
	ValidateStructure(Factory, ERTSStructureType::Factory, TEXT("Factory"), OutErrors);
	ValidateStructure(SupplyDepot, ERTSStructureType::SupplyDepot, TEXT("Supply Depot"), OutErrors);
	ValidateStructure(DefensiveTurret, ERTSStructureType::DefensiveTurret, TEXT("Defensive Turret"), OutErrors);

	if (!FMath::IsNearlyEqual(Headquarters.BuildAreaRadius, Deployment.HeadquartersBuildAreaRadius)
		|| !FMath::IsNearlyEqual(SupplyDepot.BuildAreaRadius, Deployment.SupplyDepotBuildAreaRadius))
	{
		OutErrors.Add(TEXT("Structure and deployment build-area radii disagree."));
	}
	if (MaterialExtractor.MaterialIncomePerInterval <= 0)
	{
		OutErrors.Add(TEXT("The Material Extractor must define positive income."));
	}
	return OutErrors.IsEmpty();
}

const FRTSUnitDefinition* FRTSMilestone2Configuration::FindUnit(const ERTSUnitType Type) const
{
	switch (Type)
	{
	case ERTSUnitType::CommandVehicle: return &CommandVehicle;
	case ERTSUnitType::InfantrySquad: return &InfantrySquad;
	case ERTSUnitType::LightVehicle: return &LightVehicle;
	case ERTSUnitType::HeavyVehicle: return &HeavyVehicle;
	}
	return nullptr;
}

const FRTSStructureDefinition* FRTSMilestone2Configuration::FindStructure(const ERTSStructureType Type) const
{
	switch (Type)
	{
	case ERTSStructureType::Headquarters: return &Headquarters;
	case ERTSStructureType::MaterialExtractor: return &MaterialExtractor;
	case ERTSStructureType::PowerGenerator: return &PowerGenerator;
	case ERTSStructureType::Factory: return &Factory;
	case ERTSStructureType::SupplyDepot: return &SupplyDepot;
	case ERTSStructureType::DefensiveTurret: return &DefensiveTurret;
	}
	return nullptr;
}
