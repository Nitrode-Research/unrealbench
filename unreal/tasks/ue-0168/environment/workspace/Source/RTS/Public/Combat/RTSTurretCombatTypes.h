// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RTSTurretCombatTypes.generated.h"

class ARTSCombatUnit;

UENUM()
enum class ERTSTurretState : uint8
{
	UnderConstruction,
	Unpowered,
	Searching,
	Firing
};

USTRUCT()
struct RTS_API FRTSTurretCombatSnapshot
{
	GENERATED_BODY()

	ERTSTurretState State = ERTSTurretState::UnderConstruction;
	TWeakObjectPtr<ARTSCombatUnit> Target;
	int32 TargetStableUnitId = INDEX_NONE;
	float AttackRange = 0.0f;
	int32 ShotsFired = 0;
};
