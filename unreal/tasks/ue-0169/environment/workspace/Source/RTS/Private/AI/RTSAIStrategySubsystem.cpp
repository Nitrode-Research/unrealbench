// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/RTSAIStrategySubsystem.h"

#include "Configuration/RTSMilestone2Configuration.h"
#include "Economy/RTSEconomySubsystem.h"
#include "Match/RTSMatchSubsystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Orders/RTSCommandSubsystem.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Production/RTSProductionSubsystem.h"
#include "Reclaim/RTSWreckage.h"
#include "Reclaim/RTSWreckageSubsystem.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSUnitRegistrySubsystem.h"
#include "Units/RTSTeams.h"

namespace
{
struct FRTSRouteMeasurement
{
	bool bComplete = false;
	float Length = 0.0f;
};

FRTSRouteMeasurement MeasureCompleteRoute(
	UWorld& World,
	const FVector& Start,
	const FVector& End)
{
	FRTSRouteMeasurement Measurement;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
	ANavigationData* NavigationData = Navigation != nullptr
		? Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)
		: nullptr;
	if (Navigation == nullptr || NavigationData == nullptr)
	{
		return Measurement;
	}
	FNavLocation ProjectedStart;
	FNavLocation ProjectedEnd;
	const FVector ProjectionExtent(500.0f, 500.0f, 500.0f);
	if (!Navigation->ProjectPointToNavigation(Start, ProjectedStart, ProjectionExtent, NavigationData)
		|| !Navigation->ProjectPointToNavigation(End, ProjectedEnd, ProjectionExtent, NavigationData))
	{
		return Measurement;
	}
	FPathFindingQuery Query(nullptr, *NavigationData, ProjectedStart.Location, ProjectedEnd.Location);
	Query.SetAllowPartialPaths(false);
	const FPathFindingResult PathResult = Navigation->FindPathSync(Query);
	if (!PathResult.IsSuccessful() || PathResult.IsPartial() || !PathResult.Path.IsValid())
	{
		return Measurement;
	}
	Measurement.bComplete = true;
	Measurement.Length = PathResult.Path->GetLength();
	return Measurement;
}

bool HasCompleteRouteWithinRange(
	UWorld& World,
	const FVector& Start,
	const FVector& Target,
	const float AcceptanceRange)
{
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
	ANavigationData* NavigationData = Navigation != nullptr
		? Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)
		: nullptr;
	if (Navigation == nullptr || NavigationData == nullptr)
	{
		return false;
	}
	FNavLocation ProjectedStart;
	FNavLocation ProjectedTarget;
	if (!Navigation->ProjectPointToNavigation(
			Start,
			ProjectedStart,
			FVector(500.0f, 500.0f, 500.0f),
			NavigationData)
		|| !Navigation->ProjectPointToNavigation(
			Target,
			ProjectedTarget,
			FVector(AcceptanceRange, AcceptanceRange, 500.0f),
			NavigationData)
		|| FVector::DistSquared2D(ProjectedTarget.Location, Target) > FMath::Square(AcceptanceRange))
	{
		return false;
	}
	FPathFindingQuery Query(nullptr, *NavigationData, ProjectedStart.Location, ProjectedTarget.Location);
	Query.SetAllowPartialPaths(false);
	const FPathFindingResult PathResult = Navigation->FindPathSync(Query);
	return PathResult.IsSuccessful() && !PathResult.IsPartial();
}
}

void URTSAIStrategySubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		for (TPair<uint8, FRuntimeState>& Entry : RuntimeStatesByTeam)
		{
			World->GetTimerManager().ClearTimer(Entry.Value.DecisionTimer);
		}
	}
	RuntimeStatesByTeam.Reset();
	Super::Deinitialize();
}

FRTSAIStartResult URTSAIStrategySubsystem::StartFaction(
	const FGenericTeamId TeamId,
	const FRTSAIProfile& Profile)
{
	FRTSAIStartResult Result;
	if (!IsSupportedTeam(TeamId))
	{
		Result.Refusal = ERTSAIRefusal::UnsupportedTeam;
		return Result;
	}
	if (RuntimeStatesByTeam.Contains(TeamId.GetId()))
	{
		Result.Refusal = ERTSAIRefusal::AlreadyStarted;
		Result.Snapshot = RuntimeStatesByTeam.FindChecked(TeamId.GetId()).Snapshot;
		return Result;
	}
	TArray<FString> ProfileErrors;
	if (!Profile.Validate(TEXT("AI strategy profile"), ProfileErrors))
	{
		Result.Refusal = ERTSAIRefusal::InvalidProfile;
		return Result;
	}

	FRuntimeState State;
	State.Profile = Profile;
	State.Snapshot.TeamId = TeamId;
	State.Snapshot.Phase = ERTSAIStrategyPhase::Observing;
	State.Snapshot.HeadquartersPhase = ERTSAIHeadquartersPhase::ChoosingSite;
	State.Snapshot.bRunning = true;
	RuntimeStatesByTeam.Add(TeamId.GetId(), MoveTemp(State));

	EvaluateFaction(TeamId.GetId());
	if (UWorld* World = GetWorld())
	{
		FRuntimeState& StoredState = RuntimeStatesByTeam.FindChecked(TeamId.GetId());
		FTimerDelegate DecisionDelegate;
		DecisionDelegate.BindUObject(this, &URTSAIStrategySubsystem::EvaluateFaction, TeamId.GetId());
		World->GetTimerManager().SetTimer(
			StoredState.DecisionTimer,
			DecisionDelegate,
			Profile.DecisionIntervalSeconds,
			true);
	}

	Result.bAccepted = true;
	Result.Snapshot = RuntimeStatesByTeam.FindChecked(TeamId.GetId()).Snapshot;
	Result.Refusal = Result.Snapshot.LastRefusal;
	return Result;
}

void URTSAIStrategySubsystem::StopFaction(const FGenericTeamId TeamId)
{
	FRuntimeState State;
	if (!RuntimeStatesByTeam.RemoveAndCopyValue(TeamId.GetId(), State))
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(State.DecisionTimer);
	}
}

FRTSAISnapshot URTSAIStrategySubsystem::GetSnapshot(const FGenericTeamId TeamId) const
{
	const FRuntimeState* State = RuntimeStatesByTeam.Find(TeamId.GetId());
	if (State != nullptr)
	{
		return State->Snapshot;
	}
	FRTSAISnapshot Snapshot;
	Snapshot.TeamId = TeamId;
	return Snapshot;
}

