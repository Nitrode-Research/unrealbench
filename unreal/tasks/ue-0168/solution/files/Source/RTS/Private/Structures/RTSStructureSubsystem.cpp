// Copyright Epic Games, Inc. All Rights Reserved.

#include "Structures/RTSStructureSubsystem.h"

#include "CollisionQueryParams.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "Match/RTSMatchSubsystem.h"
#include "NavigationSystem.h"
#include "Structures/RTSStructure.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"
#include "World/RTSMaterialDeposit.h"
#include "World/RTSStartingZone.h"

FRTSHeadquartersDeploymentPreview URTSStructureSubsystem::EvaluateHeadquartersDeployment(
	const ARTSCombatUnit& CommandVehicle) const
{
	return EvaluateHeadquartersDeploymentAt(CommandVehicle, CommandVehicle.GetActorLocation());
}

FRTSHeadquartersDeploymentPreview URTSStructureSubsystem::EvaluateHeadquartersDeploymentAt(
	const ARTSCombatUnit& CommandVehicle,
	const FVector& ProposedWorldLocation) const
{
	FRTSHeadquartersDeploymentPreview Preview;
	const FRTSMilestone2Configuration Configuration = FRTSMilestone2Configuration::Load();
	Preview.FootprintCells = Configuration.Headquarters.FootprintCells;
	Preview.BuildAreaRadius = Configuration.Headquarters.BuildAreaRadius;
	if (!IsValid(&CommandVehicle)
		|| !CommandVehicle.IsAlive()
		|| CommandVehicle.GetGenericTeamId() == FGenericTeamId::NoTeam)
	{
		return Preview;
	}
	if (!CommandVehicle.IsMilestone2Unit()
		|| CommandVehicle.GetUnitType() != ERTSUnitType::CommandVehicle)
	{
		Preview.Refusal = ERTSDeploymentRefusal::WrongUnitType;
		return Preview;
	}
	// Refusal feedback still needs a meaningful spatial anchor. Zone containment is evaluated
	// before the ground trace so it remains the primary refusal, but the HUD must never interpret
	// an early return as a preview at world origin.
	Preview.GroundLocation = FVector(
		ProposedWorldLocation.X,
		ProposedWorldLocation.Y,
		0.0f);

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return Preview;
	}
	const URTSMatchSubsystem* Match = World->GetSubsystem<URTSMatchSubsystem>();
	if (Match != nullptr && Match->IsResolved())
	{
		Preview.Refusal = ERTSDeploymentRefusal::MatchResolved;
		return Preview;
	}
	if (Match != nullptr && Match->GetHeadquarters(CommandVehicle.GetGenericTeamId()) != nullptr)
	{
		Preview.Refusal = ERTSDeploymentRefusal::HeadquartersAlreadyDeployed;
		return Preview;
	}

	const TOptional<FRTSStartingZoneSnapshot> StartingZone = FindStartingZone(
		CommandVehicle.GetGenericTeamId());
	if (!StartingZone.IsSet())
	{
		Preview.Refusal = ERTSDeploymentRefusal::NoStartingZone;
		return Preview;
	}

	const FVector2D HalfFootprint(
		Configuration.Headquarters.FootprintCells.X * Configuration.World.PlacementCellSize * 0.5f,
		Configuration.Headquarters.FootprintCells.Y * Configuration.World.PlacementCellSize * 0.5f);
	const float FootprintRadius = HalfFootprint.Size();
	if (FVector::Dist2D(ProposedWorldLocation, StartingZone->Center)
		+ FootprintRadius > StartingZone->Radius)
	{
		Preview.Refusal = ERTSDeploymentRefusal::OutsideStartingZone;
		return Preview;
	}

	FHitResult GroundHit;
	FCollisionQueryParams TraceParameters(SCENE_QUERY_STAT(RTSHeadquartersGround), false);
	TraceParameters.AddIgnoredActor(&CommandVehicle);
	const FVector TraceStart = ProposedWorldLocation + FVector(0.0f, 0.0f, 1500.0f);
	const FVector TraceEnd = ProposedWorldLocation - FVector(0.0f, 0.0f, 2500.0f);
	if (!World->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_Visibility, TraceParameters)
		|| GroundHit.ImpactNormal.Z < FMath::Cos(FMath::DegreesToRadians(
			Configuration.Deployment.MaximumGroundSlopeDegrees)))
	{
		Preview.Refusal = ERTSDeploymentRefusal::InvalidGround;
		return Preview;
	}
	Preview.GroundLocation = GroundHit.ImpactPoint;

	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectTypes.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams OverlapParameters(SCENE_QUERY_STAT(RTSHeadquartersFootprint), false);
	OverlapParameters.AddIgnoredActor(&CommandVehicle);
	TArray<FOverlapResult> Overlaps;
	const FVector FootprintCenter = GroundHit.ImpactPoint + FVector(0.0f, 0.0f, 275.0f);
	const FCollisionShape FootprintShape = FCollisionShape::MakeBox(FVector(HalfFootprint.X, HalfFootprint.Y, 225.0f));
	if (World->OverlapMultiByObjectType(
		Overlaps,
		FootprintCenter,
		FQuat::Identity,
		ObjectTypes,
		FootprintShape,
		OverlapParameters))
	{
		Preview.Refusal = ERTSDeploymentRefusal::FootprintBlocked;
		return Preview;
	}

	Preview.bValid = true;
	Preview.Refusal = ERTSDeploymentRefusal::None;
	return Preview;
}

