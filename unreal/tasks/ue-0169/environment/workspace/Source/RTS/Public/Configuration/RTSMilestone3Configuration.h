// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RTSMilestone3Configuration.generated.h"

UENUM()
enum class ERTSAIProfileKind : uint8
{
	Normal,
	FastTest
};

/** Typed strategy cadence used by one normal-play or accelerated automation profile. */
USTRUCT()
struct RTS_API FRTSAIProfile
{
	GENERATED_BODY()

	ERTSAIProfileKind Kind = ERTSAIProfileKind::Normal;
	float DecisionIntervalSeconds = 1.0f;
	float MinimumCommitmentSeconds = 4.0f;
	float RetryCooldownSeconds = 2.0f;
	float HeadquartersCandidateGridSpacing = 1000.0f;
	float HeadquartersMaterialInfluenceRadius = 5000.0f;
	float HeadquartersBuildProbeDistance = 1800.0f;
	float HeadquartersArrivalTolerance = 150.0f;
	float HeadquartersMovementTimeoutSeconds = 30.0f;
	float DeployCommandStructureScore = 100.0f;
	float MaterialProximityWeight = 45.0f;
	float BuildSpaceWeight = 25.0f;
	float ApproachStandoffWeight = 10.0f;
	float RouteAccessWeight = 20.0f;
	int32 MaximumPlacementRetries = 4;
	float StructureCandidateGridSpacing = 500.0f;
	float StructureCandidateRadius = 4500.0f;
	int32 MaximumStructurePlacementRetries = 8;
	int32 DesiredMaterialExtractors = 1;
	int32 DesiredPowerGenerators = 1;
	int32 DesiredSupplyDepots = 1;
	int32 DesiredFactories = 1;
	int32 TargetInfantrySquads = 1;
	int32 TargetLightVehicles = 1;
	int32 TargetHeavyVehicles = 1;
	float BaseThreatRadius = 3500.0f;
	int32 MinimumDefenseUnits = 1;
	int32 MaximumDefenseUnits = 3;
	int32 DefenseReserveUnits = 1;
	int32 MinimumAttackGroupSize = 3;
	float AttackMusterDistance = 1800.0f;
	float AttackMusterTolerance = 500.0f;
	float AttackMusterTimeoutSeconds = 12.0f;
	float AttackCommitmentSeconds = 45.0f;
	int32 MinimumRaidsBeforeHeadquarters = 0;
	int32 MaximumDefensiveTurrets = 2;
	float TurretAnchorRadius = 1800.0f;
	float MinimumTurretSpacing = 1400.0f;
	float ReclaimSearchRadius = 5500.0f;
	float ReclaimSafetyRadius = 2500.0f;
	int32 StrategySeed = 1337;

	bool Validate(const TCHAR* ProfileLabel, TArray<FString>& OutErrors) const;
};

/** Hard budgets and lifetimes for presentation-only transient work. */
USTRUCT()
struct RTS_API FRTSPresentationTuning
{
	GENERATED_BODY()

	int32 MaximumActiveEffects = 48;
	int32 MaximumRetainedReceipts = 256;
	int32 MaximumTrackedEventKeys = 4096;
	float VolumeMultiplier = 0.45f;
	float AcknowledgementLifetimeSeconds = 0.45f;
	float WeaponLifetimeSeconds = 0.25f;
	float DestructionLifetimeSeconds = 0.9f;
	float CompletionLifetimeSeconds = 1.0f;
	float MatchLifetimeSeconds = 1.8f;

	bool Validate(TArray<FString>& OutErrors) const;
};

/** Closed, text-backed Milestone 3 tuning with distinct normal and fast-test profiles. */
USTRUCT()
struct RTS_API FRTSMilestone3Configuration
{
	GENERATED_BODY()

	FRTSAIProfile Normal;
	FRTSAIProfile FastTest;
	FRTSPresentationTuning Presentation;

	static FRTSMilestone3Configuration Load();
	bool Validate(TArray<FString>& OutErrors) const;
	const FRTSAIProfile& GetProfile(ERTSAIProfileKind Kind) const;
};
