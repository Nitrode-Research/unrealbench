// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "RTSWorldTypes.generated.h"

/** Read-only authored zone state used by deployment policy without exposing an actor lifetime. */
USTRUCT()
struct RTS_API FRTSStartingZoneSnapshot
{
	GENERATED_BODY()

	FGenericTeamId TeamId = FGenericTeamId::NoTeam;
	FVector Center = FVector::ZeroVector;
	float Radius = 0.0f;
};

/** Read-only deposit state; claim authority remains in the structure subsystem. */
USTRUCT()
struct RTS_API FRTSMaterialDepositSnapshot
{
	GENERATED_BODY()

	int32 StableDepositId = INDEX_NONE;
	FVector WorldLocation = FVector::ZeroVector;
	bool bClaimed = false;
};
