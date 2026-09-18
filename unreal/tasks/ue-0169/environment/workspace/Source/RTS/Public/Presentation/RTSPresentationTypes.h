// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "RTSPresentationTypes.generated.h"

UENUM()
enum class ERTSPresentationEventKind : uint8
{
	SelectionAcknowledged,
	MoveAcknowledged,
	AttackAcknowledged,
	WeaponImpact,
	Destruction,
	ConstructionCompleted,
	ProductionCompleted,
	ReclaimCompleted,
	Victory,
	Defeat
};

UENUM()
enum class ERTSPresentationSourceKind : uint8
{
	Selection,
	Command,
	UnitWeapon,
	TurretWeapon,
	Unit,
	Structure,
	Wreckage,
	Match
};

/** One immutable presentation request derived from an already-accepted authoritative event. */
USTRUCT()
struct RTS_API FRTSPresentationRequest
{
	GENERATED_BODY()

	ERTSPresentationEventKind Kind = ERTSPresentationEventKind::SelectionAcknowledged;
	ERTSPresentationSourceKind SourceKind = ERTSPresentationSourceKind::Selection;
	FGenericTeamId TeamId = FGenericTeamId::NoTeam;
	int32 StableSourceId = INDEX_NONE;
	int32 StableTargetId = INDEX_NONE;
	int64 SourceEventSequence = 0;
	FVector SourceWorldLocation = FVector::ZeroVector;
	FVector TargetWorldLocation = FVector::ZeroVector;
	float Magnitude = 0.0f;
	float LifetimeSeconds = 0.0f;
};

USTRUCT()
struct RTS_API FRTSPresentationReceipt
{
	GENERATED_BODY()

	bool bAccepted = false;
	bool bDuplicate = false;
	bool bVisualSpawned = false;
	bool bAudioAvailable = false;
	int32 ReceiptSequence = 0;
	int32 ActiveEffectsAfterRequest = 0;
	FRTSPresentationRequest Request;
};

USTRUCT()
struct RTS_API FRTSPresentationSnapshot
{
	GENERATED_BODY()

	int32 ActiveEffectCount = 0;
	int32 MaximumActiveEffects = 0;
	int32 TrackedEventCount = 0;
	int32 MaximumTrackedEvents = 0;
	int32 MaximumRetainedReceipts = 0;
	int32 EvictedEffectCount = 0;
	int32 AcceptedRequestCount = 0;
	int32 DuplicateRequestCount = 0;
	TArray<FRTSPresentationReceipt> RecentReceipts;
};
