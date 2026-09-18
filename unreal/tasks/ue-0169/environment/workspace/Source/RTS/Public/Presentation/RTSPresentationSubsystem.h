// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Combat/RTSCombatTypes.h"
#include "CoreMinimal.h"
#include "Presentation/RTSPresentationTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSPresentationSubsystem.generated.h"

class ARTSCombatUnit;
class ARTSStructure;
class ARTSTransientEffect;
class URTSSelectionSubsystem;
class USoundBase;
struct FRTSCommandResult;
struct FRTSMatchEventReceipt;
struct FRTSStructureSnapshot;

DECLARE_MULTICAST_DELEGATE_OneParam(FRTSPresentationRequested, const FRTSPresentationReceipt&);

/**
 * Converts typed gameplay events into a bounded presentation stream.
 *
 * This subsystem owns request identity, deduplication, voice/effect limits, and recent receipts.
 * The transient actors it spawns cannot damage actors or change gameplay state.
 */
UCLASS()
class RTS_API URTSPresentationSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	URTSPresentationSubsystem();
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void ObserveSelection(URTSSelectionSubsystem& Selection);
	void ObserveCombatUnit(ARTSCombatUnit& Unit);
	void ObserveStructure(ARTSStructure& Structure);
	FRTSPresentationReceipt Present(const FRTSPresentationRequest& Request);
	FRTSPresentationSnapshot GetSnapshot();
	FRTSPresentationRequested& OnPresentationRequested();

private:
	struct FEventKey
	{
		ERTSPresentationEventKind Kind = ERTSPresentationEventKind::SelectionAcknowledged;
		ERTSPresentationSourceKind SourceKind = ERTSPresentationSourceKind::Selection;
		uint8 TeamId = FGenericTeamId::NoTeam.GetId();
		int32 StableSourceId = INDEX_NONE;
		int64 SourceEventSequence = 0;

		bool operator==(const FEventKey& Other) const
		{
			return Kind == Other.Kind
				&& SourceKind == Other.SourceKind
				&& TeamId == Other.TeamId
				&& StableSourceId == Other.StableSourceId
				&& SourceEventSequence == Other.SourceEventSequence;
		}

		friend uint32 GetTypeHash(const FEventKey& Key)
		{
			uint32 Hash = HashCombine(GetTypeHash(Key.Kind), GetTypeHash(Key.SourceKind));
			Hash = HashCombine(Hash, GetTypeHash(Key.TeamId));
			Hash = HashCombine(Hash, GetTypeHash(Key.StableSourceId));
			return HashCombine(Hash, GetTypeHash(Key.SourceEventSequence));
		}
	};

	void HandleSelectionChanged();
	void HandleCommandAccepted(const FRTSCommandResult& Result);
	void HandleConstructionCompleted(const FRTSStructureSnapshot& Snapshot);
	void HandleStructureDestroyed(const FRTSStructureSnapshot& Snapshot);
	void HandleMatchEventRecorded(const FRTSMatchEventReceipt& Receipt);
	void HandleWeaponFired(const FRTSWeaponEvent& Event);
	void PruneActiveEffects();
	void RetainReceipt(const FRTSPresentationReceipt& Receipt);
	float LifetimeFor(ERTSPresentationEventKind Kind) const;
	USoundBase* SoundFor(ERTSPresentationEventKind Kind) const;

	UPROPERTY(Transient)
	TObjectPtr<URTSSelectionSubsystem> PlayerSelection;
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ARTSTransientEffect>> ActiveEffects;
	UPROPERTY()
	TObjectPtr<USoundBase> SelectionSound;
	UPROPERTY()
	TObjectPtr<USoundBase> MoveSound;
	UPROPERTY()
	TObjectPtr<USoundBase> AttackSound;
	UPROPERTY()
	TObjectPtr<USoundBase> WeaponSound;
	UPROPERTY()
	TObjectPtr<USoundBase> DestructionSound;
	UPROPERTY()
	TObjectPtr<USoundBase> ConstructionSound;
	UPROPERTY()
	TObjectPtr<USoundBase> ProductionSound;
	UPROPERTY()
	TObjectPtr<USoundBase> ReclaimSound;
	UPROPERTY()
	TObjectPtr<USoundBase> VictorySound;
	UPROPERTY()
	TObjectPtr<USoundBase> DefeatSound;

	TSet<FEventKey> PresentedEvents;
	TArray<FEventKey> PresentedEventOrder;
	TArray<FRTSPresentationReceipt> RecentReceipts;
	TMap<uint8, FVector> LastDestructionLocationByTeam;
	FRTSPresentationRequested PresentationRequested;
	int32 NextReceiptSequence = 1;
	int64 NextSelectionEventSequence = 1;
	int32 EvictedEffectCount = 0;
	int32 AcceptedRequestCount = 0;
	int32 DuplicateRequestCount = 0;
	int32 MaximumActiveEffects = 48;
	int32 MaximumRetainedReceipts = 256;
	int32 MaximumTrackedEventKeys = 4096;
	float VolumeMultiplier = 0.45f;
	float AcknowledgementLifetimeSeconds = 0.45f;
	float WeaponLifetimeSeconds = 0.25f;
	float DestructionLifetimeSeconds = 0.9f;
	float CompletionLifetimeSeconds = 1.0f;
	float MatchLifetimeSeconds = 1.8f;
};
