// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Configuration/RTSMilestone2Configuration.h"
#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "Orders/RTSOrderTypes.h"
#include "RTSUnitTypes.generated.h"

USTRUCT()
struct RTS_API FRTSUnitSnapshot
{
	GENERATED_BODY()

	int32 StableUnitId = INDEX_NONE;
	ERTSUnitType UnitType = ERTSUnitType::InfantrySquad;
	FGenericTeamId TeamId = FGenericTeamId::NoTeam;
	FVector WorldLocation = FVector::ZeroVector;
	float CurrentHealth = 0.0f;
	float MaximumHealth = 0.0f;
	ERTSOrderKind OrderKind = ERTSOrderKind::None;
	ERTSOrderPhase OrderPhase = ERTSOrderPhase::Idle;
	bool bAlive = false;
	bool bAvailableForOrders = false;
};
