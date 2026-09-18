// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "RTSCombatTypes.generated.h"

UENUM()
enum class ERTSWeaponSourceKind : uint8
{
	Unit,
	DefensiveTurret
};

UENUM()
enum class ERTSWeaponTargetKind : uint8
{
	Unit,
	Structure
};

/** Immutable receipt for one accepted weapon application. Presentation observes this, never drives it. */
USTRUCT()
struct RTS_API FRTSWeaponEvent
{
	GENERATED_BODY()

	ERTSWeaponSourceKind SourceKind = ERTSWeaponSourceKind::Unit;
	ERTSWeaponTargetKind TargetKind = ERTSWeaponTargetKind::Unit;
	FGenericTeamId SourceTeamId = FGenericTeamId::NoTeam;
	int32 StableSourceId = INDEX_NONE;
	int32 StableTargetId = INDEX_NONE;
	int32 SourceShotSequence = 0;
	FVector SourceWorldLocation = FVector::ZeroVector;
	FVector TargetWorldLocation = FVector::ZeroVector;
	float AppliedDamage = 0.0f;
	bool bDestroyedTarget = false;
};

/** Repository-readable Milestone 1 combat tuning loaded from DefaultGame.ini. */
USTRUCT()
struct RTS_API FRTSCombatTuning
{
	GENERATED_BODY()

	float MaximumHealth = 100.0f;
	float AttackDamage = 20.0f;
	float AttackRange = 650.0f;
	float AcquisitionRange = 1400.0f;
	float WeaponCooldownSeconds = 0.75f;
	float FacingDegreesPerSecond = 720.0f;
	float DeathCleanupDelaySeconds = 1.5f;
	float AttackFeedbackDurationSeconds = 0.15f;

	static FRTSCombatTuning Load();
};

USTRUCT()
struct RTS_API FRTSHealthSnapshot
{
	GENERATED_BODY()

	float MaximumHealth = 0.0f;
	float CurrentHealth = 0.0f;
	bool bDead = false;
};

USTRUCT()
struct RTS_API FRTSDamageResult
{
	GENERATED_BODY()

	bool bAccepted = false;
	bool bKilled = false;
	float AppliedDamage = 0.0f;
	float RemainingHealth = 0.0f;
};
