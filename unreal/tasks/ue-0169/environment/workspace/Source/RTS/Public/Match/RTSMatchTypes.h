// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "RTSMatchTypes.generated.h"

UENUM()
enum class ERTSMatchState : uint8
{
	InProgress,
	Resolved
};

UENUM()
enum class ERTSMatchResolution : uint8
{
	None,
	CommandVehicleDestroyed,
	HeadquartersDestroyed
};

UENUM()
enum class ERTSMatchEventKind : uint8
{
	UnitProduced,
	UnitLost,
	MaterialsReclaimed,
	MatchResolved
};

USTRUCT()
struct RTS_API FRTSFactionMatchStatistics
{
	GENERATED_BODY()

	FGenericTeamId TeamId = FGenericTeamId::NoTeam;
	int32 UnitsProduced = 0;
	int32 UnitsLost = 0;
	int32 MaterialsReclaimed = 0;
};

USTRUCT()
struct RTS_API FRTSMatchEventReceipt
{
	GENERATED_BODY()

	bool bAccepted = false;
	ERTSMatchEventKind Kind = ERTSMatchEventKind::UnitProduced;
	FGenericTeamId TeamId = FGenericTeamId::NoTeam;
	int32 StableSourceId = INDEX_NONE;
	int32 Amount = 0;
	int32 EventSequence = 0;
};

USTRUCT()
struct RTS_API FRTSMatchSnapshot
{
	GENERATED_BODY()

	ERTSMatchState State = ERTSMatchState::InProgress;
	ERTSMatchResolution Resolution = ERTSMatchResolution::None;
	FGenericTeamId WinningTeamId = FGenericTeamId::NoTeam;
	FGenericTeamId LosingTeamId = FGenericTeamId::NoTeam;
	int32 ResolutionSequence = 0;
	double StartTimeSeconds = 0.0;
	double ResolutionTimeSeconds = 0.0;
	double ActiveDurationSeconds = 0.0;
	FRTSFactionMatchStatistics PlayerStatistics;
	FRTSFactionMatchStatistics EnemyStatistics;
};

USTRUCT()
struct RTS_API FRTSMatchResult
{
	GENERATED_BODY()

	bool bAccepted = false;
	FRTSMatchSnapshot Snapshot;
};
