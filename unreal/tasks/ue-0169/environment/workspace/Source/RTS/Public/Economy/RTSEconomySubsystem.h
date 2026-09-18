// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Economy/RTSEconomyTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSEconomySubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FRTSEconomyChanged, const FRTSEconomySnapshot&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRTSEconomyTransactionApplied, const FRTSEconomyTransactionReceipt&);

/** Owns faction ledgers and makes multi-resource changes indivisible and exactly once. */
UCLASS()
class RTS_API URTSEconomySubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	int64 AllocateTransactionId();
	FRTSEconomySnapshot GetSnapshot(FGenericTeamId TeamId) const;
	FRTSEconomyTransactionResult TryCommit(
		int64 TransactionId,
		FGenericTeamId TeamId,
		const FRTSEconomyDelta& Delta);
	bool TryConvertSupplyReservation(int64 ReservationTransactionId, int64 UsageTransactionId);
	bool TryReleaseSupplyReservation(int64 ReservationTransactionId);
	bool TryRollback(int64 TransactionId);
	FRTSEconomyChanged& OnEconomyChanged();
	FRTSEconomyTransactionApplied& OnTransactionApplied();

private:
	struct FCommittedTransaction
	{
		FGenericTeamId TeamId = FGenericTeamId::NoTeam;
		FRTSEconomyDelta Delta;
	};

	FRTSEconomySnapshot* FindMutableLedger(FGenericTeamId TeamId);
	static bool IsSupportedTeam(FGenericTeamId TeamId);

	TMap<uint8, FRTSEconomySnapshot> Ledgers;
	TMap<int64, FCommittedTransaction> CommittedTransactions;
	TSet<int64> RetiredTransactions;
	int64 NextTransactionId = 1;
	FRTSEconomyChanged EconomyChanged;
	FRTSEconomyTransactionApplied TransactionApplied;
};