FRTSHQCandidateSearchResult URTSAIStrategySubsystem::EvaluateHeadquartersCandidates(
	const ARTSCombatUnit& CommandVehicle,
	const FRTSAIProfile& Profile,
	const TConstArrayView<int32> RetiredCandidateIndices) const
{
	FRTSHQCandidateSearchResult Result;
	UWorld* World = GetWorld();
	const URTSStructureSubsystem* Structures = World != nullptr
		? World->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	if (World == nullptr || Structures == nullptr)
	{
		return Result;
	}
	const TOptional<FRTSStartingZoneSnapshot> StartingZone = Structures->FindStartingZone(
		CommandVehicle.GetGenericTeamId());
	const FGenericTeamId OpponentTeam = CommandVehicle.GetGenericTeamId() == RTSTeams::Player
		? RTSTeams::Enemy
		: RTSTeams::Player;
	const TOptional<FRTSStartingZoneSnapshot> OpponentZone = Structures->FindStartingZone(OpponentTeam);
	if (!StartingZone.IsSet() || !OpponentZone.IsSet())
	{
		return Result;
	}
	const TArray<FRTSMaterialDepositSnapshot> Deposits = Structures->QueryMaterialDeposits();
	const TSet<int32> RetiredCandidates(RetiredCandidateIndices);
	const int32 GridRadius = FMath::CeilToInt(
		StartingZone->Radius / Profile.HeadquartersCandidateGridSpacing);
	const FVector ContestedCenter = (StartingZone->Center + OpponentZone->Center) * 0.5f;
	const float MaximumStandoffDistance = FMath::Max(
		1.0f,
		FVector::Dist2D(StartingZone->Center, ContestedCenter) + StartingZone->Radius);
	const FVector2D BuildProbeDirections[] = {
		FVector2D(-1.0f, -1.0f).GetSafeNormal(),
		FVector2D(0.0f, -1.0f),
		FVector2D(1.0f, -1.0f).GetSafeNormal(),
		FVector2D(-1.0f, 0.0f),
		FVector2D(1.0f, 0.0f),
		FVector2D(-1.0f, 1.0f).GetSafeNormal(),
		FVector2D(0.0f, 1.0f),
		FVector2D(1.0f, 1.0f).GetSafeNormal()};

	int32 CandidateIndex = 0;
	for (int32 GridY = -GridRadius; GridY <= GridRadius; ++GridY)
	{
		for (int32 GridX = -GridRadius; GridX <= GridRadius; ++GridX, ++CandidateIndex)
		{
			FRTSHQCandidateReceipt Candidate;
			Candidate.CandidateIndex = CandidateIndex;
			Candidate.GroundLocation = StartingZone->Center + FVector(
				GridX * Profile.HeadquartersCandidateGridSpacing,
				GridY * Profile.HeadquartersCandidateGridSpacing,
				0.0f);
			if (RetiredCandidates.Contains(CandidateIndex))
			{
				Candidate.Refusal = ERTSAIHeadquartersCandidateRefusal::Retired;
				Result.Candidates.Add(Candidate);
				continue;
			}

			const FRTSHeadquartersDeploymentPreview Preview =
				Structures->EvaluateHeadquartersDeploymentAt(CommandVehicle, Candidate.GroundLocation);
			Candidate.GroundLocation = Preview.GroundLocation;
			Candidate.DeploymentRefusal = Preview.Refusal;
			if (!Preview.bValid)
			{
				Candidate.Refusal = ERTSAIHeadquartersCandidateRefusal::DeploymentRejected;
				Result.Candidates.Add(Candidate);
				continue;
			}

			const FRTSRouteMeasurement TravelRoute = MeasureCompleteRoute(
				*World,
				CommandVehicle.GetActorLocation(),
				Candidate.GroundLocation);
			if (!TravelRoute.bComplete)
			{
				Candidate.Refusal = ERTSAIHeadquartersCandidateRefusal::TravelRouteUnavailable;
				Result.Candidates.Add(Candidate);
				continue;
			}
			const FRTSRouteMeasurement ApproachRoute = MeasureCompleteRoute(
				*World,
				Candidate.GroundLocation,
				OpponentZone->Center);
			if (!ApproachRoute.bComplete)
			{
				Candidate.Refusal = ERTSAIHeadquartersCandidateRefusal::ApproachRouteUnavailable;
				Result.Candidates.Add(Candidate);
				continue;
			}

			float MaterialQuality = 0.0f;
			for (const FRTSMaterialDepositSnapshot& Deposit : Deposits)
			{
				if (Deposit.bClaimed)
				{
					continue;
				}
				const float Distance = FVector::Dist2D(Candidate.GroundLocation, Deposit.WorldLocation);
				if (Distance < Profile.HeadquartersMaterialInfluenceRadius)
				{
					++Candidate.NearbyMaterialCount;
					MaterialQuality += 1.0f - Distance / Profile.HeadquartersMaterialInfluenceRadius;
				}
			}
			for (const FVector2D& Direction : BuildProbeDirections)
			{
				const FVector ProbeLocation = Candidate.GroundLocation + FVector(
					Direction.X * Profile.HeadquartersBuildProbeDistance,
					Direction.Y * Profile.HeadquartersBuildProbeDistance,
					0.0f);
				Candidate.UsableBuildProbeCount += Structures->EvaluateHeadquartersDeploymentAt(
					CommandVehicle,
					ProbeLocation).bValid ? 1 : 0;
			}

			Candidate.MaterialProximityScore = MaterialQuality * Profile.MaterialProximityWeight;
			Candidate.BuildSpaceScore =
				static_cast<float>(Candidate.UsableBuildProbeCount) / UE_ARRAY_COUNT(BuildProbeDirections)
				* Profile.BuildSpaceWeight;
			Candidate.ApproachStandoffScore = FMath::Clamp(
				FVector::Dist2D(Candidate.GroundLocation, ContestedCenter) / MaximumStandoffDistance,
				0.0f,
				1.0f) * Profile.ApproachStandoffWeight;
			const float DirectApproachDistance = FVector::Dist2D(
				Candidate.GroundLocation,
				OpponentZone->Center);
			Candidate.RouteLength = ApproachRoute.Length;
			Candidate.RouteAccessScore = FMath::Clamp(
				DirectApproachDistance / FMath::Max(DirectApproachDistance, ApproachRoute.Length),
				0.0f,
				1.0f) * Profile.RouteAccessWeight;
			Candidate.TotalScore = Profile.DeployCommandStructureScore
				+ Candidate.MaterialProximityScore
				+ Candidate.BuildSpaceScore
				+ Candidate.ApproachStandoffScore
				+ Candidate.RouteAccessScore;
			Candidate.bSelectable = true;
			Result.Candidates.Add(Candidate);
			if (!Result.bFound
				|| Candidate.TotalScore > Result.Selected.TotalScore + UE_KINDA_SMALL_NUMBER
				|| (FMath::IsNearlyEqual(Candidate.TotalScore, Result.Selected.TotalScore)
					&& Candidate.CandidateIndex < Result.Selected.CandidateIndex))
			{
				Result.bFound = true;
				Result.Selected = Candidate;
			}
		}
	}
	return Result;
}

FRTSStructureCandidateSearchResult URTSAIStrategySubsystem::EvaluateStructureCandidates(
	const FGenericTeamId TeamId,
	const ERTSStructureType StructureType,
	const FRTSAIProfile& Profile) const
{
	FRTSStructureCandidateSearchResult Result;
	const URTSStructureSubsystem* Structures = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	if (Structures == nullptr || StructureType == ERTSStructureType::Headquarters)
	{
		return Result;
	}
	const TArray<FRTSStructureSnapshot> Headquarters = Structures->Query(
		TeamId,
		ERTSStructureType::Headquarters);
	if (Headquarters.IsEmpty() || !Headquarters[0].bConstructed)
	{
		return Result;
	}
	const FVector HeadquartersLocation = Headquarters[0].WorldLocation;
	const auto ConsiderCandidate = [&Result, &HeadquartersLocation](
		const int32 CandidateIndex,
		const FRTSPlacementPreview& Preview)
	{
		FRTSStructureCandidateReceipt Candidate;
		Candidate.CandidateIndex = CandidateIndex;
		Candidate.GroundLocation = Preview.SnappedGroundLocation;
		Candidate.bSelectable = Preview.bValid;
		Candidate.Refusal = Preview.Refusal;
		Candidate.StableDepositId = Preview.StableDepositId;
		Candidate.DistanceScore = -FVector::DistSquared2D(
			HeadquartersLocation,
			Preview.SnappedGroundLocation);
		Result.Candidates.Add(Candidate);
		if (Candidate.bSelectable
			&& (!Result.bFound
				|| Candidate.DistanceScore > Result.Selected.DistanceScore + UE_KINDA_SMALL_NUMBER
				|| (FMath::IsNearlyEqual(Candidate.DistanceScore, Result.Selected.DistanceScore)
					&& Candidate.CandidateIndex < Result.Selected.CandidateIndex)))
		{
			Result.bFound = true;
			Result.Selected = Candidate;
		}
	};

	if (StructureType == ERTSStructureType::MaterialExtractor)
	{
		for (const FRTSMaterialDepositSnapshot& Deposit : Structures->QueryMaterialDeposits())
		{
			if (Deposit.bClaimed)
			{
				continue;
			}
			FRTSPlacementRequest Request;
			Request.TeamId = TeamId;
			Request.StructureType = StructureType;
			Request.DesiredWorldLocation = Deposit.WorldLocation;
			ConsiderCandidate(
				Deposit.StableDepositId,
				Structures->EvaluatePlacement(Request));
		}
		return Result;
	}

	const int32 GridRadius = FMath::FloorToInt(
		Profile.StructureCandidateRadius / Profile.StructureCandidateGridSpacing);
	int32 CandidateIndex = 0;
	for (int32 GridY = -GridRadius; GridY <= GridRadius; ++GridY)
	{
		for (int32 GridX = -GridRadius; GridX <= GridRadius; ++GridX, ++CandidateIndex)
		{
			const FVector Offset(
				GridX * Profile.StructureCandidateGridSpacing,
				GridY * Profile.StructureCandidateGridSpacing,
				0.0f);
			if (Offset.SizeSquared2D() > FMath::Square(Profile.StructureCandidateRadius))
			{
				continue;
			}
			FRTSPlacementRequest Request;
			Request.TeamId = TeamId;
			Request.StructureType = StructureType;
			Request.DesiredWorldLocation = HeadquartersLocation + Offset;
			ConsiderCandidate(CandidateIndex, Structures->EvaluatePlacement(Request));
		}
	}
	return Result;
}