FRTSHeadquartersDeploymentResult URTSStructureSubsystem::TryDeployHeadquarters(
	ARTSCombatUnit& CommandVehicle)
{
	FRTSHeadquartersDeploymentResult Result;
	const FRTSHeadquartersDeploymentPreview Preview = EvaluateHeadquartersDeployment(CommandVehicle);
	if (!Preview.bValid)
	{
		Result.Refusal = Preview.Refusal;
		return Result;
	}

	UWorld* World = GetWorld();
	URTSEconomySubsystem* Economy = World != nullptr ? World->GetSubsystem<URTSEconomySubsystem>() : nullptr;
	URTSMatchSubsystem* Match = World != nullptr ? World->GetSubsystem<URTSMatchSubsystem>() : nullptr;
	if (World == nullptr || Economy == nullptr || Match == nullptr)
	{
		Result.Refusal = ERTSDeploymentRefusal::SpawnFailed;
		return Result;
	}

	const FRTSMilestone2Configuration Configuration = FRTSMilestone2Configuration::Load();
	const int32 StableStructureId = NextStableStructureId++;
	ARTSStructure* Headquarters = World->SpawnActorDeferred<ARTSStructure>(
		ARTSStructure::StaticClass(),
		FTransform(FRotator::ZeroRotator, Preview.GroundLocation),
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Headquarters == nullptr)
	{
		Result.Refusal = ERTSDeploymentRefusal::SpawnFailed;
		return Result;
	}
	Headquarters->Configure(
		ERTSStructureType::Headquarters,
		StableStructureId,
		CommandVehicle.GetGenericTeamId(),
		Configuration.Headquarters,
		Configuration.World.PlacementCellSize);
	Headquarters->FinishSpawning(FTransform(FRotator::ZeroRotator, Preview.GroundLocation));
	if (!RegisterStructure(*Headquarters, INDEX_NONE))
	{
		Headquarters->Destroy();
		Result.Refusal = ERTSDeploymentRefusal::SpawnFailed;
		return Result;
	}
	if (!Match->PromoteCommandVehicleToHeadquarters(CommandVehicle, *Headquarters))
	{
		Headquarters->Destroy();
		Result.Refusal = ERTSDeploymentRefusal::CommandIdentityRejected;
		return Result;
	}

	if (!CommandVehicle.Destroy())
	{
		Match->RevertHeadquartersPromotion(CommandVehicle, *Headquarters);
		Headquarters->Destroy();
		Result.Refusal = ERTSDeploymentRefusal::CommandIdentityRejected;
		return Result;
	}
	Result.bAccepted = true;
	Result.Refusal = ERTSDeploymentRefusal::None;
	Result.StableStructureId = StableStructureId;
	return Result;
}

