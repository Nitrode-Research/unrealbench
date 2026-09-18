// Copyright Epic Games, Inc. All Rights Reserved.

#include "Configuration/RTSMilestone3Configuration.h"

#include "Misc/ConfigCacheIni.h"

namespace RTSMilestone3ConfigurationPrivate
{
void LoadProfile(const TCHAR* Section, const ERTSAIProfileKind Kind, FRTSAIProfile& Profile)
{
	Profile.Kind = Kind;
	GConfig->GetFloat(Section, TEXT("DecisionIntervalSeconds"), Profile.DecisionIntervalSeconds, GGameIni);
	GConfig->GetFloat(Section, TEXT("MinimumCommitmentSeconds"), Profile.MinimumCommitmentSeconds, GGameIni);
	GConfig->GetFloat(Section, TEXT("RetryCooldownSeconds"), Profile.RetryCooldownSeconds, GGameIni);
	GConfig->GetFloat(
		Section,
		TEXT("HeadquartersCandidateGridSpacing"),
		Profile.HeadquartersCandidateGridSpacing,
		GGameIni);
	GConfig->GetFloat(
		Section,
		TEXT("HeadquartersMaterialInfluenceRadius"),
		Profile.HeadquartersMaterialInfluenceRadius,
		GGameIni);
	GConfig->GetFloat(
		Section,
		TEXT("HeadquartersBuildProbeDistance"),
		Profile.HeadquartersBuildProbeDistance,
		GGameIni);
	GConfig->GetFloat(
		Section,
		TEXT("HeadquartersArrivalTolerance"),
		Profile.HeadquartersArrivalTolerance,
		GGameIni);
	GConfig->GetFloat(
		Section,
		TEXT("HeadquartersMovementTimeoutSeconds"),
		Profile.HeadquartersMovementTimeoutSeconds,
		GGameIni);
	GConfig->GetFloat(
		Section,
		TEXT("DeployCommandStructureScore"),
		Profile.DeployCommandStructureScore,
		GGameIni);
	GConfig->GetFloat(Section, TEXT("MaterialProximityWeight"), Profile.MaterialProximityWeight, GGameIni);
	GConfig->GetFloat(Section, TEXT("BuildSpaceWeight"), Profile.BuildSpaceWeight, GGameIni);
	GConfig->GetFloat(Section, TEXT("ApproachStandoffWeight"), Profile.ApproachStandoffWeight, GGameIni);
	GConfig->GetFloat(Section, TEXT("RouteAccessWeight"), Profile.RouteAccessWeight, GGameIni);
	GConfig->GetInt(Section, TEXT("MaximumPlacementRetries"), Profile.MaximumPlacementRetries, GGameIni);
	GConfig->GetFloat(
		Section,
		TEXT("StructureCandidateGridSpacing"),
		Profile.StructureCandidateGridSpacing,
		GGameIni);
	GConfig->GetFloat(
		Section,
		TEXT("StructureCandidateRadius"),
		Profile.StructureCandidateRadius,
		GGameIni);
	GConfig->GetInt(
		Section,
		TEXT("MaximumStructurePlacementRetries"),
		Profile.MaximumStructurePlacementRetries,
		GGameIni);
	GConfig->GetInt(Section, TEXT("DesiredMaterialExtractors"), Profile.DesiredMaterialExtractors, GGameIni);
	GConfig->GetInt(Section, TEXT("DesiredPowerGenerators"), Profile.DesiredPowerGenerators, GGameIni);
	GConfig->GetInt(Section, TEXT("DesiredSupplyDepots"), Profile.DesiredSupplyDepots, GGameIni);
	GConfig->GetInt(Section, TEXT("DesiredFactories"), Profile.DesiredFactories, GGameIni);
	GConfig->GetInt(Section, TEXT("TargetInfantrySquads"), Profile.TargetInfantrySquads, GGameIni);
	GConfig->GetInt(Section, TEXT("TargetLightVehicles"), Profile.TargetLightVehicles, GGameIni);
	GConfig->GetInt(Section, TEXT("TargetHeavyVehicles"), Profile.TargetHeavyVehicles, GGameIni);
	GConfig->GetFloat(Section, TEXT("BaseThreatRadius"), Profile.BaseThreatRadius, GGameIni);
	GConfig->GetInt(Section, TEXT("MinimumDefenseUnits"), Profile.MinimumDefenseUnits, GGameIni);
	GConfig->GetInt(Section, TEXT("MaximumDefenseUnits"), Profile.MaximumDefenseUnits, GGameIni);
	GConfig->GetInt(Section, TEXT("DefenseReserveUnits"), Profile.DefenseReserveUnits, GGameIni);
	GConfig->GetInt(Section, TEXT("MinimumAttackGroupSize"), Profile.MinimumAttackGroupSize, GGameIni);
	GConfig->GetFloat(Section, TEXT("AttackMusterDistance"), Profile.AttackMusterDistance, GGameIni);
	GConfig->GetFloat(Section, TEXT("AttackMusterTolerance"), Profile.AttackMusterTolerance, GGameIni);
	GConfig->GetFloat(Section, TEXT("AttackMusterTimeoutSeconds"), Profile.AttackMusterTimeoutSeconds, GGameIni);
	GConfig->GetFloat(Section, TEXT("AttackCommitmentSeconds"), Profile.AttackCommitmentSeconds, GGameIni);
	GConfig->GetInt(Section, TEXT("MinimumRaidsBeforeHeadquarters"), Profile.MinimumRaidsBeforeHeadquarters, GGameIni);
	GConfig->GetInt(Section, TEXT("MaximumDefensiveTurrets"), Profile.MaximumDefensiveTurrets, GGameIni);
	GConfig->GetFloat(Section, TEXT("TurretAnchorRadius"), Profile.TurretAnchorRadius, GGameIni);
	GConfig->GetFloat(Section, TEXT("MinimumTurretSpacing"), Profile.MinimumTurretSpacing, GGameIni);
	GConfig->GetFloat(Section, TEXT("ReclaimSearchRadius"), Profile.ReclaimSearchRadius, GGameIni);
	GConfig->GetFloat(Section, TEXT("ReclaimSafetyRadius"), Profile.ReclaimSafetyRadius, GGameIni);
	GConfig->GetInt(Section, TEXT("StrategySeed"), Profile.StrategySeed, GGameIni);
}

void LoadPresentation(FRTSPresentationTuning& Presentation)
{
	constexpr TCHAR Section[] = TEXT("RTS.Milestone3.Presentation");
	GConfig->GetInt(Section, TEXT("MaximumActiveEffects"), Presentation.MaximumActiveEffects, GGameIni);
	GConfig->GetInt(Section, TEXT("MaximumRetainedReceipts"), Presentation.MaximumRetainedReceipts, GGameIni);
	GConfig->GetInt(Section, TEXT("MaximumTrackedEventKeys"), Presentation.MaximumTrackedEventKeys, GGameIni);
	GConfig->GetFloat(Section, TEXT("VolumeMultiplier"), Presentation.VolumeMultiplier, GGameIni);
	GConfig->GetFloat(Section, TEXT("AcknowledgementLifetimeSeconds"), Presentation.AcknowledgementLifetimeSeconds, GGameIni);
	GConfig->GetFloat(Section, TEXT("WeaponLifetimeSeconds"), Presentation.WeaponLifetimeSeconds, GGameIni);
	GConfig->GetFloat(Section, TEXT("DestructionLifetimeSeconds"), Presentation.DestructionLifetimeSeconds, GGameIni);
	GConfig->GetFloat(Section, TEXT("CompletionLifetimeSeconds"), Presentation.CompletionLifetimeSeconds, GGameIni);
	GConfig->GetFloat(Section, TEXT("MatchLifetimeSeconds"), Presentation.MatchLifetimeSeconds, GGameIni);
}
}

