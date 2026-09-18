// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/RTSPresentationText.h"

const TCHAR* RTSPresentationText::StructureTypeLabel(const ERTSStructureType StructureType)
{
	switch (StructureType)
	{
	case ERTSStructureType::Headquarters: return TEXT("HEADQUARTERS");
	case ERTSStructureType::MaterialExtractor: return TEXT("MATERIAL EXTRACTOR");
	case ERTSStructureType::PowerGenerator: return TEXT("POWER GENERATOR");
	case ERTSStructureType::Factory: return TEXT("FACTORY");
	case ERTSStructureType::SupplyDepot: return TEXT("SUPPLY DEPOT");
	case ERTSStructureType::DefensiveTurret: return TEXT("DEFENSIVE TURRET");
	}
	return TEXT("STRUCTURE");
}

const TCHAR* RTSPresentationText::UnitTypeLabel(const ERTSUnitType UnitType)
{
	switch (UnitType)
	{
	case ERTSUnitType::CommandVehicle: return TEXT("COMMAND VEHICLE");
	case ERTSUnitType::InfantrySquad: return TEXT("INFANTRY");
	case ERTSUnitType::LightVehicle: return TEXT("LIGHT VEHICLE");
	case ERTSUnitType::HeavyVehicle: return TEXT("HEAVY VEHICLE");
	}
	return TEXT("UNIT");
}