FRTSPlacementPreview URTSStructureSubsystem::EvaluatePlacement(
	const FRTSPlacementRequest& Request) const
{
	FRTSPlacementPreview Preview;
	Preview.StructureType = Request.StructureType;
	// A refusal is still spatial feedback. Seed the preview from the request before any legality
	// check so an early return cannot turn a cursor-relative footprint into a box at world origin.
	Preview.SnappedGroundLocation = Request.DesiredWorldLocation;
	if (!IsSupportedTeam(Request.TeamId))
	{
		return Preview;
	}
	if (Request.StructureType == ERTSStructureType::Headquarters)
	{
		Preview.Refusal = ERTSPlacementRefusal::HeadquartersCannotBePlaced;
		return Preview;
	}

	const FRTSMilestone2Configuration Configuration = FRTSMilestone2Configuration::Load();
	const FRTSStructureDefinition* Definition = Configuration.FindStructure(Request.StructureType);
	if (Definition == nullptr || GetWorld() == nullptr)
	{
		Preview.Refusal = ERTSPlacementRefusal::SpawnFailed;
		return Preview;
	}
	Preview.FootprintCells = Definition->FootprintCells;
	Preview.BuildAreaRadius = Definition->BuildAreaRadius;
	const float CellSize = Configuration.World.PlacementCellSize;
	const auto SnapPlanarLocation = [CellSize](const FVector& Location)
	{
		return FVector(
			FMath::RoundToFloat(Location.X / CellSize) * CellSize,
			FMath::RoundToFloat(Location.Y / CellSize) * CellSize,
			Location.Z);
	};
	Preview.SnappedGroundLocation = SnapPlanarLocation(Request.DesiredWorldLocation);
	if (const URTSMatchSubsystem* Match = GetWorld()->GetSubsystem<URTSMatchSubsystem>();
		Match != nullptr && Match->IsResolved())
	{
		Preview.Refusal = ERTSPlacementRefusal::MatchResolved;
		return Preview;
	}

	bool bHasHeadquarters = false;
	for (const TPair<int32, FRegisteredStructure>& Entry : StructuresById)
	{
		const ARTSStructure* Structure = Entry.Value.Actor.Get();
		if (IsValid(Structure)
			&& Structure->IsAlive()
			&& Structure->IsConstructed()
			&& Structure->GetGenericTeamId() == Request.TeamId
			&& Structure->GetStructureType() == ERTSStructureType::Headquarters)
		{
			bHasHeadquarters = true;
			break;
		}
	}
	if (!bHasHeadquarters)
	{
		Preview.Refusal = ERTSPlacementRefusal::HeadquartersRequired;
		return Preview;
	}

	ARTSMaterialDeposit* Deposit = nullptr;
	FVector DesiredLocation = Request.DesiredWorldLocation;
	if (Request.StructureType == ERTSStructureType::MaterialExtractor)
	{
		Deposit = FindDepositForPlacement(DesiredLocation, Definition->FootprintCells);
		if (Deposit == nullptr)
		{
			Preview.Refusal = ERTSPlacementRefusal::DepositRequired;
			return Preview;
		}
		Preview.StableDepositId = Deposit->GetStableDepositId();
		DesiredLocation = Deposit->GetActorLocation();
		Preview.SnappedGroundLocation = SnapPlanarLocation(DesiredLocation);
		if (StructureByDepositId.Contains(Preview.StableDepositId))
		{
			Preview.Refusal = ERTSPlacementRefusal::DepositClaimed;
			return Preview;
		}
	}

	const FVector SnappedPlanarLocation = Preview.SnappedGroundLocation;
	const TArray<FIntPoint> CandidateCells = BuildFootprintCells(
		SnappedPlanarLocation,
		Definition->FootprintCells);
	for (const FIntPoint& Cell : CandidateCells)
	{
		if (OccupancyByCell.Contains(Cell))
		{
			Preview.Refusal = ERTSPlacementRefusal::FootprintBlocked;
			return Preview;
		}
	}
	if (!IsFootprintInsideBuildArea(Request.TeamId, CandidateCells))
	{
		Preview.Refusal = ERTSPlacementRefusal::OutsideBuildArea;
		return Preview;
	}

	FCollisionQueryParams GroundParameters(SCENE_QUERY_STAT(RTSStructureGround), false);
	if (Deposit != nullptr)
	{
		GroundParameters.AddIgnoredActor(Deposit);
	}
	const FVector2D HalfFootprint(
		Definition->FootprintCells.X * CellSize * 0.5f - 5.0f,
		Definition->FootprintCells.Y * CellSize * 0.5f - 5.0f);
	const FVector2D GroundSampleOffsets[] = {
		FVector2D::ZeroVector,
		FVector2D(-HalfFootprint.X, -HalfFootprint.Y),
		FVector2D(HalfFootprint.X, -HalfFootprint.Y),
		FVector2D(HalfFootprint.X, HalfFootprint.Y),
		FVector2D(-HalfFootprint.X, HalfFootprint.Y)};
	float MinimumGroundHeight = TNumericLimits<float>::Max();
	float MaximumGroundHeight = TNumericLimits<float>::Lowest();
	FVector CenterGroundLocation = FVector::ZeroVector;
	const float MinimumGroundNormalZ = FMath::Cos(FMath::DegreesToRadians(
		Configuration.Deployment.MaximumGroundSlopeDegrees));
	for (int32 SampleIndex = 0; SampleIndex < UE_ARRAY_COUNT(GroundSampleOffsets); ++SampleIndex)
	{
		const FVector SamplePlanarLocation(
			SnappedPlanarLocation.X + GroundSampleOffsets[SampleIndex].X,
			SnappedPlanarLocation.Y + GroundSampleOffsets[SampleIndex].Y,
			SnappedPlanarLocation.Z);
		FHitResult GroundHit;
		if (!GetWorld()->LineTraceSingleByChannel(
			GroundHit,
			SamplePlanarLocation + FVector(0.0f, 0.0f, 3000.0f),
			SamplePlanarLocation - FVector(0.0f, 0.0f, 3000.0f),
			ECC_Visibility,
			GroundParameters)
			|| GroundHit.ImpactNormal.Z < MinimumGroundNormalZ)
		{
			Preview.Refusal = ERTSPlacementRefusal::InvalidGround;
			return Preview;
		}
		MinimumGroundHeight = FMath::Min(MinimumGroundHeight, static_cast<float>(GroundHit.ImpactPoint.Z));
		MaximumGroundHeight = FMath::Max(MaximumGroundHeight, static_cast<float>(GroundHit.ImpactPoint.Z));
		if (SampleIndex == 0)
		{
			CenterGroundLocation = GroundHit.ImpactPoint;
		}
	}
	const float MaximumHeightDifference = FMath::Tan(FMath::DegreesToRadians(
		Configuration.Deployment.MaximumGroundSlopeDegrees)) * HalfFootprint.Size() * 2.0f;
	if (MaximumGroundHeight - MinimumGroundHeight > MaximumHeightDifference)
	{
		Preview.Refusal = ERTSPlacementRefusal::InvalidGround;
		return Preview;
	}
	Preview.SnappedGroundLocation = FVector(
		SnappedPlanarLocation.X,
		SnappedPlanarLocation.Y,
		CenterGroundLocation.Z);
	if (HasBlockingOverlap(Preview.SnappedGroundLocation, Definition->FootprintCells, Deposit))
	{
		Preview.Refusal = ERTSPlacementRefusal::FootprintBlocked;
		return Preview;
	}
	if (Request.StructureType == ERTSStructureType::Factory
		&& !HasFactoryExit(
			Preview.SnappedGroundLocation,
			Definition->FootprintCells,
			CandidateCells,
			nullptr))
	{
		Preview.Refusal = ERTSPlacementRefusal::FactoryExitBlocked;
		return Preview;
	}
	for (const TPair<int32, FRegisteredStructure>& Entry : StructuresById)
	{
		const ARTSStructure* ExistingFactory = Entry.Value.Actor.Get();
		if (IsValid(ExistingFactory)
			&& ExistingFactory->IsAlive()
			&& ExistingFactory->GetGenericTeamId() == Request.TeamId
			&& ExistingFactory->GetStructureType() == ERTSStructureType::Factory
			&& !HasFactoryExit(
				ExistingFactory->GetActorLocation(),
				ExistingFactory->GetFootprintCells(),
				CandidateCells,
				ExistingFactory))
		{
			Preview.Refusal = ERTSPlacementRefusal::FactoryExitBlocked;
			return Preview;
		}
	}

	const URTSEconomySubsystem* Economy = GetWorld()->GetSubsystem<URTSEconomySubsystem>();
	if (Economy == nullptr || Economy->GetSnapshot(Request.TeamId).Materials < Definition->MaterialCost)
	{
		Preview.Refusal = ERTSPlacementRefusal::EconomyRejected;
		return Preview;
	}
	Preview.bValid = true;
	Preview.Refusal = ERTSPlacementRefusal::None;
	return Preview;
}