bool FRTSAIProfile::Validate(const TCHAR* ProfileLabel, TArray<FString>& OutErrors) const
{
	const int32 StartingErrorCount = OutErrors.Num();
	if (DecisionIntervalSeconds <= 0.0f
		|| MinimumCommitmentSeconds < 0.0f
		|| RetryCooldownSeconds < 0.0f)
	{
		OutErrors.Add(FString::Printf(
			TEXT("%s requires a positive decision interval and non-negative commitment and retry times."),
			ProfileLabel));
	}
	if (HeadquartersCandidateGridSpacing <= 0.0f
		|| HeadquartersMaterialInfluenceRadius <= 0.0f
		|| HeadquartersBuildProbeDistance <= 0.0f
		|| HeadquartersArrivalTolerance <= 0.0f
		|| HeadquartersMovementTimeoutSeconds <= 0.0f
		|| DeployCommandStructureScore <= 0.0f
		|| MaximumPlacementRetries <= 0)
	{
		OutErrors.Add(FString::Printf(
			TEXT("%s requires positive HQ spatial values, deployment score, and placement retries."),
			ProfileLabel));
	}
	if (MaterialProximityWeight < 0.0f
		|| BuildSpaceWeight < 0.0f
		|| ApproachStandoffWeight < 0.0f
		|| RouteAccessWeight < 0.0f)
	{
		OutErrors.Add(FString::Printf(TEXT("%s HQ scoring weights cannot be negative."), ProfileLabel));
	}
	if (StructureCandidateGridSpacing <= 0.0f
		|| StructureCandidateRadius < StructureCandidateGridSpacing
		|| MaximumStructurePlacementRetries <= 0
		|| DesiredMaterialExtractors <= 0
		|| DesiredPowerGenerators <= 0
		|| DesiredSupplyDepots <= 0
		|| DesiredFactories <= 0
		|| TargetInfantrySquads <= 0
		|| TargetLightVehicles <= 0
		|| TargetHeavyVehicles <= 0)
	{
		OutErrors.Add(FString::Printf(
			TEXT("%s requires positive economy targets, placement retries, and a usable structure grid."),
			ProfileLabel));
	}
	if (BaseThreatRadius <= 0.0f
		|| MinimumDefenseUnits <= 0
		|| MaximumDefenseUnits < MinimumDefenseUnits
		|| DefenseReserveUnits <= 0
		|| MinimumAttackGroupSize < 2
		|| AttackMusterDistance <= 0.0f
		|| AttackMusterTolerance <= 0.0f
		|| AttackMusterTimeoutSeconds <= 0.0f
		|| AttackCommitmentSeconds <= AttackMusterTimeoutSeconds
		|| MinimumRaidsBeforeHeadquarters < 0
		|| MaximumDefensiveTurrets <= 0
		|| TurretAnchorRadius <= 0.0f
		|| MinimumTurretSpacing <= 0.0f
		|| ReclaimSearchRadius <= 0.0f
		|| ReclaimSafetyRadius <= 0.0f)
	{
		OutErrors.Add(FString::Printf(
			TEXT("%s requires consistent positive defense, attack, turret, and reclaim tuning."),
			ProfileLabel));
	}
	return OutErrors.Num() == StartingErrorCount;
}

