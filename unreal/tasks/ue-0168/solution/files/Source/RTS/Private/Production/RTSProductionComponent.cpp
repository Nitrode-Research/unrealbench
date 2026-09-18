// Copyright Epic Games, Inc. All Rights Reserved.

#include "Production/RTSProductionComponent.h"

#include "Economy/RTSEconomySubsystem.h"
#include "Engine/World.h"
#include "Match/RTSMatchSubsystem.h"
#include "Orders/RTSCommandSubsystem.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "TimerManager.h"
#include "Units/RTSCombatUnit.h"

namespace
{
constexpr float ProcessingIntervalSeconds = 0.1f;
}

URTSProductionComponent::URTSProductionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URTSProductionComponent::Initialize(ARTSStructure& InFactory)
{
	Factory = &InFactory;
	State = InFactory.IsConstructed() ? ERTSProductionState::Idle : ERTSProductionState::UnderConstruction;
}

void URTSProductionComponent::NotifyConstructionCompleted()
{
	if (State == ERTSProductionState::UnderConstruction)
	{
		State = Queue.IsEmpty() ? ERTSProductionState::Idle : ERTSProductionState::Producing;
	}
	StartProcessing();
}

FRTSProductionResult URTSProductionComponent::TryEnqueue(const ERTSUnitType UnitType)
{
	FRTSProductionResult Result;
	const URTSMatchSubsystem* Match = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSMatchSubsystem>()
		: nullptr;
	if (Match != nullptr && Match->IsResolved())
	{
		Result.Refusal = ERTSProductionRefusal::MatchResolved;
	}
	else if (!IsValid(Factory) || !Factory->IsAlive() || !Factory->IsConstructed())
	{
		Result.Refusal = ERTSProductionRefusal::FactoryUnavailable;
	}
	else if (UnitType == ERTSUnitType::CommandVehicle)
	{
		Result.Refusal = ERTSProductionRefusal::InvalidUnitType;
	}
	else
	{
		const FRTSMilestone2Configuration Configuration = FRTSMilestone2Configuration::Load();
		const FRTSUnitDefinition* Definition = Configuration.FindUnit(UnitType);
		if (Definition == nullptr)
		{
			Result.Refusal = ERTSProductionRefusal::InvalidUnitType;
		}
		else if (Queue.Num() >= Configuration.Production.MaximumQueueEntries)
		{
			Result.Refusal = ERTSProductionRefusal::QueueFull;
		}
		else if (UWorld* World = GetWorld())
		{
			if (URTSEconomySubsystem* Economy = World->GetSubsystem<URTSEconomySubsystem>())
			{
				FRTSEconomyDelta Reservation;
				Reservation.Materials = -Definition->MaterialCost;
				Reservation.SupplyReserved = Definition->SupplyCost;
				const int64 TransactionId = Economy->AllocateTransactionId();
				if (Economy->TryCommit(TransactionId, Factory->GetGenericTeamId(), Reservation).bAccepted)
				{
					FQueueEntry& Entry = Queue.AddDefaulted_GetRef();
					Entry.QueueEntryId = NextQueueEntryId++;
					Entry.UnitType = UnitType;
					Entry.ProductionSeconds = Definition->ProductionSeconds;
					Entry.MaterialCost = Definition->MaterialCost;
					Entry.SupplyCost = Definition->SupplyCost;
					Entry.ReservationTransactionId = TransactionId;
					Result.bAccepted = true;
					Result.QueueEntryId = Entry.QueueEntryId;
					Result.EconomyTransactionId = TransactionId;
					StartProcessing();
				}
			}
		}
		if (!Result.bAccepted && Result.Refusal == ERTSProductionRefusal::None)
		{
			Result.Refusal = ERTSProductionRefusal::EconomyRejected;
		}
	}
	LastRefusal = Result.Refusal;
	return Result;
}

FRTSProductionSnapshot URTSProductionComponent::GetSnapshot() const
{
	FRTSProductionSnapshot Snapshot;
	if (IsValid(Factory))
	{
		Snapshot.StableFactoryId = Factory->GetStableStructureId();
		Snapshot.TeamId = Factory->GetGenericTeamId();
	}
	Snapshot.State = State;
	Snapshot.LastRefusal = LastRefusal;
	for (const FQueueEntry& Entry : Queue)
	{
		FRTSProductionQueueEntrySnapshot& EntrySnapshot = Snapshot.Queue.AddDefaulted_GetRef();
		EntrySnapshot.QueueEntryId = Entry.QueueEntryId;
		EntrySnapshot.UnitType = Entry.UnitType;
		EntrySnapshot.Progress = Entry.ProductionSeconds > 0.0f
			? FMath::Clamp(Entry.ElapsedSeconds / Entry.ProductionSeconds, 0.0f, 1.0f)
			: 1.0f;
		EntrySnapshot.ProductionSeconds = Entry.ProductionSeconds;
		EntrySnapshot.MaterialCost = Entry.MaterialCost;
		EntrySnapshot.SupplyCost = Entry.SupplyCost;
	}
	return Snapshot;
}

void URTSProductionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ProcessingTimer);
	}
	ReleaseOutstandingReservations();
	Super::EndPlay(EndPlayReason);
}

void URTSProductionComponent::StartProcessing()
{
	UWorld* World = GetWorld();
	if (World == nullptr || Queue.IsEmpty() || World->GetTimerManager().IsTimerActive(ProcessingTimer))
	{
		return;
	}
	LastUpdateSeconds = World->GetTimeSeconds();
	World->GetTimerManager().SetTimer(
		ProcessingTimer,
		this,
		&URTSProductionComponent::AdvanceQueue,
		ProcessingIntervalSeconds,
		true);
}

void URTSProductionComponent::AdvanceQueue()
{
	UWorld* World = GetWorld();
	if (World == nullptr || !IsValid(Factory) || !Factory->IsAlive())
	{
		return;
	}
	if (Queue.IsEmpty())
	{
		State = ERTSProductionState::Idle;
		World->GetTimerManager().ClearTimer(ProcessingTimer);
		return;
	}
	const double CurrentSeconds = World->GetTimeSeconds();
	const float DeltaSeconds = FMath::Max(0.0f, static_cast<float>(CurrentSeconds - LastUpdateSeconds));
	LastUpdateSeconds = CurrentSeconds;
	if (!Factory->IsConstructed())
	{
		State = ERTSProductionState::UnderConstruction;
		return;
	}
	const URTSEconomySubsystem* Economy = World->GetSubsystem<URTSEconomySubsystem>();
	if (Economy == nullptr || Economy->GetSnapshot(Factory->GetGenericTeamId()).IsInBrownout())
	{
		State = ERTSProductionState::PausedPower;
		return;
	}

	FQueueEntry& Entry = Queue[0];
	Entry.ElapsedSeconds = FMath::Min(Entry.ProductionSeconds, Entry.ElapsedSeconds + DeltaSeconds);
	State = ERTSProductionState::Producing;
	if (Entry.ElapsedSeconds >= Entry.ProductionSeconds)
	{
		TryCompleteFrontEntry();
	}
}

bool URTSProductionComponent::TryCompleteFrontEntry()
{
	UWorld* World = GetWorld();
	URTSStructureSubsystem* Structures = World != nullptr ? World->GetSubsystem<URTSStructureSubsystem>() : nullptr;
	URTSEconomySubsystem* Economy = World != nullptr ? World->GetSubsystem<URTSEconomySubsystem>() : nullptr;
	if (World == nullptr || Structures == nullptr || Economy == nullptr || Queue.IsEmpty())
	{
		return false;
	}
	const TOptional<FVector> Exit = Structures->FindFactorySpawnLocation(*Factory);
	if (!Exit.IsSet())
	{
		State = ERTSProductionState::BlockedExit;
		return false;
	}

	const FQueueEntry Entry = Queue[0];
	const FVector SpawnLocation = Exit.GetValue() + FVector(0.0f, 0.0f, 110.0f);
	ARTSCombatUnit* Unit = World->SpawnActorDeferred<ARTSCombatUnit>(
		ARTSCombatUnit::StaticClass(),
		FTransform(FRotator::ZeroRotator, SpawnLocation),
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (Unit == nullptr)
	{
		return false;
	}
	const int64 UsageTransactionId = Economy->AllocateTransactionId();
	if (!Economy->TryConvertSupplyReservation(Entry.ReservationTransactionId, UsageTransactionId))
	{
		Unit->Destroy();
		return false;
	}
	Unit->ConfigureMilestone2Unit(
		Entry.UnitType,
		Structures->AllocateProducedUnitId(),
		Factory->GetGenericTeamId(),
		UsageTransactionId);
	Unit->FinishSpawning(FTransform(FRotator::ZeroRotator, SpawnLocation));
	if (URTSMatchSubsystem* Match = World->GetSubsystem<URTSMatchSubsystem>())
	{
		Match->ReportUnitProduced(Factory->GetGenericTeamId(), Unit->GetStableUnitId());
	}
	if (URTSCommandSubsystem* Commands = World->GetSubsystem<URTSCommandSubsystem>())
	{
		const FVector ExitDirection = (SpawnLocation - Factory->GetActorLocation()).GetSafeNormal2D();
		ARTSCombatUnit* ProducedUnits[] = {Unit};
		Commands->IssueMove(
			TConstArrayView<ARTSCombatUnit*>(ProducedUnits, UE_ARRAY_COUNT(ProducedUnits)),
			SpawnLocation + ExitDirection * 900.0f);
	}
	Queue.RemoveAt(0);
	State = Queue.IsEmpty() ? ERTSProductionState::Idle : ERTSProductionState::Producing;
	LastRefusal = ERTSProductionRefusal::None;
	return true;
}

void URTSProductionComponent::ReleaseOutstandingReservations()
{
	if (UWorld* World = GetWorld())
	{
		if (URTSEconomySubsystem* Economy = World->GetSubsystem<URTSEconomySubsystem>())
		{
			for (const FQueueEntry& Entry : Queue)
			{
				Economy->TryReleaseSupplyReservation(Entry.ReservationTransactionId);
			}
		}
	}
	Queue.Reset();
}
