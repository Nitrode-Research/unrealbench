// Copyright Epic Games, Inc. All Rights Reserved.

#include "Production/RTSProductionSubsystem.h"

#include "Production/RTSProductionComponent.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"

FRTSProductionResult URTSProductionSubsystem::TryEnqueue(
	const FGenericTeamId RequestingTeam,
	const int32 StableFactoryId,
	const ERTSUnitType UnitType)
{
	FRTSProductionResult Result;
	URTSStructureSubsystem* Structures = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	ARTSStructure* Structure = Structures != nullptr
		? Structures->FindActor(StableFactoryId)
		: nullptr;
	if (!IsValid(Structure) || !Structure->IsAlive())
	{
		Result.Refusal = ERTSProductionRefusal::FactoryUnavailable;
		return Result;
	}
	if (Structure->GetGenericTeamId() != RequestingTeam)
	{
		Result.Refusal = ERTSProductionRefusal::WrongTeam;
		return Result;
	}
	if (Structure->GetStructureType() != ERTSStructureType::Factory)
	{
		Result.Refusal = ERTSProductionRefusal::NotFactory;
		return Result;
	}
	URTSProductionComponent* Production = Structure->GetProductionComponent();
	if (Production == nullptr)
	{
		Result.Refusal = ERTSProductionRefusal::FactoryUnavailable;
		return Result;
	}
	return Production->TryEnqueue(UnitType);
}

TOptional<FRTSProductionSnapshot> URTSProductionSubsystem::Find(const int32 StableFactoryId) const
{
	const URTSStructureSubsystem* Structures = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	const ARTSStructure* Structure = Structures != nullptr
		? Structures->FindActor(StableFactoryId)
		: nullptr;
	const URTSProductionComponent* Production = IsValid(Structure)
		&& Structure->GetStructureType() == ERTSStructureType::Factory
		? Structure->GetProductionComponent()
		: nullptr;
	return Production != nullptr
		? TOptional<FRTSProductionSnapshot>(Production->GetSnapshot())
		: TOptional<FRTSProductionSnapshot>();
}