FRTSStructureCandidateSearchResult URTSAIStrategySubsystem::EvaluateTurretCandidates(
	const FGenericTeamId TeamId,
	const FRTSAIProfile& Profile) const
{
	FRTSStructureCandidateSearchResult Result;
	const URTSStructureSubsystem* Structures = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	if (Structures == nullptr)
	{
		return Result;
	}
	const TArray<FRTSStructureSnapshot> FriendlyStructures = Structures->Query(TeamId);
	TArray<FRTSStructureSnapshot> ProtectionAnchors;
	TArray<FRTSStructureSnapshot> ExistingTurrets;
	for (const FRTSStructureSnapshot& Structure : FriendlyStructures)
	{
		if (!Structure.bAlive)
		{
			continue;
		}
		if (Structure.StructureType == ERTSStructureType::DefensiveTurret)
		{
			ExistingTurrets.Add(Structure);
		}
		else if (Structure.bConstructed
			&& (Structure.StructureType == ERTSStructureType::Headquarters
				|| Structure.StructureType == ERTSStructureType::Factory
				|| Structure.StructureType == ERTSStructureType::MaterialExtractor))
		{
			ProtectionAnchors.Add(Structure);
		}
	}
	const FGenericTeamId OpponentTeam = TeamId == RTSTeams::Enemy ? RTSTeams::Player : RTSTeams::Enemy;
	const TOptional<FRTSStartingZoneSnapshot> OpponentZone = Structures->FindStartingZone(OpponentTeam);
	const FVector ApproachLocation = OpponentZone.IsSet() ? OpponentZone->Center : FVector::ZeroVector;
	const FVector2D Directions[] = {
		FVector2D(1.0f, 0.0f),
		FVector2D(1.0f, 1.0f).GetSafeNormal(),
		FVector2D(0.0f, 1.0f),
		FVector2D(-1.0f, 1.0f).GetSafeNormal(),
		FVector2D(-1.0f, 0.0f),
		FVector2D(-1.0f, -1.0f).GetSafeNormal(),
		FVector2D(0.0f, -1.0f),
		FVector2D(1.0f, -1.0f).GetSafeNormal()};
	TSet<FIntPoint> SeenCells;
	int32 CandidateIndex = 0;
	for (const FRTSStructureSnapshot& Anchor : ProtectionAnchors)
	{
		for (const FVector2D& Direction : Directions)
		{
			FRTSPlacementRequest Request;
			Request.TeamId = TeamId;
			Request.StructureType = ERTSStructureType::DefensiveTurret;
			Request.DesiredWorldLocation = Anchor.WorldLocation + FVector(
				Direction.X * Profile.TurretAnchorRadius,
				Direction.Y * Profile.TurretAnchorRadius,
				0.0f);
			const FRTSPlacementPreview Preview = Structures->EvaluatePlacement(Request);
			const FIntPoint CandidateCell(
				FMath::RoundToInt(Preview.SnappedGroundLocation.X / Profile.StructureCandidateGridSpacing),
				FMath::RoundToInt(Preview.SnappedGroundLocation.Y / Profile.StructureCandidateGridSpacing));
			if (SeenCells.Contains(CandidateCell))
			{
				++CandidateIndex;
				continue;
			}
			SeenCells.Add(CandidateCell);

			FRTSStructureCandidateReceipt Candidate;
			Candidate.CandidateIndex = CandidateIndex++;
			Candidate.GroundLocation = Preview.SnappedGroundLocation;
			Candidate.Refusal = Preview.Refusal;
			Candidate.bSelectable = Preview.bValid;
			for (const FRTSStructureSnapshot& ExistingTurret : ExistingTurrets)
			{
				if (FVector::DistSquared2D(Candidate.GroundLocation, ExistingTurret.WorldLocation)
					< FMath::Square(Profile.MinimumTurretSpacing))
				{
					Candidate.bSelectable = false;
					Candidate.PolicyRefusal = ERTSAIStructureCandidateRefusal::MinimumTurretSpacing;
					break;
				}
			}
			const float ProtectionDistanceSquared = FVector::DistSquared2D(
				Candidate.GroundLocation,
				Anchor.WorldLocation);
			const float ApproachDistanceSquared = FVector::DistSquared2D(
				Candidate.GroundLocation,
				ApproachLocation);
			Candidate.DistanceScore = -ProtectionDistanceSquared - ApproachDistanceSquared * 0.05f;
			Result.Candidates.Add(Candidate);
			if (Candidate.bSelectable
				&& (!Result.bFound
					|| Candidate.DistanceScore > Result.Selected.DistanceScore + UE_KINDA_SMALL_NUMBER
					|| (FMath::IsNearlyEqual(Candidate.DistanceScore, Result.Selected.DistanceScore)
						&& Candidate.CandidateIndex < Result.Selected.CandidateIndex)))
			{
				Result.bFound = true;
				Result.Selected = Candidate;
			}
		}
	}
	return Result;
}

void URTSAIStrategySubsystem::EvaluateFaction(const uint8 TeamValue)
{
	FRuntimeState* State = RuntimeStatesByTeam.Find(TeamValue);
	if (State == nullptr)
	{
		return;
	}
	FRTSAISnapshot& Snapshot = State->Snapshot;
	++Snapshot.DecisionSequence;
	const UWorld* World = GetWorld();
	Snapshot.NextReconsiderationTimeSeconds = (World != nullptr ? World->GetTimeSeconds() : 0.0)
		+ State->Profile.DecisionIntervalSeconds;
	const URTSMatchSubsystem* Match = World != nullptr
		? World->GetSubsystem<URTSMatchSubsystem>()
		: nullptr;
	if (Match != nullptr && Match->IsResolved())
	{
		Snapshot.LastRefusal = ERTSAIRefusal::MatchResolved;
		Snapshot.CurrentAction = ERTSAIAction::None;
		Snapshot.Phase = ERTSAIStrategyPhase::Observing;
		return;
	}
	URTSStructureSubsystem* Structures = World != nullptr
		? World->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	if (Structures != nullptr
		&& !Structures->Query(Snapshot.TeamId, ERTSStructureType::Headquarters).IsEmpty())
	{
		MarkHeadquartersDeployed(*State);
		if (EvaluateDefense(*State))
		{
			return;
		}
		MaintainAttackCommitment(*State);
		if (EvaluateEconomyAndProduction(*State))
		{
			return;
		}
		if (EnsureDefensiveTurrets(*State))
		{
			return;
		}
		if (TryAssignSafeReclaim(*State))
		{
			return;
		}
		UpdateOrLaunchAttackGroup(*State);
		return;
	}
	URTSUnitRegistrySubsystem* Units = World != nullptr
		? World->GetSubsystem<URTSUnitRegistrySubsystem>()
		: nullptr;
	const TArray<FRTSUnitSnapshot> CommandVehicleSnapshots = Units != nullptr
		? Units->Query(Snapshot.TeamId, ERTSUnitType::CommandVehicle)
		: TArray<FRTSUnitSnapshot>();
	ARTSCombatUnit* CommandVehicle = !CommandVehicleSnapshots.IsEmpty() && Units != nullptr
		? Units->FindActor(CommandVehicleSnapshots[0].StableUnitId)
		: nullptr;
	if (!IsValid(CommandVehicle))
	{
		Snapshot.LastRefusal = ERTSAIRefusal::CommandVehicleUnavailable;
		return;
	}
	Snapshot.ObjectiveStableId = CommandVehicle->GetStableUnitId();
	if (Snapshot.HeadquartersPhase == ERTSAIHeadquartersPhase::Traveling)
	{
		UpdateDeploymentTravel(*State, *CommandVehicle);
		return;
	}
	if (Snapshot.HeadquartersPhase == ERTSAIHeadquartersPhase::RetryLimitReached)
	{
		return;
	}
	ChooseAndIssueDeploymentMove(*State, *CommandVehicle);
}

bool URTSAIStrategySubsystem::ChooseAndIssueDeploymentMove(
	FRuntimeState& State,
	ARTSCombatUnit& CommandVehicle)
{
	FRTSAISnapshot& Snapshot = State.Snapshot;
	Snapshot.HeadquartersPhase = ERTSAIHeadquartersPhase::ChoosingSite;
	Snapshot.LastOrderFailure = ERTSOrderFailure::None;
	Snapshot.LastDeploymentRefusal = ERTSDeploymentRefusal::None;
	const double CurrentTime = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	TArray<int32> RetiredCandidateIndices;
	for (auto Iterator = State.RetiredCandidateExpiryByIndex.CreateIterator(); Iterator; ++Iterator)
	{
		if (Iterator.Value() <= CurrentTime)
		{
			Iterator.RemoveCurrent();
		}
		else
		{
			RetiredCandidateIndices.Add(Iterator.Key());
		}
	}
	RetiredCandidateIndices.Sort();
	const FRTSHQCandidateSearchResult Search = EvaluateHeadquartersCandidates(
		CommandVehicle,
		State.Profile,
		RetiredCandidateIndices);
	if (!Search.bFound)
	{
		const URTSStructureSubsystem* Structures = GetWorld() != nullptr
			? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
			: nullptr;
		Snapshot.LastRefusal = Structures != nullptr
			&& !Structures->FindStartingZone(Snapshot.TeamId).IsSet()
			? ERTSAIRefusal::StartingZoneUnavailable
			: ERTSAIRefusal::NoLegalHeadquartersCandidate;
		return false;
	}
	Snapshot.Candidate = Search.Selected;
	Snapshot.CandidateIndex = Search.Selected.CandidateIndex;
	Snapshot.ObjectiveLocation = Search.Selected.GroundLocation;
	Snapshot.Score = Search.Selected.TotalScore;
	Snapshot.CurrentAction = ERTSAIAction::DeployCommandStructure;
	Snapshot.LastRefusal = ERTSAIRefusal::None;
	if (Snapshot.CommitmentId == 0)
	{
		Snapshot.CommitmentId = NextCommitmentId++;
	}
	URTSCommandSubsystem* Commands = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSCommandSubsystem>()
		: nullptr;
	ARTSCombatUnit* Movers[] = {&CommandVehicle};
	const FRTSCommandResult MoveResult = Commands != nullptr
		? Commands->IssueMove(Movers, Snapshot.ObjectiveLocation)
		: FRTSCommandResult();
	Snapshot.GroupCommandId = MoveResult.GroupCommandId;
	if (Commands == nullptr || MoveResult.GetAcceptedCount() != 1)
	{
		Snapshot.LastOrderFailure = MoveResult.Failure;
		Snapshot.LastRefusal = ERTSAIRefusal::MoveRejected;
		RetireCandidateAndRetry(State, CommandVehicle);
		return false;
	}
	Snapshot.Phase = ERTSAIStrategyPhase::Committed;
	Snapshot.HeadquartersPhase = ERTSAIHeadquartersPhase::Traveling;
	State.CommitmentStartTimeSeconds = CurrentTime;
	return true;
}

