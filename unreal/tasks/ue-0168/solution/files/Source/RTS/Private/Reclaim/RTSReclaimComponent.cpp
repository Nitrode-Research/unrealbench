// Copyright Epic Games, Inc. All Rights Reserved.

#include "Reclaim/RTSReclaimComponent.h"

#include "Configuration/RTSMilestone2Configuration.h"
#include "Match/RTSMatchSubsystem.h"
#include "Reclaim/RTSWreckage.h"
#include "Units/RTSCombatUnit.h"

URTSReclaimComponent::URTSReclaimComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URTSReclaimComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Interrupt();
	Super::EndPlay(EndPlayReason);
}

FRTSReclaimResult URTSReclaimComponent::Begin(ARTSWreckage& Wreckage)
{
	Interrupt();
	FRTSReclaimResult Result;
	ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner());
	const URTSMatchSubsystem* Match = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSMatchSubsystem>()
		: nullptr;
	if (Match != nullptr && Match->IsResolved())
	{
		Result.Refusal = ERTSReclaimRefusal::MatchResolved;
	}
	else if (Unit == nullptr)
	{
		Result.Refusal = ERTSReclaimRefusal::InvalidReclaimer;
	}
	else if (!Unit->IsAlive())
	{
		Result.Refusal = ERTSReclaimRefusal::ReclaimerUnavailable;
	}
	else if (!Unit->IsMilestone2Unit() || Unit->GetUnitType() != ERTSUnitType::InfantrySquad)
	{
		Result.Refusal = ERTSReclaimRefusal::NotInfantry;
	}
	else if (Wreckage.GetWorld() != GetWorld())
	{
		Result.Refusal = ERTSReclaimRefusal::WrongWorld;
	}
	else
	{
		Result = Wreckage.TryAcquireLease(*Unit);
	}

	Snapshot.LastRefusal = Result.Refusal;
	if (!Result.bAccepted)
	{
		Snapshot.State = ERTSReclaimState::Interrupted;
		ReclaimChanged.Broadcast();
		return Result;
	}

	Snapshot.Target = &Wreckage;
	Snapshot.State = ERTSReclaimState::Approaching;
	Snapshot.Progress = 0.0f;
	AccumulatedSeconds = 0.0f;
	ReclaimChanged.Broadcast();
	return Result;
}

FRTSReclaimResult URTSReclaimComponent::Advance(const float DeltaTime)
{
	FRTSReclaimResult Result;
	ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner());
	ARTSWreckage* Wreckage = Snapshot.Target;
	if (Unit == nullptr || !Unit->IsAlive())
	{
		Result.Refusal = ERTSReclaimRefusal::ReclaimerUnavailable;
		Interrupt(Result.Refusal);
		return Result;
	}
	if (!IsValid(Wreckage) || !Wreckage->IsAvailable())
	{
		Result.Refusal = ERTSReclaimRefusal::WreckageUnavailable;
		Interrupt(Result.Refusal);
		return Result;
	}

	const FRTSReclaimTuning Reclaim = FRTSMilestone2Configuration::Load().Reclaim;
	if (FVector::DistSquared2D(Unit->GetActorLocation(), Wreckage->GetActorLocation())
		> FMath::Square(Reclaim.Range))
	{
		Result.Refusal = ERTSReclaimRefusal::OutOfRange;
		Interrupt(Result.Refusal);
		return Result;
	}

	Snapshot.State = ERTSReclaimState::Reclaiming;
	AccumulatedSeconds += FMath::Max(0.0f, DeltaTime);
	Snapshot.Progress = FMath::Clamp(AccumulatedSeconds / Reclaim.DurationSeconds, 0.0f, 1.0f);
	Result.bAccepted = true;
	if (Snapshot.Progress + UE_KINDA_SMALL_NUMBER < 1.0f)
	{
		ReclaimChanged.Broadcast();
		return Result;
	}

	Result = Wreckage->TryCompleteReclaim(*Unit);
	Snapshot.LastRefusal = Result.Refusal;
	if (Result.bCompleted)
	{
		Snapshot.State = ERTSReclaimState::Completed;
		Snapshot.Progress = 1.0f;
		Snapshot.Target = nullptr;
	}
	else
	{
		Interrupt(Result.Refusal);
		return Result;
	}
	ReclaimChanged.Broadcast();
	return Result;
}

void URTSReclaimComponent::MarkApproaching()
{
	if (IsValid(Snapshot.Target))
	{
		Snapshot.State = ERTSReclaimState::Approaching;
		ReclaimChanged.Broadcast();
	}
}

void URTSReclaimComponent::Interrupt(const ERTSReclaimRefusal Refusal)
{
	ARTSCombatUnit* Unit = Cast<ARTSCombatUnit>(GetOwner());
	if (Unit != nullptr)
	{
		if (ARTSWreckage* Wreckage = Snapshot.Target; IsValid(Wreckage))
		{
			Wreckage->ReleaseLease(*Unit);
		}
	}
	const bool bHadAttempt = Snapshot.Target != nullptr;
	Snapshot.Target = nullptr;
	Snapshot.Progress = 0.0f;
	Snapshot.LastRefusal = Refusal;
	Snapshot.State = bHadAttempt || Refusal != ERTSReclaimRefusal::None
		? ERTSReclaimState::Interrupted
		: ERTSReclaimState::Idle;
	AccumulatedSeconds = 0.0f;
	if (bHadAttempt || Refusal != ERTSReclaimRefusal::None)
	{
		ReclaimChanged.Broadcast();
	}
}

FRTSReclaimSnapshot URTSReclaimComponent::GetSnapshot() const
{
	return Snapshot;
}

FRTSReclaimChanged& URTSReclaimComponent::OnReclaimChanged()
{
	return ReclaimChanged;
}