FRTSPlacementResult URTSStructureSubsystem::TryPlace(const FRTSPlacementRequest& Request)
{
	FRTSPlacementResult Result;
	const FRTSPlacementPreview Preview = EvaluatePlacement(Request);
	if (!Preview.bValid)
	{
		Result.Refusal = Preview.Refusal;
		return Result;
	}

	UWorld* World = GetWorld();
	URTSEconomySubsystem* Economy = World != nullptr ? World->GetSubsystem<URTSEconomySubsystem>() : nullptr;
	const FRTSStructureDefinition* Definition = FRTSMilestone2Configuration::Load().FindStructure(Request.StructureType);
	if (World == nullptr || Economy == nullptr || Definition == nullptr)
	{
		Result.Refusal = ERTSPlacementRefusal::SpawnFailed;
		return Result;
	}

	FRTSEconomyDelta Cost;
	Cost.Materials = -Definition->MaterialCost;
	const int64 MaterialTransactionId = Economy->AllocateTransactionId();
	if (!Economy->TryCommit(MaterialTransactionId, Request.TeamId, Cost).bAccepted)
	{
		Result.Refusal = ERTSPlacementRefusal::EconomyRejected;
		return Result;
	}

	const int32 StableStructureId = NextStableStructureId++;
	ARTSStructure* Structure = World->SpawnActorDeferred<ARTSStructure>(
		ARTSStructure::StaticClass(),
		FTransform(FRotator::ZeroRotator, Preview.SnappedGroundLocation),
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Structure == nullptr)
	{
		Economy->TryRollback(MaterialTransactionId);
		Result.Refusal = ERTSPlacementRefusal::SpawnFailed;
		return Result;
	}
	Structure->Configure(
		Request.StructureType,
		StableStructureId,
		Request.TeamId,
		*Definition,
		FRTSMilestone2Configuration::Load().World.PlacementCellSize,
		Preview.StableDepositId);
	Structure->FinishSpawning(FTransform(FRotator::ZeroRotator, Preview.SnappedGroundLocation));
	if (!RegisterStructure(*Structure, Preview.StableDepositId))
	{
		Structure->Destroy();
		Economy->TryRollback(MaterialTransactionId);
		Result.Refusal = ERTSPlacementRefusal::SpawnFailed;
		return Result;
	}

	Result.bAccepted = true;
	Result.Refusal = ERTSPlacementRefusal::None;
	Result.StableStructureId = StableStructureId;
	Result.MaterialTransactionId = MaterialTransactionId;
	return Result;
}

