// Copyright Epic Games, Inc. All Rights Reserved.

#include "Match/RTSMatchSubsystem.h"

#include "Configuration/RTSMilestone2Configuration.h"
#include "Structures/RTSStructure.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"

void URTSMatchSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Snapshot.StartTimeSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	Snapshot.PlayerStatistics.TeamId = RTSTeams::Player;
	Snapshot.EnemyStatistics.TeamId = RTSTeams::Enemy;
}

bool URTSMatchSubsystem::RegisterCommandVehicle(ARTSCombatUnit& CommandVehicle)
{
	if (!CommandVehicle.IsMilestone2Unit()
		|| CommandVehicle.GetUnitType() != ERTSUnitType::CommandVehicle
		|| CommandVehicle.GetGenericTeamId() == FGenericTeamId::NoTeam
		|| IsResolved())
	{
		return false;
	}
	const uint8 TeamKey = CommandVehicle.GetGenericTeamId().GetId();
	if (HeadquartersByTeam.Contains(TeamKey))
	{
		return false;
	}
	ARTSCombatUnit* Existing = CommandVehicles.FindRef(TeamKey).Get();
	if (IsValid(Existing) && Existing != &CommandVehicle)
	{
		return false;
	}
	CommandVehicles.Add(TeamKey, &CommandVehicle);
	return true;
}

bool URTSMatchSubsystem::PromoteCommandVehicleToHeadquarters(
	ARTSCombatUnit& CommandVehicle,
	ARTSStructure& Headquarters)
{
	const FGenericTeamId TeamId = CommandVehicle.GetGenericTeamId();
	const uint8 TeamKey = TeamId.GetId();
	if (TeamId == FGenericTeamId::NoTeam
		|| IsResolved()
		|| Headquarters.GetGenericTeamId() != TeamId
		|| Headquarters.GetStructureType() != ERTSStructureType::Headquarters
		|| CommandVehicles.FindRef(TeamKey).Get() != &CommandVehicle
		|| HeadquartersByTeam.Contains(TeamKey))
	{
		return false;
	}
	CommandVehicles.Remove(TeamKey);
	HeadquartersByTeam.Add(TeamKey, &Headquarters);
	return true;
}

bool URTSMatchSubsystem::RevertHeadquartersPromotion(
	ARTSCombatUnit& CommandVehicle,
	ARTSStructure& Headquarters)
{
	const uint8 TeamKey = CommandVehicle.GetGenericTeamId().GetId();
	if (HeadquartersByTeam.FindRef(TeamKey).Get() != &Headquarters
		|| CommandVehicle.GetGenericTeamId() != Headquarters.GetGenericTeamId())
	{
		return false;
	}
	HeadquartersByTeam.Remove(TeamKey);
	CommandVehicles.Add(TeamKey, &CommandVehicle);
	return true;
}

FRTSMatchResult URTSMatchSubsystem::ReportCommandVehicleDestroyed(ARTSCombatUnit& CommandVehicle)
{
	const FGenericTeamId TeamId = CommandVehicle.GetGenericTeamId();
	if (CommandVehicles.FindRef(TeamId.GetId()).Get() != &CommandVehicle)
	{
		FRTSMatchResult Result;
		Result.Snapshot = Snapshot;
		return Result;
	}
	CommandVehicles.Remove(TeamId.GetId());
	return Resolve(TeamId, ERTSMatchResolution::CommandVehicleDestroyed);
}

FRTSMatchResult URTSMatchSubsystem::ReportHeadquartersDestroyed(ARTSStructure& Headquarters)
{
	const FGenericTeamId TeamId = Headquarters.GetGenericTeamId();
	if (HeadquartersByTeam.FindRef(TeamId.GetId()).Get() != &Headquarters)
	{
		FRTSMatchResult Result;
		Result.Snapshot = Snapshot;
		return Result;
	}
	HeadquartersByTeam.Remove(TeamId.GetId());
	return Resolve(TeamId, ERTSMatchResolution::HeadquartersDestroyed);
}

FRTSMatchEventReceipt URTSMatchSubsystem::ReportUnitProduced(
	const FGenericTeamId TeamId,
	const int32 StableUnitId)
{
	return RecordEvent(ERTSMatchEventKind::UnitProduced, TeamId, StableUnitId, 1);
}

FRTSMatchEventReceipt URTSMatchSubsystem::ReportUnitLost(
	const FGenericTeamId TeamId,
	const int32 StableUnitId)
{
	return RecordEvent(ERTSMatchEventKind::UnitLost, TeamId, StableUnitId, 1);
}