void URTSAIStrategySubsystem::UpdateDeploymentTravel(
	FRuntimeState& State,
	ARTSCombatUnit& CommandVehicle)
{
	FRTSAISnapshot& Snapshot = State.Snapshot;
	const FRTSOrderSnapshot Order = CommandVehicle.GetOrderComponent()->GetSnapshot();
	const float DistanceToSite = FVector::Dist2D(
		CommandVehicle.GetActorLocation(),
		Snapshot.ObjectiveLocation);
	const double CurrentTime = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Order.LastFailure != ERTSOrderFailure::None
		|| CurrentTime - State.CommitmentStartTimeSeconds
			> State.Profile.HeadquartersMovementTimeoutSeconds)
	{
		Snapshot.LastOrderFailure = Order.LastFailure != ERTSOrderFailure::None
			? Order.LastFailure
			: ERTSOrderFailure::PathFollowingFailed;
		Snapshot.LastRefusal = ERTSAIRefusal::MovementFailed;
		RetireCandidateAndRetry(State, CommandVehicle);
		return;
	}
	if (DistanceToSite > State.Profile.HeadquartersArrivalTolerance
		|| Order.Phase != ERTSOrderPhase::Idle)
	{
		return;
	}

	Snapshot.HeadquartersPhase = ERTSAIHeadquartersPhase::ReadyToDeploy;
	URTSStructureSubsystem* Structures = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	Snapshot.HeadquartersPhase = ERTSAIHeadquartersPhase::Deploying;
	++Snapshot.DeploymentAttemptCount;
	const FRTSHeadquartersDeploymentResult Deployment = Structures != nullptr
		? Structures->TryDeployHeadquarters(CommandVehicle)
		: FRTSHeadquartersDeploymentResult();
	Snapshot.LastDeploymentRefusal = Deployment.Refusal;
	if (Deployment.bAccepted)
	{
		++Snapshot.AcceptedDeploymentCount;
		MarkHeadquartersDeployed(State);
		return;
	}
	Snapshot.LastRefusal = ERTSAIRefusal::DeploymentRejected;
	RetireCandidateAndRetry(State, CommandVehicle);
}

void URTSAIStrategySubsystem::RetireCandidateAndRetry(
	FRuntimeState& State,
	ARTSCombatUnit& CommandVehicle)
{
	FRTSAISnapshot& Snapshot = State.Snapshot;
	CommandVehicle.GetOrderComponent()->Cancel();
	const double CurrentTime = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Snapshot.CandidateIndex != INDEX_NONE)
	{
		State.RetiredCandidateExpiryByIndex.Add(
			Snapshot.CandidateIndex,
			CurrentTime + State.Profile.RetryCooldownSeconds);
	}
	++Snapshot.CandidateRetryCount;
	if (Snapshot.CandidateRetryCount >= State.Profile.MaximumPlacementRetries)
	{
		Snapshot.HeadquartersPhase = ERTSAIHeadquartersPhase::RetryLimitReached;
		Snapshot.Phase = ERTSAIStrategyPhase::Observing;
		Snapshot.LastRefusal = ERTSAIRefusal::RetryLimitReached;
		return;
	}
	Snapshot.HeadquartersPhase = ERTSAIHeadquartersPhase::Retrying;
}

void URTSAIStrategySubsystem::MarkHeadquartersDeployed(FRuntimeState& State)
{
	FRTSAISnapshot& Snapshot = State.Snapshot;
	Snapshot.HeadquartersPhase = ERTSAIHeadquartersPhase::Deployed;
	Snapshot.Phase = ERTSAIStrategyPhase::Observing;
	Snapshot.CurrentAction = ERTSAIAction::None;
	Snapshot.LastRefusal = ERTSAIRefusal::None;
}

bool URTSAIStrategySubsystem::EvaluateEconomyAndProduction(FRuntimeState& State)
{
	if (EnsureStructure(
		State,
		ERTSStructureType::MaterialExtractor,
		State.Profile.DesiredMaterialExtractors,
		ERTSAIAction::RestoreMaterialIncome))
	{
		return true;
	}
	if (EnsureStructure(
		State,
		ERTSStructureType::PowerGenerator,
		State.Profile.DesiredPowerGenerators,
		ERTSAIAction::AddPowerCapacity))
	{
		return true;
	}
	if (EnsureStructure(
		State,
		ERTSStructureType::SupplyDepot,
		State.Profile.DesiredSupplyDepots,
		ERTSAIAction::AddSupplyCapacity))
	{
		return true;
	}
	if (EnsureStructure(
		State,
		ERTSStructureType::Factory,
		State.Profile.DesiredFactories,
		ERTSAIAction::EstablishProduction))
	{
		return true;
	}
	return EnqueueMissingRosterUnit(State);
}

bool URTSAIStrategySubsystem::EnsureStructure(
	FRuntimeState& State,
	const ERTSStructureType StructureType,
	const int32 DesiredCount,
	const ERTSAIAction Action)
{
	FRTSAISnapshot& Snapshot = State.Snapshot;
	URTSStructureSubsystem* Structures = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	if (Structures == nullptr)
	{
		Snapshot.LastRefusal = ERTSAIRefusal::HeadquartersUnavailable;
		return true;
	}
	const TArray<FRTSStructureSnapshot> Existing = Structures->Query(Snapshot.TeamId, StructureType);
	if (Existing.Num() >= DesiredCount)
	{
		const FRTSStructureSnapshot* UnderConstruction = Existing.FindByPredicate(
			[](const FRTSStructureSnapshot& Structure)
			{
				return Structure.bAlive && !Structure.bConstructed;
			});
		if (UnderConstruction != nullptr)
		{
			Snapshot.CurrentAction = Action;
			Snapshot.ObjectiveStructureType = StructureType;
			Snapshot.ObjectiveStableId = UnderConstruction->StableStructureId;
			Snapshot.LastRefusal = ERTSAIRefusal::ConstructionInProgress;
			Snapshot.Phase = ERTSAIStrategyPhase::Committed;
			return true;
		}
		return false;
	}

	Snapshot.CurrentAction = Action;
	Snapshot.ObjectiveStructureType = StructureType;
	Snapshot.ObjectiveStableId = INDEX_NONE;
	Snapshot.Phase = ERTSAIStrategyPhase::Committed;
	const double CurrentTime = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	if (CurrentTime < State.NextStructureAttemptTimeSeconds)
	{
		return true;
	}
	const FRTSStructureCandidateSearchResult Search = EvaluateStructureCandidates(
		Snapshot.TeamId,
		StructureType,
		State.Profile);
	if (!Search.bFound)
	{
		const FRTSStructureCandidateReceipt* EconomyRefusal = Search.Candidates.FindByPredicate(
			[](const FRTSStructureCandidateReceipt& Candidate)
			{
				return Candidate.Refusal == ERTSPlacementRefusal::EconomyRejected;
			});
		if (EconomyRefusal != nullptr)
		{
			Snapshot.StructureCandidate = *EconomyRefusal;
			Snapshot.LastPlacementRefusal = EconomyRefusal->Refusal;
			Snapshot.LastRefusal = ERTSAIRefusal::StructurePlacementRejected;
		}
		else
		{
			Snapshot.LastRefusal = ERTSAIRefusal::NoLegalStructureCandidate;
		}
		State.NextStructureAttemptTimeSeconds = CurrentTime
			+ FMath::Max(0.5f, State.Profile.RetryCooldownSeconds);
		return true;
	}

	Snapshot.StructureCandidate = Search.Selected;
	FRTSPlacementRequest Request;
	Request.TeamId = Snapshot.TeamId;
	Request.StructureType = StructureType;
	Request.DesiredWorldLocation = Search.Selected.GroundLocation;
	++Snapshot.StructurePlacementAttemptCount;
	const FRTSPlacementResult Placement = Structures->TryPlace(Request);
	Snapshot.LastPlacementRefusal = Placement.Refusal;
	if (!Placement.bAccepted)
	{
		Snapshot.LastRefusal = ERTSAIRefusal::StructurePlacementRejected;
		int32& RetryCount = State.StructurePlacementRetriesByType.FindOrAdd(
			static_cast<uint8>(StructureType));
		RetryCount = (RetryCount + 1) % State.Profile.MaximumStructurePlacementRetries;
		State.NextStructureAttemptTimeSeconds = CurrentTime
			+ FMath::Max(0.5f, State.Profile.RetryCooldownSeconds);
		return true;
	}

	State.StructurePlacementRetriesByType.Remove(static_cast<uint8>(StructureType));
	State.NextStructureAttemptTimeSeconds = 0.0;
	Snapshot.LastRefusal = ERTSAIRefusal::None;
	Snapshot.ObjectiveStableId = Placement.StableStructureId;
	++Snapshot.AcceptedStructurePlacementCount;
	FRTSAIStructureCommitReceipt& Receipt = Snapshot.StructureCommits.AddDefaulted_GetRef();
	Receipt.Action = Action;
	Receipt.StructureType = StructureType;
	Receipt.StableStructureId = Placement.StableStructureId;
	Receipt.MaterialTransactionId = Placement.MaterialTransactionId;
	return true;
}

