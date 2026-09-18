// Copyright Epic Games, Inc. All Rights Reserved.

#include "Reclaim/RTSWreckageSubsystem.h"

#include "Configuration/RTSMilestone2Configuration.h"
#include "Engine/World.h"
#include "Reclaim/RTSWreckage.h"
#include "Structures/RTSStructure.h"
#include "Units/RTSCombatUnit.h"

ARTSWreckage* URTSWreckageSubsystem::CreateFromUnit(const ARTSCombatUnit& Unit)
{
	if (!Unit.IsMilestone2Unit() || Unit.GetWorld() != GetWorld())
	{
		return nullptr;
	}
	const FRTSUnitDefinition* Definition = FRTSMilestone2Configuration::Load().FindUnit(Unit.GetUnitType());
	ARTSWreckage* Wreckage = Definition != nullptr ? SpawnWreckage(Unit.GetActorLocation()) : nullptr;
	if (Wreckage == nullptr)
	{
		return nullptr;
	}
	const int32 StableWreckageId = NextStableWreckageId++;
	Wreckage->ConfigureFromUnit(
		StableWreckageId,
		Unit.GetUnitType(),
		Unit.GetGenericTeamId(),
		FMath::RoundToInt(Definition->MaterialCost * FRTSMilestone2Configuration::Load().Reclaim.MaterialFraction));
	WreckageById.Add(StableWreckageId, Wreckage);
	return Wreckage;
}

ARTSWreckage* URTSWreckageSubsystem::CreateFromStructure(const ARTSStructure& Structure)
{
	if (Structure.GetWorld() != GetWorld())
	{
		return nullptr;
	}
	const FRTSMilestone2Configuration Configuration = FRTSMilestone2Configuration::Load();
	const FRTSStructureDefinition* Definition = Configuration.FindStructure(Structure.GetStructureType());
	ARTSWreckage* Wreckage = Definition != nullptr ? SpawnWreckage(Structure.GetActorLocation()) : nullptr;
	if (Wreckage == nullptr)
	{
		return nullptr;
	}
	const int32 StableWreckageId = NextStableWreckageId++;
	Wreckage->ConfigureFromStructure(
		StableWreckageId,
		Structure.GetStructureType(),
		Structure.GetGenericTeamId(),
		FMath::RoundToInt(Definition->MaterialCost * Configuration.Reclaim.MaterialFraction),
		Structure.GetFootprintCells(),
		Configuration.World.PlacementCellSize);
	WreckageById.Add(StableWreckageId, Wreckage);
	return Wreckage;
}

ARTSWreckage* URTSWreckageSubsystem::Find(const int32 StableWreckageId) const
{
	return WreckageById.FindRef(StableWreckageId).Get();
}

TArray<ARTSWreckage*> URTSWreckageSubsystem::QueryAvailable() const
{
	TArray<ARTSWreckage*> Results;
	for (const TPair<int32, TWeakObjectPtr<ARTSWreckage>>& Entry : WreckageById)
	{
		ARTSWreckage* Wreckage = Entry.Value.Get();
		if (IsValid(Wreckage) && Wreckage->IsAvailable())
		{
			Results.Add(Wreckage);
		}
	}
	Results.Sort([](const ARTSWreckage& Left, const ARTSWreckage& Right)
	{
		return Left.GetSnapshot().StableWreckageId < Right.GetSnapshot().StableWreckageId;
	});
	return Results;
}

TArray<FRTSWreckageSnapshot> URTSWreckageSubsystem::QueryAvailableSnapshots() const
{
	TArray<FRTSWreckageSnapshot> Results;
	for (ARTSWreckage* Wreckage : QueryAvailable())
	{
		Results.Add(Wreckage->GetSnapshot());
	}
	return Results;
}

void URTSWreckageSubsystem::UnregisterWreckage(ARTSWreckage& Wreckage)
{
	const int32 StableWreckageId = Wreckage.GetSnapshot().StableWreckageId;
	if (WreckageById.FindRef(StableWreckageId).Get() == &Wreckage)
	{
		WreckageById.Remove(StableWreckageId);
	}
}

ARTSWreckage* URTSWreckageSubsystem::SpawnWreckage(const FVector& WorldLocation)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ARTSWreckage>(
		ARTSWreckage::StaticClass(),
		WorldLocation,
		FRotator::ZeroRotator,
		Parameters);
}
