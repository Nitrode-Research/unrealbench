// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Production/RTSProductionTypes.h"
#include "RTSProductionComponent.generated.h"

class ARTSStructure;

/** Owns one Factory's bounded queue and converts reserved Supply into one spawned unit. */
UCLASS()
class RTS_API URTSProductionComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSProductionComponent();
	void Initialize(ARTSStructure& InFactory);
	void NotifyConstructionCompleted();
	FRTSProductionResult TryEnqueue(ERTSUnitType UnitType);
	FRTSProductionSnapshot GetSnapshot() const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FQueueEntry
	{
		int32 QueueEntryId = INDEX_NONE;
		ERTSUnitType UnitType = ERTSUnitType::InfantrySquad;
		float ElapsedSeconds = 0.0f;
		float ProductionSeconds = 0.0f;
		int32 MaterialCost = 0;
		int32 SupplyCost = 0;
		int64 ReservationTransactionId = 0;
	};

	void StartProcessing();
	void AdvanceQueue();
	bool TryCompleteFrontEntry();
	void ReleaseOutstandingReservations();

	UPROPERTY(Transient)
	TObjectPtr<ARTSStructure> Factory;
	TArray<FQueueEntry> Queue;
	FTimerHandle ProcessingTimer;
	ERTSProductionState State = ERTSProductionState::UnderConstruction;
	ERTSProductionRefusal LastRefusal = ERTSProductionRefusal::None;
	double LastUpdateSeconds = 0.0;
	int32 NextQueueEntryId = 1;
};
