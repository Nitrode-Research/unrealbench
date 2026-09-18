// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/RTSCombatTypes.h"

#include "Misc/ConfigCacheIni.h"

FRTSCombatTuning FRTSCombatTuning::Load()
{
	FRTSCombatTuning Tuning;
	constexpr TCHAR Section[] = TEXT("RTS.Milestone1.Combat");
	GConfig->GetFloat(Section, TEXT("MaximumHealth"), Tuning.MaximumHealth, GGameIni);
	GConfig->GetFloat(Section, TEXT("AttackDamage"), Tuning.AttackDamage, GGameIni);
	GConfig->GetFloat(Section, TEXT("AttackRange"), Tuning.AttackRange, GGameIni);
	GConfig->GetFloat(Section, TEXT("AcquisitionRange"), Tuning.AcquisitionRange, GGameIni);
	GConfig->GetFloat(
		Section,
		TEXT("WeaponCooldownSeconds"),
		Tuning.WeaponCooldownSeconds,
		GGameIni);
	GConfig->GetFloat(
		Section,
		TEXT("FacingDegreesPerSecond"),
		Tuning.FacingDegreesPerSecond,
		GGameIni);
	GConfig->GetFloat(
		Section,
		TEXT("DeathCleanupDelaySeconds"),
		Tuning.DeathCleanupDelaySeconds,
		GGameIni);
	GConfig->GetFloat(
		Section,
		TEXT("AttackFeedbackDurationSeconds"),
		Tuning.AttackFeedbackDurationSeconds,
		GGameIni);

	Tuning.MaximumHealth = FMath::Max(1.0f, Tuning.MaximumHealth);
	Tuning.AttackDamage = FMath::Max(0.0f, Tuning.AttackDamage);
	Tuning.AttackRange = FMath::Max(1.0f, Tuning.AttackRange);
	Tuning.AcquisitionRange = FMath::Max(Tuning.AttackRange, Tuning.AcquisitionRange);
	Tuning.WeaponCooldownSeconds = FMath::Max(0.01f, Tuning.WeaponCooldownSeconds);
	Tuning.FacingDegreesPerSecond = FMath::Max(1.0f, Tuning.FacingDegreesPerSecond);
	Tuning.DeathCleanupDelaySeconds = FMath::Max(0.0f, Tuning.DeathCleanupDelaySeconds);
	Tuning.AttackFeedbackDurationSeconds = FMath::Max(
		0.01f,
		Tuning.AttackFeedbackDurationSeconds);
	return Tuning;
}