bool URTSAIStrategySubsystem::EnqueueMissingRosterUnit(FRuntimeState& State)
{
	FRTSAISnapshot& Snapshot = State.Snapshot;
	URTSStructureSubsystem* Structures = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	URTSProductionSubsystem* Production = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSProductionSubsystem>()
		: nullptr;
	URTSUnitRegistrySubsystem* Units = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSUnitRegistrySubsystem>()
		: nullptr;
	const TArray<FRTSStructureSnapshot> Factories = Structures != nullptr
		? Structures->Query(Snapshot.TeamId, ERTSStructureType::Factory)
		: TArray<FRTSStructureSnapshot>();
	const FRTSStructureSnapshot* Factory = Factories.FindByPredicate(
		[](const FRTSStructureSnapshot& Structure)
		{
			return Structure.bAlive && Structure.bConstructed;
		});
	if (Factory == nullptr || Production == nullptr || Units == nullptr)
	{
		Snapshot.CurrentAction = ERTSAIAction::EstablishProduction;
		Snapshot.LastRefusal = ERTSAIRefusal::FactoryUnavailable;
		return true;
	}
	const TOptional<FRTSProductionSnapshot> ProductionSnapshot = Production->Find(
		Factory->StableStructureId);
	if (!ProductionSnapshot.IsSet())
	{
		Snapshot.LastRefusal = ERTSAIRefusal::FactoryUnavailable;
		return true;
	}
	Snapshot.ObservedProductionState = ProductionSnapshot->State;

	const struct FTarget
	{
		ERTSUnitType UnitType;
		int32 Count;
	} Targets[] = {
		{ERTSUnitType::InfantrySquad, State.Profile.TargetInfantrySquads},
		{ERTSUnitType::LightVehicle, State.Profile.TargetLightVehicles},
		{ERTSUnitType::HeavyVehicle, State.Profile.TargetHeavyVehicles}};
	for (const FTarget& Target : Targets)
	{
		const int32 LivingCount = Units->Query(Snapshot.TeamId, Target.UnitType).Num();
		int32 QueuedCount = 0;
		for (const FRTSProductionQueueEntrySnapshot& Entry : ProductionSnapshot->Queue)
		{
			QueuedCount += Entry.UnitType == Target.UnitType ? 1 : 0;
		}
		if (LivingCount + QueuedCount >= Target.Count)
		{
			continue;
		}

		Snapshot.CurrentAction = ERTSAIAction::EnqueueRosterUnit;
		Snapshot.ObjectiveStableId = Factory->StableStructureId;
		Snapshot.ObjectiveUnitType = Target.UnitType;
		Snapshot.Phase = ERTSAIStrategyPhase::Committed;
		++Snapshot.ProductionEnqueueAttemptCount;
		const FRTSProductionResult Enqueue = Production->TryEnqueue(
			Snapshot.TeamId,
			Factory->StableStructureId,
			Target.UnitType);
		Snapshot.LastProductionRefusal = Enqueue.Refusal;
		if (!Enqueue.bAccepted)
		{
			Snapshot.LastRefusal = ERTSAIRefusal::ProductionRejected;
			return true;
		}
		Snapshot.LastRefusal = ERTSAIRefusal::None;
		++Snapshot.AcceptedProductionEnqueueCount;
		FRTSAIProductionCommitReceipt& Receipt = Snapshot.ProductionCommits.AddDefaulted_GetRef();
		Receipt.UnitType = Target.UnitType;
		Receipt.StableFactoryId = Factory->StableStructureId;
		Receipt.QueueEntryId = Enqueue.QueueEntryId;
		Receipt.MaterialReservationTransactionId = Enqueue.EconomyTransactionId;
		return true;
	}

	Snapshot.CurrentAction = ERTSAIAction::None;
	Snapshot.LastRefusal = ERTSAIRefusal::None;
	Snapshot.Phase = ERTSAIStrategyPhase::Observing;
	return false;
}

bool URTSAIStrategySubsystem::EvaluateDefense(FRuntimeState& State)
{
	FRTSAISnapshot& Snapshot = State.Snapshot;
	URTSStructureSubsystem* Structures = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	URTSUnitRegistrySubsystem* Units = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSUnitRegistrySubsystem>()
		: nullptr;
	URTSCommandSubsystem* Commands = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSCommandSubsystem>()
		: nullptr;
	if (Structures == nullptr || Units == nullptr || Commands == nullptr)
	{
		return false;
	}

	const FGenericTeamId OpponentTeam = Snapshot.TeamId == RTSTeams::Enemy
		? RTSTeams::Player
		: RTSTeams::Enemy;
	const TArray<FRTSStructureSnapshot> FriendlyStructures = Structures->Query(Snapshot.TeamId);
	const TArray<FRTSUnitSnapshot> Hostiles = Units->Query(OpponentTeam);
	const FRTSUnitSnapshot* BestThreat = nullptr;
	float BestThreatDistanceSquared = FMath::Square(State.Profile.BaseThreatRadius);
	for (const FRTSUnitSnapshot& Hostile : Hostiles)
	{
		if (!Hostile.bAlive)
		{
			continue;
		}
		for (const FRTSStructureSnapshot& Structure : FriendlyStructures)
		{
			if (!Structure.bAlive || !Structure.bConstructed)
			{
				continue;
			}
			const float DistanceSquared = FVector::DistSquared2D(
				Hostile.WorldLocation,
				Structure.WorldLocation);
			if (DistanceSquared < BestThreatDistanceSquared
				|| (FMath::IsNearlyEqual(DistanceSquared, BestThreatDistanceSquared)
					&& (BestThreat == nullptr || Hostile.StableUnitId < BestThreat->StableUnitId)))
			{
				BestThreat = &Hostile;
				BestThreatDistanceSquared = DistanceSquared;
			}
		}
	}

	if (BestThreat == nullptr)
	{
		if (State.DefenseCommitment.Action != ERTSAIAction::None)
		{
			ReleaseCommitment(State, State.DefenseCommitment);
		}
		return false;
	}

	if (State.DefenseCommitment.Action != ERTSAIAction::None
		&& State.DefenseCommitment.ObjectiveStableId == BestThreat->StableUnitId)
	{
		State.DefenseCommitment.MemberStableUnitIds.RemoveAll(
			[Units](const int32 StableUnitId)
			{
				const ARTSCombatUnit* Unit = Units->FindActor(StableUnitId);
				return !IsValid(Unit) || !Unit->IsAlive();
			});
		if (!State.DefenseCommitment.MemberStableUnitIds.IsEmpty())
		{
			Snapshot.CurrentAction = ERTSAIAction::DefendBase;
			Snapshot.ObjectiveStableId = BestThreat->StableUnitId;
			Snapshot.LastRefusal = ERTSAIRefusal::None;
			Snapshot.Phase = ERTSAIStrategyPhase::Committed;
			PublishCommitment(Snapshot, State.DefenseCommitment);
			return true;
		}
	}
	if (State.DefenseCommitment.Action != ERTSAIAction::None)
	{
		ReleaseCommitment(State, State.DefenseCommitment);
	}

	TArray<ARTSCombatUnit*> Defenders;
	for (const FRTSUnitSnapshot& UnitSnapshot : Units->Query(Snapshot.TeamId))
	{
		if (!UnitSnapshot.bAlive
			|| UnitSnapshot.UnitType == ERTSUnitType::CommandVehicle
			|| State.AttackCommitment.MemberStableUnitIds.Contains(UnitSnapshot.StableUnitId))
		{
			continue;
		}
		if (ARTSCombatUnit* Unit = Units->FindActor(UnitSnapshot.StableUnitId); IsValid(Unit))
		{
			Defenders.Add(Unit);
			if (Defenders.Num() >= State.Profile.MaximumDefenseUnits)
			{
				break;
			}
		}
	}
	Snapshot.CurrentAction = ERTSAIAction::DefendBase;
	Snapshot.ObjectiveStableId = BestThreat->StableUnitId;
	Snapshot.Phase = ERTSAIStrategyPhase::Committed;
	if (Defenders.Num() < State.Profile.MinimumDefenseUnits)
	{
		Snapshot.LastRefusal = ERTSAIRefusal::DefenseForceUnavailable;
		return true;
	}
	ARTSCombatUnit* Threat = Units->FindActor(BestThreat->StableUnitId);
	if (!IsValid(Threat))
	{
		Snapshot.LastRefusal = ERTSAIRefusal::AttackTargetUnavailable;
		return true;
	}
	const FRTSCommandResult Command = Commands->IssueAttack(Defenders, *Threat);
	if (Command.GetAcceptedCount() < State.Profile.MinimumDefenseUnits)
	{
		Snapshot.LastOrderFailure = Command.Failure;
		Snapshot.LastRefusal = ERTSAIRefusal::DefenseCommandRejected;
		return true;
	}

	FTacticalCommitment Commitment;
	Commitment.Action = ERTSAIAction::DefendBase;
	Commitment.Phase = ERTSAITacticalPhase::Defending;
	Commitment.CommitmentId = NextCommitmentId++;
	Commitment.GroupCommandId = Command.GroupCommandId;
	Commitment.ObjectiveStableId = BestThreat->StableUnitId;
	Commitment.StartedAtSeconds = GetWorld()->GetTimeSeconds();
	for (const FRTSUnitCommandOutcome& Outcome : Command.Outcomes)
	{
		if (Outcome.bAccepted && IsValid(Outcome.Unit))
		{
			Commitment.MemberStableUnitIds.Add(Outcome.Unit->GetStableUnitId());
		}
	}
	Commitment.MemberStableUnitIds.Sort();
	State.DefenseCommitment = Commitment;
	++Snapshot.DefenseResponseCount;
	Snapshot.LastRefusal = ERTSAIRefusal::None;
	PublishCommitment(Snapshot, State.DefenseCommitment);
	FRTSAICommandCommitReceipt& Receipt = Snapshot.CommandCommits.AddDefaulted_GetRef();
	Receipt.Action = Commitment.Action;
	Receipt.Phase = Commitment.Phase;
	Receipt.TacticalCommitmentId = Commitment.CommitmentId;
	Receipt.GroupCommandId = Commitment.GroupCommandId;
	Receipt.MemberStableUnitIds = Commitment.MemberStableUnitIds;
	Receipt.ObjectiveStableId = Commitment.ObjectiveStableId;
	return true;
}