bool FRTSPresentationTuning::Validate(TArray<FString>& OutErrors) const
{
	const int32 StartingErrorCount = OutErrors.Num();
	if (MaximumActiveEffects <= 0
		|| MaximumRetainedReceipts <= 0
		|| MaximumTrackedEventKeys < MaximumRetainedReceipts)
	{
		OutErrors.Add(TEXT("Presentation requires positive effect and receipt budgets, with enough tracked event keys for retained receipts."));
	}
	if (VolumeMultiplier < 0.0f
		|| VolumeMultiplier > 1.0f
		|| AcknowledgementLifetimeSeconds <= 0.0f
		|| WeaponLifetimeSeconds <= 0.0f
		|| DestructionLifetimeSeconds <= 0.0f
		|| CompletionLifetimeSeconds <= 0.0f
		|| MatchLifetimeSeconds <= 0.0f)
	{
		OutErrors.Add(TEXT("Presentation volume must be within zero and one and every transient lifetime must be positive."));
	}
	return OutErrors.Num() == StartingErrorCount;
}

FRTSMilestone3Configuration FRTSMilestone3Configuration::Load()
{
	FRTSMilestone3Configuration Configuration;
	RTSMilestone3ConfigurationPrivate::LoadProfile(
		TEXT("RTS.Milestone3.AI.Normal"),
		ERTSAIProfileKind::Normal,
		Configuration.Normal);
	RTSMilestone3ConfigurationPrivate::LoadProfile(
		TEXT("RTS.Milestone3.AI.FastTest"),
		ERTSAIProfileKind::FastTest,
		Configuration.FastTest);
	RTSMilestone3ConfigurationPrivate::LoadPresentation(Configuration.Presentation);
	return Configuration;
}

bool FRTSMilestone3Configuration::Validate(TArray<FString>& OutErrors) const
{
	OutErrors.Reset();
	Normal.Validate(TEXT("Normal AI profile"), OutErrors);
	FastTest.Validate(TEXT("Fast-test AI profile"), OutErrors);
	Presentation.Validate(OutErrors);
	if (Normal.Kind != ERTSAIProfileKind::Normal || FastTest.Kind != ERTSAIProfileKind::FastTest)
	{
		OutErrors.Add(TEXT("Milestone 3 AI profiles have mismatched typed identities."));
	}
	if (FastTest.DecisionIntervalSeconds > Normal.DecisionIntervalSeconds)
	{
		OutErrors.Add(TEXT("Fast-test AI cadence cannot be slower than normal play."));
	}
	return OutErrors.IsEmpty();
}

const FRTSAIProfile& FRTSMilestone3Configuration::GetProfile(const ERTSAIProfileKind Kind) const
{
	return Kind == ERTSAIProfileKind::FastTest ? FastTest : Normal;
}
