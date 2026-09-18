// Copyright Epic Games, Inc. All Rights Reserved.

#include "Match/RTSMatchSubsystem.h"

#include "Configuration/RTSMilestone2Configuration.h"
#include "Structures/RTSStructure.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"

void URTSMatchSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Restore this contractor-owned implementation.
	Super::Initialize(Collection);
}

bool URTSMatchSubsystem::RegisterCommandVehicle(ARTSCombatUnit& CommandVehicle)
{
	// Restore this contractor-owned implementation.
	return {};
}

bool URTSMatchSubsystem::PromoteCommandVehicleToHeadquarters(
	ARTSCombatUnit& CommandVehicle,
	ARTSStructure& Headquarters)
{
	// Restore this contractor-owned implementation.
	return {};
}

bool URTSMatchSubsystem::RevertHeadquartersPromotion(
	ARTSCombatUnit& CommandVehicle,
	ARTSStructure& Headquarters)
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSMatchResult URTSMatchSubsystem::ReportCommandVehicleDestroyed(ARTSCombatUnit& CommandVehicle)
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSMatchResult URTSMatchSubsystem::ReportHeadquartersDestroyed(ARTSStructure& Headquarters)
{
	// Restore this contractor-owned implementation.
	return {};
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
	// Restore this contractor-owned implementation.
	return {};
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
	// Restore this contractor-owned implementation.
	return {};
}

FRTSFactionMatchStatistics* URTSMatchSubsystem::FindStatistics(const FGenericTeamId TeamId)
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSMatchResult URTSMatchSubsystem::Resolve(
	const FGenericTeamId LosingTeamId,
	const ERTSMatchResolution Resolution)
{
	// Restore this contractor-owned implementation.
	return {};
}

FGenericTeamId URTSMatchSubsystem::OpponentOf(const FGenericTeamId TeamId)
{
	// Restore this contractor-owned implementation.
	return {};
}