TOptional<FRTSStructureSnapshot> URTSStructureSubsystem::Find(const int32 StableStructureId) const
{
	const ARTSStructure* Structure = FindActor(StableStructureId);
	return IsValid(Structure)
		? TOptional<FRTSStructureSnapshot>(MakeSnapshot(*Structure))
		: TOptional<FRTSStructureSnapshot>();
}

ARTSStructure* URTSStructureSubsystem::FindActor(const int32 StableStructureId) const
{
	const FRegisteredStructure* Registered = StructuresById.Find(StableStructureId);
	ARTSStructure* Structure = Registered != nullptr ? Registered->Actor.Get() : nullptr;
	return IsValid(Structure) ? Structure : nullptr;
}

TArray<FRTSStructureSnapshot> URTSStructureSubsystem::Query(
	const FGenericTeamId TeamId,
	const TOptional<ERTSStructureType> StructureType) const
{
	TArray<FRTSStructureSnapshot> Results;
	for (const TPair<int32, FRegisteredStructure>& Entry : StructuresById)
	{
		const ARTSStructure* Structure = Entry.Value.Actor.Get();
		if (!IsValid(Structure)
			|| Structure->GetGenericTeamId() != TeamId
			|| (StructureType.IsSet() && Structure->GetStructureType() != StructureType.GetValue()))
		{
			continue;
		}
		Results.Add(MakeSnapshot(*Structure));
	}
	Results.Sort([](const FRTSStructureSnapshot& Left, const FRTSStructureSnapshot& Right)
	{
		return Left.StableStructureId < Right.StableStructureId;
	});
	return Results;
}

TOptional<FRTSStartingZoneSnapshot> URTSStructureSubsystem::FindStartingZone(
	const FGenericTeamId TeamId) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return {};
	}
	TArray<const ARTSStartingZone*> MatchingZones;
	for (TActorIterator<ARTSStartingZone> ZoneIterator(World); ZoneIterator; ++ZoneIterator)
	{
		if (ZoneIterator->GetGenericTeamId() == TeamId)
		{
			MatchingZones.Add(*ZoneIterator);
		}
	}
	MatchingZones.Sort([](const ARTSStartingZone& Left, const ARTSStartingZone& Right)
	{
		const FVector LeftLocation = Left.GetActorLocation();
		const FVector RightLocation = Right.GetActorLocation();
		if (!FMath::IsNearlyEqual(LeftLocation.X, RightLocation.X))
		{
			return LeftLocation.X < RightLocation.X;
		}
		return LeftLocation.Y < RightLocation.Y;
	});
	if (MatchingZones.IsEmpty())
	{
		return {};
	}
	FRTSStartingZoneSnapshot Snapshot;
	Snapshot.TeamId = TeamId;
	Snapshot.Center = MatchingZones[0]->GetActorLocation();
	Snapshot.Radius = MatchingZones[0]->GetRadius();
	return Snapshot;
}

TArray<FRTSMaterialDepositSnapshot> URTSStructureSubsystem::QueryMaterialDeposits() const
{
	TArray<FRTSMaterialDepositSnapshot> Results;
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return Results;
	}
	for (TActorIterator<ARTSMaterialDeposit> DepositIterator(World); DepositIterator; ++DepositIterator)
	{
		if (DepositIterator->GetStableDepositId() == INDEX_NONE)
		{
			continue;
		}
		FRTSMaterialDepositSnapshot Snapshot;
		Snapshot.StableDepositId = DepositIterator->GetStableDepositId();
		Snapshot.WorldLocation = DepositIterator->GetActorLocation();
		Snapshot.bClaimed = StructureByDepositId.Contains(Snapshot.StableDepositId);
		Results.Add(Snapshot);
	}
	Results.Sort([](const FRTSMaterialDepositSnapshot& Left, const FRTSMaterialDepositSnapshot& Right)
	{
		return Left.StableDepositId < Right.StableDepositId;
	});
	return Results;
}

