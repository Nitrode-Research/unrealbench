// Copyright Epic Games, Inc. All Rights Reserved.

#include "Economy/RTSEconomySubsystem.h"

#include "Configuration/RTSMilestone2Configuration.h"
#include "Units/RTSTeams.h"

void URTSEconomySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const int32 StartingMaterials = FRTSMilestone2Configuration::Load().Economy.StartingMaterials;
	for (const FGenericTeamId TeamId : {RTSTeams::Player, RTSTeams::Enemy})
	{
		FRTSEconomySnapshot Snapshot;
		Snapshot.TeamId = TeamId;
		Snapshot.Materials = StartingMaterials;
		Ledgers.Add(TeamId.GetId(), Snapshot);
	}
}

int64 URTSEconomySubsystem::AllocateTransactionId()
{
	return NextTransactionId++;
}

FRTSEconomySnapshot URTSEconomySubsystem::GetSnapshot(const FGenericTeamId TeamId) const
{
	if (const FRTSEconomySnapshot* Snapshot = Ledgers.Find(TeamId.GetId()))
	{
		return *Snapshot;
	}
	FRTSEconomySnapshot Missing;
	Missing.TeamId = TeamId;
	return Missing;
}

FRTSEconomyTransactionResult URTSEconomySubsystem::TryCommit(
	const int64 TransactionId,
	const FGenericTeamId TeamId,
	const FRTSEconomyDelta& Delta)
{
	FRTSEconomyTransactionResult Result;
	Result.TransactionId = TransactionId;
	Result.Snapshot = GetSnapshot(TeamId);
	if (TransactionId <= 0)
	{
		Result.Refusal = ERTSEconomyRefusal::InvalidTransaction;
		return Result;
	}
	if (!IsSupportedTeam(TeamId))
	{
		Result.Refusal = ERTSEconomyRefusal::InvalidTeam;
		return Result;
	}
	if (CommittedTransactions.Contains(TransactionId) || RetiredTransactions.Contains(TransactionId))
	{
		Result.Refusal = ERTSEconomyRefusal::AlreadyApplied;
		return Result;
	}

	FRTSEconomySnapshot* Ledger = FindMutableLedger(TeamId);
	check(Ledger != nullptr);
	FRTSEconomySnapshot Candidate;
	Candidate.TeamId = TeamId;
	Candidate.Materials = Ledger->Materials + Delta.Materials;
	Candidate.PowerGeneration = Ledger->PowerGeneration + Delta.PowerGeneration;
	Candidate.PowerDemand = Ledger->PowerDemand + Delta.PowerDemand;
	Candidate.SupplyUsed = Ledger->SupplyUsed + Delta.SupplyUsed;
	Candidate.SupplyReserved = Ledger->SupplyReserved + Delta.SupplyReserved;
	Candidate.SupplyCapacity = Ledger->SupplyCapacity + Delta.SupplyCapacity;
	if (Candidate.Materials < 0)
	{
		Result.Refusal = ERTSEconomyRefusal::InsufficientMaterials;
		return Result;
	}
	// Existing units may precede the first HQ, but transactions may never increase usage beyond
	// a positive cap. This preserves the authored opening roster without weakening production.
	if ((Delta.SupplyUsed > 0 || Delta.SupplyReserved > 0)
		&& Candidate.SupplyCapacity > 0
		&& Candidate.SupplyUsed + Candidate.SupplyReserved > Candidate.SupplyCapacity)
	{
		Result.Refusal = ERTSEconomyRefusal::InsufficientSupply;
		return Result;
	}
	if (Candidate.PowerGeneration < 0
		|| Candidate.PowerDemand < 0
		|| Candidate.SupplyUsed < 0
		|| Candidate.SupplyReserved < 0
		|| Candidate.SupplyCapacity < 0)
	{
		Result.Refusal = ERTSEconomyRefusal::WouldCreateNegativeTotal;
		return Result;
	}

	*Ledger = Candidate;
	FCommittedTransaction Transaction;
	Transaction.TeamId = TeamId;
	Transaction.Delta = Delta;
	CommittedTransactions.Add(TransactionId, Transaction);
	Result.bAccepted = true;
	Result.Refusal = ERTSEconomyRefusal::None;
	Result.Snapshot = *Ledger;
	EconomyChanged.Broadcast(*Ledger);
	FRTSEconomyTransactionReceipt Receipt;
	Receipt.Kind = ERTSEconomyTransactionKind::Commit;
	Receipt.TransactionId = TransactionId;
	Receipt.TeamId = TeamId;
	Receipt.Delta = Delta;
	Receipt.Snapshot = *Ledger;
	TransactionApplied.Broadcast(Receipt);
	return Result;
}

