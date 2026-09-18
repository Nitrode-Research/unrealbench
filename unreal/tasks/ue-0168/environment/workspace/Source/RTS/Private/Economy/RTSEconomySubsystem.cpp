// Copyright Epic Games, Inc. All Rights Reserved.

#include "Economy/RTSEconomySubsystem.h"

#include "Configuration/RTSMilestone2Configuration.h"
#include "Units/RTSTeams.h"

void URTSEconomySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Restore this contractor-owned implementation.
	Super::Initialize(Collection);
}

int64 URTSEconomySubsystem::AllocateTransactionId()
{
	return NextTransactionId++;
}

FRTSEconomySnapshot URTSEconomySubsystem::GetSnapshot(const FGenericTeamId TeamId) const
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSEconomyTransactionResult URTSEconomySubsystem::TryCommit(
	const int64 TransactionId,
	const FGenericTeamId TeamId,
	const FRTSEconomyDelta& Delta)
{
	// Restore this contractor-owned implementation.
	return {};
}

bool URTSEconomySubsystem::TryConvertSupplyReservation(
	const int64 ReservationTransactionId,
	const int64 UsageTransactionId)
{
	// Restore this contractor-owned implementation.
	return {};
}

bool URTSEconomySubsystem::TryReleaseSupplyReservation(const int64 ReservationTransactionId)
{
	// Restore this contractor-owned implementation.
	return {};
}

bool URTSEconomySubsystem::TryRollback(const int64 TransactionId)
{
	// Restore this contractor-owned implementation.
	return {};
}

FRTSEconomyChanged& URTSEconomySubsystem::OnEconomyChanged()
{
	return EconomyChanged;
}

FRTSEconomyTransactionApplied& URTSEconomySubsystem::OnTransactionApplied()
{
	return TransactionApplied;
}

FRTSEconomySnapshot* URTSEconomySubsystem::FindMutableLedger(const FGenericTeamId TeamId)
{
	return Ledgers.Find(TeamId.GetId());
}

bool URTSEconomySubsystem::IsSupportedTeam(const FGenericTeamId TeamId)
{
	return TeamId == RTSTeams::Player || TeamId == RTSTeams::Enemy;
}