void URTSStructureSubsystem::NotifyConstructionCompleted(ARTSStructure& Structure)
{
	if (StructuresById.Contains(Structure.GetStableStructureId()))
	{
		const FRTSStructureSnapshot Snapshot = MakeSnapshot(Structure);
		StructureChanged.Broadcast(Snapshot);
		ConstructionCompleted.Broadcast(Snapshot);
	}
}

void URTSStructureSubsystem::NotifyStructureDestroyed(ARTSStructure& Structure)
{
	if (StructuresById.Contains(Structure.GetStableStructureId()))
	{
		StructureDestroyed.Broadcast(MakeSnapshot(Structure));
	}
}

void URTSStructureSubsystem::UnregisterStructure(ARTSStructure& Structure)
{
	FRegisteredStructure Registered;
	if (!StructuresById.RemoveAndCopyValue(Structure.GetStableStructureId(), Registered))
	{
		return;
	}
	for (const FIntPoint& Cell : Registered.OccupiedCells)
	{
		if (const int32* Occupant = OccupancyByCell.Find(Cell);
			Occupant != nullptr && *Occupant == Structure.GetStableStructureId())
		{
			OccupancyByCell.Remove(Cell);
		}
	}
	if (Registered.StableDepositId != INDEX_NONE)
	{
		StructureByDepositId.Remove(Registered.StableDepositId);
	}
	StructureChanged.Broadcast(MakeSnapshot(Structure));
}

FRTSStructureChanged& URTSStructureSubsystem::OnStructureChanged()
{
	return StructureChanged;
}

FRTSConstructionCompleted& URTSStructureSubsystem::OnConstructionCompleted()
{
	return ConstructionCompleted;
}

FRTSStructureDestroyed& URTSStructureSubsystem::OnStructureDestroyed()
{
	return StructureDestroyed;
}

bool URTSStructureSubsystem::RegisterStructure(
	ARTSStructure& Structure,
	const int32 StableDepositId)
{
	const int32 StableStructureId = Structure.GetStableStructureId();
	if (StableStructureId == INDEX_NONE || StructuresById.Contains(StableStructureId))
	{
		return false;
	}
	const TArray<FIntPoint> OccupiedCells = BuildFootprintCells(
		Structure.GetActorLocation(),
		Structure.GetFootprintCells());
	for (const FIntPoint& Cell : OccupiedCells)
	{
		if (OccupancyByCell.Contains(Cell))
		{
			return false;
		}
	}
	if (StableDepositId != INDEX_NONE && StructureByDepositId.Contains(StableDepositId))
	{
		return false;
	}

	FRegisteredStructure Registered;
	Registered.Actor = &Structure;
	Registered.OccupiedCells = OccupiedCells;
	Registered.StableDepositId = StableDepositId;
	StructuresById.Add(StableStructureId, MoveTemp(Registered));
	for (const FIntPoint& Cell : OccupiedCells)
	{
		OccupancyByCell.Add(Cell, StableStructureId);
	}
	if (StableDepositId != INDEX_NONE)
	{
		StructureByDepositId.Add(StableDepositId, StableStructureId);
	}
	StructureChanged.Broadcast(MakeSnapshot(Structure));
	return true;
}

TArray<FIntPoint> URTSStructureSubsystem::BuildFootprintCells(
	const FVector& SnappedLocation,
	const FIntPoint& FootprintCells) const
{
	const float CellSize = FRTSMilestone2Configuration::Load().World.PlacementCellSize;
	const FIntPoint CenterCell(
		FMath::RoundToInt(SnappedLocation.X / CellSize),
		FMath::RoundToInt(SnappedLocation.Y / CellSize));
	const FIntPoint MinimumCell(
		CenterCell.X - FootprintCells.X / 2,
		CenterCell.Y - FootprintCells.Y / 2);
	TArray<FIntPoint> Cells;
	Cells.Reserve(FootprintCells.X * FootprintCells.Y);
	for (int32 X = 0; X < FootprintCells.X; ++X)
	{
		for (int32 Y = 0; Y < FootprintCells.Y; ++Y)
		{
			Cells.Add(FIntPoint(MinimumCell.X + X, MinimumCell.Y + Y));
		}
	}
	return Cells;
}

