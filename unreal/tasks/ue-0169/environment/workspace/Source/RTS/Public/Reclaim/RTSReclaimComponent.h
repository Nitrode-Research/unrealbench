// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Reclaim/RTSReclaimTypes.h"
#include "RTSReclaimComponent.generated.h"

class ARTSWreckage;

DECLARE_MULTICAST_DELEGATE(FRTSReclaimChanged);

/** Infantry capability that owns one lease and the continuous in-range reclaim progress. */
UCLASS()
class RTS_API URTSReclaimComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSReclaimComponent();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	FRTSReclaimResult Begin(ARTSWreckage& Wreckage);
	FRTSReclaimResult Advance(float DeltaTime);
	void MarkApproaching();
	void Interrupt(ERTSReclaimRefusal Refusal = ERTSReclaimRefusal::None);
	FRTSReclaimSnapshot GetSnapshot() const;
	FRTSReclaimChanged& OnReclaimChanged();

private:
	FRTSReclaimSnapshot Snapshot;
	float AccumulatedSeconds = 0.0f;
	FRTSReclaimChanged ReclaimChanged;
};
