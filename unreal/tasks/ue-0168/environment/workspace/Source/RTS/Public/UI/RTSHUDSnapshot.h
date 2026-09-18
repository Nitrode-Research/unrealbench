// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Combat/RTSCombatTypes.h"
#include "Combat/RTSTurretCombatTypes.h"
#include "CoreMinimal.h"
#include "Economy/RTSEconomyTypes.h"
#include "Match/RTSMatchTypes.h"
#include "Orders/RTSOrderTypes.h"
#include "Production/RTSProductionTypes.h"
#include "Reclaim/RTSReclaimTypes.h"
#include "Structures/RTSStructureTypes.h"
#include "Units/RTSUnitTypes.h"

class ARTSPlayerController;

enum class ERTSHUDSelectionMode : uint8
{
	None,
	SingleUnit,
	SingleStructure,
	Multiple
};

enum class ERTSHUDMessageTone : uint8
{
	Neutral,
	Positive,
	Warning,
	Critical
};

enum class ERTSHUDActionKind : uint8
{
	Move,
	Attack,
	Reclaim,
	DeployHeadquarters,
	ProduceInfantry,
	ProduceLightVehicle,
	ProduceHeavyVehicle
};

struct RTS_API FRTSHUDActionView
{
	ERTSHUDActionKind Kind = ERTSHUDActionKind::Move;
	FString Hotkey;
	FString Label;
};

/** One ephemeral projection of authoritative gameplay snapshots for the native Slate HUD. */
struct RTS_API FRTSHUDSnapshot
{
	bool bAvailable = false;
	FRTSEconomySnapshot Economy;
	FRTSMatchSnapshot Match;
	ERTSHUDSelectionMode SelectionMode = ERTSHUDSelectionMode::None;
	int32 SelectedUnitCount = 0;
	int32 SelectedStructureCount = 0;
	int32 SelectedStableUnitId = INDEX_NONE;
	int32 SelectedStableStructureId = INDEX_NONE;
	ERTSUnitType SelectedUnitType = ERTSUnitType::InfantrySquad;
	ERTSStructureType SelectedStructureType = ERTSStructureType::Headquarters;
	FRTSHealthSnapshot SelectedHealth;
	FRTSOrderSnapshot SelectedOrder;
	TOptional<FRTSProductionSnapshot> Production;
	TOptional<FRTSTurretCombatSnapshot> Turret;
	TOptional<FRTSReclaimSnapshot> Reclaim;

	FString MaterialsText;
	FString PowerText;
	FString SupplyText;
	FString SelectionTitle;
	FString SelectionDetail;
	FString QueueText;
	FString AvailableActionsText;
	FString ContextText;
	FString WarningText;
	FString ResultTitle;
	FString ResultReason;
	FString ResultDuration;
	FString PlayerResultStatistics;
	FString EnemyResultStatistics;
	TArray<FRTSHUDActionView> Actions;
	ERTSHUDMessageTone ContextTone = ERTSHUDMessageTone::Neutral;
	ERTSHUDMessageTone WarningTone = ERTSHUDMessageTone::Neutral;
	bool bPlayerVictory = false;
};

/** Captures live state without retaining it; gameplay modules remain the only state owners. */
class RTS_API FRTSHUDSnapshotAdapter final
{
public:
	static FRTSHUDSnapshot Capture(const ARTSPlayerController* PlayerController);
};