bool URTSStructureSubsystem::IsFootprintInsideBuildArea(
	const FGenericTeamId TeamId,
	const TConstArrayView<FIntPoint> FootprintCells) const
{
	const float CellSize = FRTSMilestone2Configuration::Load().World.PlacementCellSize;
	TArray<TPair<FVector2D, float>> BuildAreas;
	for (const TPair<int32, FRegisteredStructure>& Entry : StructuresById)
	{
		const ARTSStructure* Structure = Entry.Value.Actor.Get();
		if (IsValid(Structure)
			&& Structure->IsAlive()
			&& Structure->IsConstructed()
			&& Structure->GetGenericTeamId() == TeamId
			&& Structure->GetBuildAreaRadius() > 0.0f)
		{
			BuildAreas.Emplace(FVector2D(Structure->GetActorLocation()), Structure->GetBuildAreaRadius());
		}
	}
	if (BuildAreas.IsEmpty())
	{
		return false;
	}

	for (const FIntPoint& Cell : FootprintCells)
	{
		const FVector2D Minimum(Cell.X * CellSize, Cell.Y * CellSize);
		const FVector2D Corners[] = {
			Minimum,
			Minimum + FVector2D(CellSize, 0.0f),
			Minimum + FVector2D(0.0f, CellSize),
			Minimum + FVector2D(CellSize, CellSize)};
		for (const FVector2D& Corner : Corners)
		{
			const bool bCovered = BuildAreas.ContainsByPredicate(
				[Corner](const TPair<FVector2D, float>& Area)
				{
					return FVector2D::DistSquared(Corner, Area.Key) <= FMath::Square(Area.Value);
				});
			if (!bCovered)
			{
				return false;
			}
		}
	}
	return true;
}

bool URTSStructureSubsystem::HasBlockingOverlap(
	const FVector& GroundLocation,
	const FIntPoint& FootprintCells,
	const AActor* IgnoredActor) const
{
	if (GetWorld() == nullptr)
	{
		return true;
	}
	const float CellSize = FRTSMilestone2Configuration::Load().World.PlacementCellSize;
	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectTypes.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Parameters(SCENE_QUERY_STAT(RTSStructureFootprint), false);
	if (IgnoredActor != nullptr)
	{
		Parameters.AddIgnoredActor(IgnoredActor);
	}
	TArray<FOverlapResult> Overlaps;
	return GetWorld()->OverlapMultiByObjectType(
		Overlaps,
		GroundLocation + FVector(0.0f, 0.0f, 275.0f),
		FQuat::Identity,
		ObjectTypes,
		FCollisionShape::MakeBox(FVector(
			FootprintCells.X * CellSize * 0.5f - 2.0f,
			FootprintCells.Y * CellSize * 0.5f - 2.0f,
			225.0f)),
		Parameters);
}

ARTSMaterialDeposit* URTSStructureSubsystem::FindDepositForPlacement(
	const FVector& DesiredLocation,
	const FIntPoint& FootprintCells) const
{
	if (GetWorld() == nullptr)
	{
		return nullptr;
	}
	const float CellSize = FRTSMilestone2Configuration::Load().World.PlacementCellSize;
	const float MaximumDistanceSquared = FMath::Square(
		FMath::Min(FootprintCells.X, FootprintCells.Y) * CellSize * 0.5f);
	ARTSMaterialDeposit* Closest = nullptr;
	float ClosestDistanceSquared = MaximumDistanceSquared;
	for (TActorIterator<ARTSMaterialDeposit> DepositIterator(GetWorld()); DepositIterator; ++DepositIterator)
	{
		const float DistanceSquared = FVector::DistSquared2D(
			DesiredLocation,
			DepositIterator->GetActorLocation());
		if (DistanceSquared < ClosestDistanceSquared
			|| (FMath::IsNearlyEqual(DistanceSquared, ClosestDistanceSquared)
				&& Closest != nullptr
				&& DepositIterator->GetStableDepositId() < Closest->GetStableDepositId()))
		{
			Closest = *DepositIterator;
			ClosestDistanceSquared = DistanceSquared;
		}
	}
	return Closest;
}

bool URTSStructureSubsystem::HasFactoryExit(
	const FVector& GroundLocation,
	const FIntPoint& FootprintCells,
	const TConstArrayView<FIntPoint> AdditionalOccupiedCells,
	const AActor* IgnoredFactory) const
{
	return FindFactoryExit(GroundLocation, FootprintCells, AdditionalOccupiedCells, IgnoredFactory).IsSet();
}

TOptional<FVector> URTSStructureSubsystem::FindFactorySpawnLocation(const ARTSStructure& Factory) const
{
	if (!Factory.IsAlive()
		|| !Factory.IsConstructed()
		|| Factory.GetStructureType() != ERTSStructureType::Factory)
	{
		return {};
	}
	return FindFactoryExit(
		Factory.GetActorLocation(),
		Factory.GetFootprintCells(),
		TConstArrayView<FIntPoint>(),
		&Factory);
}

int32 URTSStructureSubsystem::AllocateProducedUnitId()
{
	if (NextProducedUnitId == INDEX_NONE)
	{
		int32 MaximumExistingId = 0;
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
			{
				MaximumExistingId = FMath::Max(MaximumExistingId, UnitIterator->GetStableUnitId());
			}
		}
		NextProducedUnitId = MaximumExistingId + 1;
	}
	return NextProducedUnitId++;
}