FRTSMatchEventReceipt URTSMatchSubsystem::ReportMaterialsReclaimed(
	const FGenericTeamId TeamId,
	const int32 StableWreckageId,
	const int32 MaterialAmount)
{
	return RecordEvent(
		ERTSMatchEventKind::MaterialsReclaimed,
		TeamId,
		StableWreckageId,
		MaterialAmount);
}

ARTSCombatUnit* URTSMatchSubsystem::GetCommandVehicle(const FGenericTeamId TeamId) const
{
	return CommandVehicles.FindRef(TeamId.GetId()).Get();
}

ARTSStructure* URTSMatchSubsystem::GetHeadquarters(const FGenericTeamId TeamId) const
{
	return HeadquartersByTeam.FindRef(TeamId.GetId()).Get();
}

FRTSMatchSnapshot URTSMatchSubsystem::GetSnapshot() const
{
	return Snapshot;
}

bool URTSMatchSubsystem::IsResolved() const
{
	return Snapshot.State == ERTSMatchState::Resolved;
}

FRTSMatchResolved& URTSMatchSubsystem::OnMatchResolved()
{
	return MatchResolved;
}

FRTSMatchEventRecorded& URTSMatchSubsystem::OnMatchEventRecorded()
{
	return MatchEventRecorded;
}

FRTSMatchEventReceipt URTSMatchSubsystem::RecordEvent(
	const ERTSMatchEventKind Kind,
	const FGenericTeamId TeamId,
	const int32 StableSourceId,
	const int32 Amount)
{
	FRTSMatchEventReceipt Receipt;
	Receipt.Kind = Kind;
	Receipt.TeamId = TeamId;
	Receipt.StableSourceId = StableSourceId;
	Receipt.Amount = Amount;
	FRTSFactionMatchStatistics* Statistics = FindStatistics(TeamId);
	if (Statistics == nullptr || StableSourceId == INDEX_NONE || Amount < 0)
	{
		return Receipt;
	}
	const uint64 EventKey = (static_cast<uint64>(Kind) << 56)
		| (static_cast<uint64>(TeamId.GetId()) << 48)
		| static_cast<uint32>(StableSourceId);
	if (RecordedEventKeys.Contains(EventKey))
	{
		return Receipt;
	}
	RecordedEventKeys.Add(EventKey);
	switch (Kind)
	{
	case ERTSMatchEventKind::UnitProduced:
		Statistics->UnitsProduced += Amount;
		break;
	case ERTSMatchEventKind::UnitLost:
		Statistics->UnitsLost += Amount;
		break;
	case ERTSMatchEventKind::MaterialsReclaimed:
		Statistics->MaterialsReclaimed += Amount;
		break;
	case ERTSMatchEventKind::MatchResolved:
		break;
	}
	Receipt.bAccepted = true;
	Receipt.EventSequence = NextEventSequence++;
	MatchEventRecorded.Broadcast(Receipt);
	return Receipt;
}

FRTSFactionMatchStatistics* URTSMatchSubsystem::FindStatistics(const FGenericTeamId TeamId)
{
	if (TeamId == RTSTeams::Player)
	{
		return &Snapshot.PlayerStatistics;
	}
	if (TeamId == RTSTeams::Enemy)
	{
		return &Snapshot.EnemyStatistics;
	}
	return nullptr;
}

FRTSMatchResult URTSMatchSubsystem::Resolve(
	const FGenericTeamId LosingTeamId,
	const ERTSMatchResolution Resolution)
{
	FRTSMatchResult Result;
	Result.Snapshot = Snapshot;
	if (IsResolved())
	{
		return Result;
	}
	const FGenericTeamId WinningTeamId = OpponentOf(LosingTeamId);
	if (WinningTeamId == FGenericTeamId::NoTeam)
	{
		return Result;
	}

	Snapshot.State = ERTSMatchState::Resolved;
	Snapshot.Resolution = Resolution;
	Snapshot.WinningTeamId = WinningTeamId;
	Snapshot.LosingTeamId = LosingTeamId;
	Snapshot.ResolutionSequence = 1;
	Snapshot.ResolutionTimeSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	Snapshot.ActiveDurationSeconds = FMath::Max(
		0.0,
		Snapshot.ResolutionTimeSeconds - Snapshot.StartTimeSeconds);
	RecordEvent(ERTSMatchEventKind::MatchResolved, LosingTeamId, Snapshot.ResolutionSequence, 0);
	Result.bAccepted = true;
	Result.Snapshot = Snapshot;
	MatchResolved.Broadcast(Snapshot);
	return Result;
}

FGenericTeamId URTSMatchSubsystem::OpponentOf(const FGenericTeamId TeamId)
{
	if (TeamId == RTSTeams::Player)
	{
		return RTSTeams::Enemy;
	}
	if (TeamId == RTSTeams::Enemy)
	{
		return RTSTeams::Player;
	}
	return FGenericTeamId::NoTeam;
}