bool URTSAIStrategySubsystem::EnsureDefensiveTurrets(FRuntimeState& State)
{
	FRTSAISnapshot& Snapshot = State.Snapshot;
	URTSStructureSubsystem* Structures = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	if (Structures == nullptr)
	{
		return false;
	}
	const TArray<FRTSStructureSnapshot> Turrets = Structures->Query(
		Snapshot.TeamId,
		ERTSStructureType::DefensiveTurret);
	if (Turrets.Num() >= State.Profile.MaximumDefensiveTurrets)
	{
		const FRTSStructureSnapshot* UnderConstruction = Turrets.FindByPredicate(
			[](const FRTSStructureSnapshot& Turret)
			{
				return Turret.bAlive && !Turret.bConstructed;
			});
		if (UnderConstruction != nullptr)
		{
			Snapshot.CurrentAction = ERTSAIAction::PlaceDefensiveTurret;
			Snapshot.ObjectiveStructureType = ERTSStructureType::DefensiveTurret;
			Snapshot.ObjectiveStableId = UnderConstruction->StableStructureId;
			Snapshot.LastRefusal = ERTSAIRefusal::ConstructionInProgress;
			Snapshot.Phase = ERTSAIStrategyPhase::Committed;
			return true;
		}
		return false;
	}

	Snapshot.CurrentAction = ERTSAIAction::PlaceDefensiveTurret;
	Snapshot.ObjectiveStructureType = ERTSStructureType::DefensiveTurret;
	Snapshot.Phase = ERTSAIStrategyPhase::Committed;
	const double CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime < State.NextStructureAttemptTimeSeconds)
	{
		return true;
	}
	const FRTSStructureCandidateSearchResult Search = EvaluateTurretCandidates(
		Snapshot.TeamId,
		State.Profile);
	if (!Search.bFound)
	{
		const FRTSStructureCandidateReceipt* EconomyRefusal = Search.Candidates.FindByPredicate(
			[](const FRTSStructureCandidateReceipt& Candidate)
			{
				return Candidate.Refusal == ERTSPlacementRefusal::EconomyRejected;
			});
		if (EconomyRefusal != nullptr)
		{
			Snapshot.StructureCandidate = *EconomyRefusal;
			Snapshot.LastPlacementRefusal = EconomyRefusal->Refusal;
		}
		Snapshot.LastRefusal = ERTSAIRefusal::TurretPlacementRejected;
		State.NextStructureAttemptTimeSeconds = CurrentTime
			+ FMath::Max(0.5f, State.Profile.RetryCooldownSeconds);
		return true;
	}

	Snapshot.StructureCandidate = Search.Selected;
	FRTSPlacementRequest Request;
	Request.TeamId = Snapshot.TeamId;
	Request.StructureType = ERTSStructureType::DefensiveTurret;
	Request.DesiredWorldLocation = Search.Selected.GroundLocation;
	++Snapshot.StructurePlacementAttemptCount;
	const FRTSPlacementResult Placement = Structures->TryPlace(Request);
	Snapshot.LastPlacementRefusal = Placement.Refusal;
	if (!Placement.bAccepted)
	{
		Snapshot.LastRefusal = ERTSAIRefusal::TurretPlacementRejected;
		State.NextStructureAttemptTimeSeconds = CurrentTime
			+ FMath::Max(0.5f, State.Profile.RetryCooldownSeconds);
		return true;
	}

	State.NextStructureAttemptTimeSeconds = 0.0;
	Snapshot.LastRefusal = ERTSAIRefusal::None;
	Snapshot.ObjectiveStableId = Placement.StableStructureId;
	++Snapshot.AcceptedStructurePlacementCount;
	FRTSAIStructureCommitReceipt& Receipt = Snapshot.StructureCommits.AddDefaulted_GetRef();
	Receipt.Action = ERTSAIAction::PlaceDefensiveTurret;
	Receipt.StructureType = ERTSStructureType::DefensiveTurret;
	Receipt.StableStructureId = Placement.StableStructureId;
	Receipt.MaterialTransactionId = Placement.MaterialTransactionId;
	return true;
}

bool URTSAIStrategySubsystem::TryAssignSafeReclaim(FRuntimeState& State)
{
	FRTSAISnapshot& Snapshot = State.Snapshot;
	URTSWreckageSubsystem* Wreckage = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSWreckageSubsystem>()
		: nullptr;
	URTSUnitRegistrySubsystem* Units = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSUnitRegistrySubsystem>()
		: nullptr;
	URTSStructureSubsystem* Structures = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	URTSCommandSubsystem* Commands = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSCommandSubsystem>()
		: nullptr;
	if (Wreckage == nullptr || Units == nullptr || Structures == nullptr || Commands == nullptr)
	{
		return false;
	}
	if (State.ReclaimCommitmentId != 0)
	{
		const int64 ExistingReclaimCommitmentId = State.ReclaimCommitmentId;
		ARTSCombatUnit* Reclaimer = Units->FindActor(State.ReclaimerStableUnitId);
		ARTSWreckage* Target = Wreckage->Find(State.ReclaimWreckageId);
		const FRTSOrderSnapshot Order = IsValid(Reclaimer)
			? Reclaimer->GetOrderComponent()->GetSnapshot()
			: FRTSOrderSnapshot();
		if (IsValid(Target)
			&& Target->GetSnapshot().LeaseHolderStableUnitId == State.ReclaimerStableUnitId
			&& Order.Kind == ERTSOrderKind::Reclaim)
		{
			Snapshot.CurrentAction = ERTSAIAction::ReclaimWreckage;
			Snapshot.TacticalPhase = ERTSAITacticalPhase::Reclaiming;
			Snapshot.TacticalCommitmentId = State.ReclaimCommitmentId;
			Snapshot.GroupCommandId = State.ReclaimGroupCommandId;
			Snapshot.ObjectiveStableId = State.ReclaimWreckageId;
			Snapshot.TacticalMemberStableUnitIds = {State.ReclaimerStableUnitId};
			Snapshot.Phase = ERTSAIStrategyPhase::Committed;
			return true;
		}
		State.ReclaimCommitmentId = 0;
		State.ReclaimGroupCommandId = 0;
		State.ReclaimerStableUnitId = INDEX_NONE;
		State.ReclaimWreckageId = INDEX_NONE;
		if (Snapshot.TacticalCommitmentId == ExistingReclaimCommitmentId)
		{
			PublishCommitment(Snapshot, FTacticalCommitment());
		}
	}

	const TArray<FRTSStructureSnapshot> Headquarters = Structures->Query(
		Snapshot.TeamId,
		ERTSStructureType::Headquarters);
	if (Headquarters.IsEmpty())
	{
		return false;
	}
	const FGenericTeamId OpponentTeam = Snapshot.TeamId == RTSTeams::Enemy
		? RTSTeams::Player
		: RTSTeams::Enemy;
	const TArray<FRTSUnitSnapshot> Hostiles = Units->Query(OpponentTeam);
	const TArray<FRTSUnitSnapshot> Infantry = Units->Query(
		Snapshot.TeamId,
		ERTSUnitType::InfantrySquad);
	ARTSCombatUnit* BestInfantry = nullptr;
	ARTSWreckage* BestWreckage = nullptr;
	int32 BestWreckageId = INDEX_NONE;
	double BestDistanceSquared = TNumericLimits<double>::Max();
	for (const FRTSWreckageSnapshot& WreckSnapshot : Wreckage->QueryAvailableSnapshots())
	{
		if (WreckSnapshot.LeaseHolderStableUnitId != INDEX_NONE
			|| FVector::DistSquared2D(WreckSnapshot.WorldLocation, Headquarters[0].WorldLocation)
				> FMath::Square(State.Profile.ReclaimSearchRadius))
		{
			continue;
		}
		const bool bThreatened = Hostiles.ContainsByPredicate(
			[&WreckSnapshot, &State](const FRTSUnitSnapshot& Hostile)
			{
				return Hostile.bAlive
					&& FVector::DistSquared2D(Hostile.WorldLocation, WreckSnapshot.WorldLocation)
						< FMath::Square(State.Profile.ReclaimSafetyRadius);
			});
		if (bThreatened)
		{
			continue;
		}
		for (const FRTSUnitSnapshot& InfantrySnapshot : Infantry)
		{
			if (!InfantrySnapshot.bAlive
				|| InfantrySnapshot.OrderPhase != ERTSOrderPhase::Idle
				|| State.AttackCommitment.MemberStableUnitIds.Contains(InfantrySnapshot.StableUnitId)
				|| State.DefenseCommitment.MemberStableUnitIds.Contains(InfantrySnapshot.StableUnitId))
			{
				continue;
			}
			if (!HasCompleteRouteWithinRange(
					*GetWorld(),
					InfantrySnapshot.WorldLocation,
					WreckSnapshot.WorldLocation,
					FRTSMilestone2Configuration::Load().Reclaim.Range))
			{
				continue;
			}
			const double DistanceSquared = FVector::DistSquared2D(
				InfantrySnapshot.WorldLocation,
				WreckSnapshot.WorldLocation);
			if (DistanceSquared < BestDistanceSquared
				|| (FMath::IsNearlyEqual(DistanceSquared, BestDistanceSquared)
					&& (BestWreckageId == INDEX_NONE
						|| WreckSnapshot.StableWreckageId < BestWreckageId)))
			{
				BestInfantry = Units->FindActor(InfantrySnapshot.StableUnitId);
				BestWreckage = Wreckage->Find(WreckSnapshot.StableWreckageId);
				BestWreckageId = WreckSnapshot.StableWreckageId;
				BestDistanceSquared = DistanceSquared;
			}
		}
	}
	if (!IsValid(BestInfantry) || !IsValid(BestWreckage))
	{
		return false;
	}
	ARTSCombatUnit* Reclaimers[] = {BestInfantry};
	const FRTSCommandResult Command = Commands->IssueReclaim(Reclaimers, *BestWreckage);
	Snapshot.CurrentAction = ERTSAIAction::ReclaimWreckage;
	Snapshot.ObjectiveStableId = BestWreckageId;
	Snapshot.Phase = ERTSAIStrategyPhase::Committed;
	if (Command.GetAcceptedCount() != 1)
	{
		Snapshot.LastOrderFailure = Command.Failure;
		Snapshot.LastRefusal = ERTSAIRefusal::ReclaimCommandRejected;
		return true;
	}

	State.ReclaimCommitmentId = NextCommitmentId++;
	State.ReclaimGroupCommandId = Command.GroupCommandId;
	State.ReclaimerStableUnitId = BestInfantry->GetStableUnitId();
	State.ReclaimWreckageId = BestWreckageId;
	++Snapshot.ReclaimOrderCount;
	Snapshot.LastRefusal = ERTSAIRefusal::None;
	Snapshot.TacticalPhase = ERTSAITacticalPhase::Reclaiming;
	Snapshot.TacticalCommitmentId = State.ReclaimCommitmentId;
	Snapshot.GroupCommandId = Command.GroupCommandId;
	Snapshot.TacticalMemberStableUnitIds = {State.ReclaimerStableUnitId};
	FRTSAIReclaimCommitReceipt& Receipt = Snapshot.ReclaimCommits.AddDefaulted_GetRef();
	Receipt.TacticalCommitmentId = State.ReclaimCommitmentId;
	Receipt.GroupCommandId = Command.GroupCommandId;
	Receipt.ReclaimerStableUnitId = State.ReclaimerStableUnitId;
	Receipt.StableWreckageId = BestWreckageId;
	return true;
}