bool URTSEconomySubsystem::TryConvertSupplyReservation(
	const int64 ReservationTransactionId,
	const int64 UsageTransactionId)
{
	FCommittedTransaction* Reservation = CommittedTransactions.Find(ReservationTransactionId);
	if (Reservation == nullptr
		|| Reservation->Delta.SupplyReserved <= 0
		|| UsageTransactionId <= 0
		|| CommittedTransactions.Contains(UsageTransactionId)
		|| RetiredTransactions.Contains(UsageTransactionId))
	{
		return false;
	}

	FRTSEconomySnapshot* Ledger = FindMutableLedger(Reservation->TeamId);
	check(Ledger != nullptr);
	const FGenericTeamId TeamId = Reservation->TeamId;
	const int32 ReservedSupply = Reservation->Delta.SupplyReserved;
	Ledger->SupplyReserved -= ReservedSupply;
	Ledger->SupplyUsed += ReservedSupply;
	Reservation->Delta.SupplyReserved = 0;

	FCommittedTransaction Usage;
	Usage.TeamId = TeamId;
	Usage.Delta.SupplyUsed = ReservedSupply;
	CommittedTransactions.Add(UsageTransactionId, Usage);
	EconomyChanged.Broadcast(*Ledger);
	FRTSEconomyTransactionReceipt Receipt;
	Receipt.Kind = ERTSEconomyTransactionKind::SupplyConversion;
	Receipt.TransactionId = UsageTransactionId;
	Receipt.RelatedTransactionId = ReservationTransactionId;
	Receipt.TeamId = TeamId;
	Receipt.Delta.SupplyReserved = -ReservedSupply;
	Receipt.Delta.SupplyUsed = ReservedSupply;
	Receipt.Snapshot = *Ledger;
	TransactionApplied.Broadcast(Receipt);
	return true;
}

bool URTSEconomySubsystem::TryReleaseSupplyReservation(const int64 ReservationTransactionId)
{
	FCommittedTransaction* Reservation = CommittedTransactions.Find(ReservationTransactionId);
	if (Reservation == nullptr || Reservation->Delta.SupplyReserved <= 0)
	{
		return false;
	}
	FRTSEconomySnapshot* Ledger = FindMutableLedger(Reservation->TeamId);
	check(Ledger != nullptr);
	const int32 ReleasedSupply = Reservation->Delta.SupplyReserved;
	Ledger->SupplyReserved -= ReleasedSupply;
	Reservation->Delta.SupplyReserved = 0;
	EconomyChanged.Broadcast(*Ledger);
	FRTSEconomyTransactionReceipt Receipt;
	Receipt.Kind = ERTSEconomyTransactionKind::SupplyRelease;
	Receipt.TransactionId = ReservationTransactionId;
	Receipt.TeamId = Reservation->TeamId;
	Receipt.Delta.SupplyReserved = -ReleasedSupply;
	Receipt.Snapshot = *Ledger;
	TransactionApplied.Broadcast(Receipt);
	return true;
}

bool URTSEconomySubsystem::TryRollback(const int64 TransactionId)
{
	FCommittedTransaction Transaction;
	if (!CommittedTransactions.RemoveAndCopyValue(TransactionId, Transaction))
	{
		return false;
	}

	FRTSEconomySnapshot* Ledger = FindMutableLedger(Transaction.TeamId);
	check(Ledger != nullptr);
	Ledger->Materials -= Transaction.Delta.Materials;
	Ledger->PowerGeneration -= Transaction.Delta.PowerGeneration;
	Ledger->PowerDemand -= Transaction.Delta.PowerDemand;
	Ledger->SupplyUsed -= Transaction.Delta.SupplyUsed;
	Ledger->SupplyReserved -= Transaction.Delta.SupplyReserved;
	Ledger->SupplyCapacity -= Transaction.Delta.SupplyCapacity;
	RetiredTransactions.Add(TransactionId);
	EconomyChanged.Broadcast(*Ledger);
	FRTSEconomyTransactionReceipt Receipt;
	Receipt.Kind = ERTSEconomyTransactionKind::Rollback;
	Receipt.TransactionId = TransactionId;
	Receipt.TeamId = Transaction.TeamId;
	Receipt.Delta.Materials = -Transaction.Delta.Materials;
	Receipt.Delta.PowerGeneration = -Transaction.Delta.PowerGeneration;
	Receipt.Delta.PowerDemand = -Transaction.Delta.PowerDemand;
	Receipt.Delta.SupplyUsed = -Transaction.Delta.SupplyUsed;
	Receipt.Delta.SupplyReserved = -Transaction.Delta.SupplyReserved;
	Receipt.Delta.SupplyCapacity = -Transaction.Delta.SupplyCapacity;
	Receipt.Snapshot = *Ledger;
	TransactionApplied.Broadcast(Receipt);
	return true;
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