TOptional<FVector> URTSStructureSubsystem::FindFactoryExit(
	const FVector& GroundLocation,
	const FIntPoint& FootprintCells,
	const TConstArrayView<FIntPoint> AdditionalOccupiedCells,
	const AActor* IgnoredFactory) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Navigation = World != nullptr
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	if (World == nullptr || Navigation == nullptr)
	{
		return {};
	}
	const FRTSMilestone2Configuration Configuration = FRTSMilestone2Configuration::Load();
	const float CellSize = Configuration.World.PlacementCellSize;
	const FVector2D HalfFootprint(
		FootprintCells.X * CellSize * 0.5f,
		FootprintCells.Y * CellSize * 0.5f);
	const float ExitDepth = Configuration.Production.FactoryExitDepth;
	const FVector ExitOffsets[] = {
		FVector(HalfFootprint.X + ExitDepth * 0.5f, 0.0f, 0.0f),
		FVector(-HalfFootprint.X - ExitDepth * 0.5f, 0.0f, 0.0f),
		FVector(0.0f, HalfFootprint.Y + ExitDepth * 0.5f, 0.0f),
		FVector(0.0f, -HalfFootprint.Y - ExitDepth * 0.5f, 0.0f)};
	const FVector ExitExtents[] = {
		FVector(ExitDepth * 0.5f, HalfFootprint.Y, 200.0f),
		FVector(ExitDepth * 0.5f, HalfFootprint.Y, 200.0f),
		FVector(HalfFootprint.X, ExitDepth * 0.5f, 200.0f),
		FVector(HalfFootprint.X, ExitDepth * 0.5f, 200.0f)};
	const int32 ExitDepthCells = FMath::Max(1, FMath::RoundToInt(ExitDepth / CellSize));
	const FIntPoint ExitCellFootprints[] = {
		FIntPoint(ExitDepthCells, FootprintCells.Y),
		FIntPoint(ExitDepthCells, FootprintCells.Y),
		FIntPoint(FootprintCells.X, ExitDepthCells),
		FIntPoint(FootprintCells.X, ExitDepthCells)};
	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectTypes.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams QueryParameters(SCENE_QUERY_STAT(RTSFactoryExit), false);
	if (IgnoredFactory != nullptr)
	{
		QueryParameters.AddIgnoredActor(IgnoredFactory);
	}
	for (int32 ExitIndex = 0; ExitIndex < UE_ARRAY_COUNT(ExitOffsets); ++ExitIndex)
	{
		const FVector ExitLocation = GroundLocation + ExitOffsets[ExitIndex];
		const TArray<FIntPoint> ExitCells = BuildFootprintCells(
			ExitLocation,
			ExitCellFootprints[ExitIndex]);
		const bool bCellsBlocked = ExitCells.ContainsByPredicate(
			[this, AdditionalOccupiedCells](const FIntPoint& Cell)
			{
				return OccupancyByCell.Contains(Cell) || AdditionalOccupiedCells.Contains(Cell);
			});
		if (bCellsBlocked)
		{
			continue;
		}
		FNavLocation ProjectedLocation;
		if (!Navigation->ProjectPointToNavigation(
			ExitLocation,
			ProjectedLocation,
			FVector(ExitExtents[ExitIndex].X, ExitExtents[ExitIndex].Y, 300.0f)))
		{
			continue;
		}
		TArray<FOverlapResult> Overlaps;
		if (!World->OverlapMultiByObjectType(
			Overlaps,
			ExitLocation + FVector(0.0f, 0.0f, 225.0f),
			FQuat::Identity,
			ObjectTypes,
			FCollisionShape::MakeBox(ExitExtents[ExitIndex] - FVector(2.0f, 2.0f, 0.0f)),
			QueryParameters))
		{
			return ProjectedLocation.Location;
		}
	}
	return {};
}

FRTSStructureSnapshot URTSStructureSubsystem::MakeSnapshot(const ARTSStructure& Structure) const
{
	FRTSStructureSnapshot Snapshot;
	Snapshot.StableStructureId = Structure.GetStableStructureId();
	Snapshot.StructureType = Structure.GetStructureType();
	Snapshot.TeamId = Structure.GetGenericTeamId();
	Snapshot.WorldLocation = Structure.GetActorLocation();
	Snapshot.FootprintCells = Structure.GetFootprintCells();
	Snapshot.bAlive = Structure.IsAlive();
	Snapshot.bConstructed = Structure.IsConstructed();
	Snapshot.ConstructionProgress = Structure.GetConstructionProgress();
	return Snapshot;
}

bool URTSStructureSubsystem::IsSupportedTeam(const FGenericTeamId TeamId)
{
	return TeamId == RTSTeams::Player || TeamId == RTSTeams::Enemy;
}