void URTSAIStrategySubsystem::MaintainAttackCommitment(FRuntimeState& State)
{
	FTacticalCommitment& Commitment = State.AttackCommitment;
	if (Commitment.Action == ERTSAIAction::None)
	{
		return;
	}
	URTSUnitRegistrySubsystem* Units = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSUnitRegistrySubsystem>()
		: nullptr;
	URTSStructureSubsystem* Structures = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	if (Units == nullptr || Structures == nullptr)
	{
		ReleaseCommitment(State, Commitment);
		return;
	}
	Commitment.MemberStableUnitIds.RemoveAll(
		[Units](const int32 StableUnitId)
		{
			const ARTSCombatUnit* Unit = Units->FindActor(StableUnitId);
			return !IsValid(Unit) || !Unit->IsAlive();
		});
	const bool bObjectiveAvailable = Commitment.bObjectiveIsStructure
		? IsValid(Structures->FindActor(Commitment.ObjectiveStableId))
			&& Structures->FindActor(Commitment.ObjectiveStableId)->IsAlive()
		: IsValid(Units->FindActor(Commitment.ObjectiveStableId))
			&& Units->FindActor(Commitment.ObjectiveStableId)->IsAlive();
	const double CurrentTime = GetWorld()->GetTimeSeconds();
	if (Commitment.MemberStableUnitIds.IsEmpty()
		|| !bObjectiveAvailable
		|| CurrentTime - Commitment.StartedAtSeconds >= State.Profile.AttackCommitmentSeconds)
	{
		ReleaseCommitment(State, Commitment);
		return;
	}
	PublishCommitment(State.Snapshot, Commitment);
}

bool URTSAIStrategySubsystem::UpdateOrLaunchAttackGroup(FRuntimeState& State)
{
	FRTSAISnapshot& Snapshot = State.Snapshot;
	URTSUnitRegistrySubsystem* Units = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSUnitRegistrySubsystem>()
		: nullptr;
	URTSStructureSubsystem* Structures = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	URTSCommandSubsystem* Commands = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSCommandSubsystem>()
		: nullptr;
	if (Units == nullptr || Structures == nullptr || Commands == nullptr)
	{
		return false;
	}

	auto RecordCommand = [&Snapshot](const FTacticalCommitment& Commitment)
	{
		FRTSAICommandCommitReceipt& Receipt = Snapshot.CommandCommits.AddDefaulted_GetRef();
		Receipt.Action = Commitment.Action;
		Receipt.Phase = Commitment.Phase;
		Receipt.TacticalCommitmentId = Commitment.CommitmentId;
		Receipt.GroupCommandId = Commitment.GroupCommandId;
		Receipt.MemberStableUnitIds = Commitment.MemberStableUnitIds;
		Receipt.ObjectiveStableId = Commitment.ObjectiveStableId;
		Receipt.bObjectiveIsStructure = Commitment.bObjectiveIsStructure;
	};
	auto IssueAttack = [Commands](
		const TArray<ARTSCombatUnit*>& Members,
		ARTSCombatUnit* UnitTarget,
		ARTSStructure* StructureTarget)
	{
		return IsValid(StructureTarget)
			? Commands->IssueAttack(Members, *StructureTarget)
			: IsValid(UnitTarget)
				? Commands->IssueAttack(Members, *UnitTarget)
				: FRTSCommandResult();
	};

	FTacticalCommitment& Commitment = State.AttackCommitment;
	if (Commitment.Action != ERTSAIAction::None)
	{
		TArray<ARTSCombatUnit*> Members;
		for (const int32 StableUnitId : Commitment.MemberStableUnitIds)
		{
			if (ARTSCombatUnit* Unit = Units->FindActor(StableUnitId); IsValid(Unit) && Unit->IsAlive())
			{
				Members.Add(Unit);
			}
		}
		ARTSStructure* StructureTarget = Commitment.bObjectiveIsStructure
			? Structures->FindActor(Commitment.ObjectiveStableId)
			: nullptr;
		ARTSCombatUnit* UnitTarget = !Commitment.bObjectiveIsStructure
			? Units->FindActor(Commitment.ObjectiveStableId)
			: nullptr;
		if (Commitment.Phase == ERTSAITacticalPhase::Mustering)
		{
			const bool bMusterComplete = Members.Num() == Commitment.MemberStableUnitIds.Num()
				&& Members.ContainsByPredicate(
					[&Commitment, &State](const ARTSCombatUnit* Unit)
					{
						const FRTSOrderSnapshot Order = Unit->GetOrderComponent()->GetSnapshot();
						return Order.GroupCommandId != Commitment.GroupCommandId
							|| Order.Phase != ERTSOrderPhase::Idle
							|| FVector::DistSquared2D(
									Unit->GetActorLocation(),
									Commitment.MusterLocation)
								> FMath::Square(State.Profile.AttackMusterTolerance);
					}) == false;
			const bool bMusterTimedOut = GetWorld()->GetTimeSeconds() - Commitment.StartedAtSeconds
				>= State.Profile.AttackMusterTimeoutSeconds;
			if (bMusterComplete || bMusterTimedOut)
			{
				// Formation movement can be physically blocked by its own units even though every
				// member and attack route remains legal. Mustering is a coordination aid, not a
				// permanent gate: after a bounded wait, the normal attack orders take over.
				Snapshot.MusterTimeoutCount += bMusterTimedOut ? 1 : 0;
				const FRTSCommandResult Attack = IssueAttack(Members, UnitTarget, StructureTarget);
				if (Attack.GetAcceptedCount() < State.Profile.MinimumAttackGroupSize)
				{
					Snapshot.LastOrderFailure = Attack.Failure;
					Snapshot.LastRefusal = ERTSAIRefusal::AttackCommandRejected;
					ReleaseCommitment(State, Commitment);
					return true;
				}
				Commitment.Phase = ERTSAITacticalPhase::Attacking;
				Commitment.GroupCommandId = Attack.GroupCommandId;
				Commitment.MemberStableUnitIds.Reset();
				for (const FRTSUnitCommandOutcome& Outcome : Attack.Outcomes)
				{
					if (Outcome.bAccepted && IsValid(Outcome.Unit))
					{
						Commitment.MemberStableUnitIds.Add(Outcome.Unit->GetStableUnitId());
					}
				}
				Commitment.MemberStableUnitIds.Sort();
				++Snapshot.AttackLaunchCount;
				RecordCommand(Commitment);
			}
		}
		Snapshot.CurrentAction = ERTSAIAction::MusterAttackGroup;
		Snapshot.ObjectiveStableId = Commitment.ObjectiveStableId;
		Snapshot.LastRefusal = ERTSAIRefusal::None;
		Snapshot.Phase = ERTSAIStrategyPhase::Committed;
		PublishCommitment(Snapshot, Commitment);
		return true;
	}

	const FGenericTeamId OpponentTeam = Snapshot.TeamId == RTSTeams::Enemy
		? RTSTeams::Player
		: RTSTeams::Enemy;
	ARTSStructure* StructureTarget = nullptr;
	auto FindConstructedStructure = [Structures, OpponentTeam](const ERTSStructureType StructureType)
	{
		for (const FRTSStructureSnapshot& Candidate : Structures->Query(OpponentTeam, StructureType))
		{
			if (Candidate.bAlive && Candidate.bConstructed)
			{
				return Structures->FindActor(Candidate.StableStructureId);
			}
		}
		return static_cast<ARTSStructure*>(nullptr);
	};
	const bool bHeadquartersEscalated = Snapshot.AttackLaunchCount
		>= State.Profile.MinimumRaidsBeforeHeadquarters;
	if (bHeadquartersEscalated)
	{
		StructureTarget = FindConstructedStructure(ERTSStructureType::Headquarters);
	}
	const ERTSStructureType RaidPriorities[] = {
		ERTSStructureType::Factory,
		ERTSStructureType::MaterialExtractor,
		ERTSStructureType::PowerGenerator,
		ERTSStructureType::SupplyDepot,
		ERTSStructureType::DefensiveTurret};
	for (const ERTSStructureType StructureType : RaidPriorities)
	{
		if (!IsValid(StructureTarget))
		{
			StructureTarget = FindConstructedStructure(StructureType);
		}
	}
	if (!IsValid(StructureTarget))
	{
		// The escalation count shapes a working-base raid, not an invulnerability rule. A player
		// with no constructed auxiliary target must still be able to lose the command structure.
		StructureTarget = FindConstructedStructure(ERTSStructureType::Headquarters);
	}
	ARTSCombatUnit* UnitTarget = nullptr;
	if (!IsValid(StructureTarget))
	{
		const TArray<FRTSUnitSnapshot> CommandVehicles = Units->Query(
			OpponentTeam,
			ERTSUnitType::CommandVehicle);
		if (!CommandVehicles.IsEmpty())
		{
			UnitTarget = Units->FindActor(CommandVehicles[0].StableUnitId);
		}
	}
	if (!IsValid(StructureTarget) && !IsValid(UnitTarget))
	{
		Snapshot.CurrentAction = ERTSAIAction::MusterAttackGroup;
		Snapshot.LastRefusal = ERTSAIRefusal::AttackTargetUnavailable;
		return true;
	}

	TArray<FRTSUnitSnapshot> MilitaryUnits;
	for (const FRTSUnitSnapshot& Unit : Units->Query(Snapshot.TeamId))
	{
		if (Unit.bAlive && Unit.UnitType != ERTSUnitType::CommandVehicle)
		{
			MilitaryUnits.Add(Unit);
		}
	}
	if (MilitaryUnits.Num() < State.Profile.MinimumAttackGroupSize + State.Profile.DefenseReserveUnits)
	{
		Snapshot.CurrentAction = ERTSAIAction::MusterAttackGroup;
		Snapshot.LastRefusal = ERTSAIRefusal::AttackGroupUnavailable;
		return true;
	}
	MilitaryUnits.RemoveAt(0, State.Profile.DefenseReserveUnits);
	TArray<ARTSCombatUnit*> Attackers;
	const ERTSUnitType RequiredTypes[] = {
		ERTSUnitType::InfantrySquad,
		ERTSUnitType::LightVehicle,
		ERTSUnitType::HeavyVehicle};
	for (const ERTSUnitType RequiredType : RequiredTypes)
	{
		const int32 MatchIndex = MilitaryUnits.IndexOfByPredicate(
			[RequiredType](const FRTSUnitSnapshot& Unit)
			{
				return Unit.UnitType == RequiredType;
			});
		if (MatchIndex != INDEX_NONE)
		{
			if (ARTSCombatUnit* Unit = Units->FindActor(MilitaryUnits[MatchIndex].StableUnitId); IsValid(Unit))
			{
				Attackers.Add(Unit);
			}
			MilitaryUnits.RemoveAt(MatchIndex);
		}
	}
	for (const FRTSUnitSnapshot& Remaining : MilitaryUnits)
	{
		if (ARTSCombatUnit* Unit = Units->FindActor(Remaining.StableUnitId); IsValid(Unit))
		{
			Attackers.Add(Unit);
		}
	}
	if (Attackers.Num() < State.Profile.MinimumAttackGroupSize)
	{
		Snapshot.CurrentAction = ERTSAIAction::MusterAttackGroup;
		Snapshot.LastRefusal = ERTSAIRefusal::AttackGroupUnavailable;
		return true;
	}

	const TArray<FRTSStructureSnapshot> Headquarters = Structures->Query(
		Snapshot.TeamId,
		ERTSStructureType::Headquarters);
	if (Headquarters.IsEmpty())
	{
		Snapshot.LastRefusal = ERTSAIRefusal::HeadquartersUnavailable;
		return true;
	}
	const FVector TargetLocation = IsValid(StructureTarget)
		? StructureTarget->GetActorLocation()
		: UnitTarget->GetActorLocation();
	FVector AttackDirection = TargetLocation - Headquarters[0].WorldLocation;
	AttackDirection.Z = 0.0f;
	if (!AttackDirection.Normalize())
	{
		AttackDirection = FVector::ForwardVector;
	}
	const FVector MusterLocation = Headquarters[0].WorldLocation
		+ AttackDirection * State.Profile.AttackMusterDistance;
	const FRTSCommandResult Muster = Commands->IssueMove(Attackers, MusterLocation);
	if (Muster.GetAcceptedCount() < State.Profile.MinimumAttackGroupSize)
	{
		Snapshot.LastOrderFailure = Muster.Failure;
		Snapshot.LastRefusal = ERTSAIRefusal::AttackCommandRejected;
		return true;
	}

	Commitment.Action = ERTSAIAction::MusterAttackGroup;
	Commitment.Phase = ERTSAITacticalPhase::Mustering;
	Commitment.CommitmentId = NextCommitmentId++;
	Commitment.GroupCommandId = Muster.GroupCommandId;
	Commitment.ObjectiveStableId = IsValid(StructureTarget)
		? StructureTarget->GetStableStructureId()
		: UnitTarget->GetStableUnitId();
	Commitment.bObjectiveIsStructure = IsValid(StructureTarget);
	Commitment.MusterLocation = MusterLocation;
	Commitment.StartedAtSeconds = GetWorld()->GetTimeSeconds();
	for (const FRTSUnitCommandOutcome& Outcome : Muster.Outcomes)
	{
		if (Outcome.bAccepted && IsValid(Outcome.Unit))
		{
			Commitment.MemberStableUnitIds.Add(Outcome.Unit->GetStableUnitId());
		}
	}
	Commitment.MemberStableUnitIds.Sort();
	Snapshot.CurrentAction = ERTSAIAction::MusterAttackGroup;
	Snapshot.ObjectiveStableId = Commitment.ObjectiveStableId;
	Snapshot.LastRefusal = ERTSAIRefusal::None;
	Snapshot.Phase = ERTSAIStrategyPhase::Committed;
	PublishCommitment(Snapshot, Commitment);
	RecordCommand(Commitment);
	return true;
}

void URTSAIStrategySubsystem::ReleaseCommitment(
	FRuntimeState& State,
	FTacticalCommitment& Commitment)
{
	const bool bReleasingAttack = &Commitment == &State.AttackCommitment
		&& Commitment.Action != ERTSAIAction::None;
	const int64 ReleasedCommitmentId = Commitment.CommitmentId;
	if (URTSUnitRegistrySubsystem* Units = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSUnitRegistrySubsystem>()
		: nullptr)
	{
		for (const int32 StableUnitId : Commitment.MemberStableUnitIds)
		{
			ARTSCombatUnit* Unit = Units->FindActor(StableUnitId);
			if (IsValid(Unit)
				&& Unit->GetOrderComponent()->GetSnapshot().GroupCommandId == Commitment.GroupCommandId)
			{
				Unit->GetOrderComponent()->Cancel();
			}
		}
	}
	Commitment = {};
	State.Snapshot.AttackReleaseCount += bReleasingAttack ? 1 : 0;
	if (State.Snapshot.TacticalCommitmentId == ReleasedCommitmentId)
	{
		PublishCommitment(State.Snapshot, Commitment);
	}
}

void URTSAIStrategySubsystem::PublishCommitment(
	FRTSAISnapshot& Snapshot,
	const FTacticalCommitment& Commitment) const
{
	Snapshot.TacticalPhase = Commitment.Phase;
	Snapshot.TacticalCommitmentId = Commitment.CommitmentId;
	Snapshot.GroupCommandId = Commitment.GroupCommandId;
	Snapshot.ObjectiveStableId = Commitment.ObjectiveStableId;
	Snapshot.bObjectiveIsStructure = Commitment.bObjectiveIsStructure;
	Snapshot.MusterLocation = Commitment.MusterLocation;
	Snapshot.TacticalMemberStableUnitIds = Commitment.MemberStableUnitIds;
}

bool URTSAIStrategySubsystem::IsSupportedTeam(const FGenericTeamId TeamId)
{
	return TeamId == RTSTeams::Player || TeamId == RTSTeams::Enemy;
}
