// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Configuration/RTSMilestone2Configuration.h"
#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "RTSStructureTypes.generated.h"

UENUM()
enum class ERTSDeploymentRefusal : uint8
{
	None,
	InvalidCommandVehicle,
	WrongUnitType,
	NoStartingZone,
	OutsideStartingZone,
	InvalidGround,
	FootprintBlocked,
	HeadquartersAlreadyDeployed,
	EconomyRejected,
	CommandIdentityRejected,
	SpawnFailed,
	MatchResolved
};

UENUM()
enum class ERTSPlacementRefusal : uint8
{
	None,
	InvalidTeam,
	HeadquartersRequired,
	HeadquartersCannotBePlaced,
	OutsideBuildArea,
	InvalidGround,
	FootprintBlocked,
	DepositRequired,
	DepositClaimed,
	FactoryExitBlocked,
	EconomyRejected,
	SpawnFailed,
	MatchResolved
};

USTRUCT()
struct RTS_API FRTSPlacementRequest
{
	GENERATED_BODY()

	FGenericTeamId TeamId = FGenericTeamId::NoTeam;
	ERTSStructureType StructureType = ERTSStructureType::PowerGenerator;
	FVector DesiredWorldLocation = FVector::ZeroVector;
};

USTRUCT()
struct RTS_API FRTSPlacementPreview
{
	GENERATED_BODY()

	bool bValid = false;
	ERTSPlacementRefusal Refusal = ERTSPlacementRefusal::InvalidTeam;
	ERTSStructureType StructureType = ERTSStructureType::PowerGenerator;
	FVector SnappedGroundLocation = FVector::ZeroVector;
	FIntPoint FootprintCells = FIntPoint::ZeroValue;
	float BuildAreaRadius = 0.0f;
	int32 StableDepositId = INDEX_NONE;
};

USTRUCT()
struct RTS_API FRTSPlacementResult
{
	GENERATED_BODY()

	bool bAccepted = false;
	ERTSPlacementRefusal Refusal = ERTSPlacementRefusal::InvalidTeam;
	int32 StableStructureId = INDEX_NONE;
	int64 MaterialTransactionId = 0;
};

USTRUCT()
struct RTS_API FRTSStructureSnapshot
{
	GENERATED_BODY()

	int32 StableStructureId = INDEX_NONE;
	ERTSStructureType StructureType = ERTSStructureType::Headquarters;
	FGenericTeamId TeamId = FGenericTeamId::NoTeam;
	FVector WorldLocation = FVector::ZeroVector;
	FIntPoint FootprintCells = FIntPoint::ZeroValue;
	bool bAlive = false;
	bool bConstructed = false;
	float ConstructionProgress = 0.0f;
};

USTRUCT()
struct RTS_API FRTSHeadquartersDeploymentPreview
{
	GENERATED_BODY()

	bool bValid = false;
	ERTSDeploymentRefusal Refusal = ERTSDeploymentRefusal::InvalidCommandVehicle;
	FVector GroundLocation = FVector::ZeroVector;
	FIntPoint FootprintCells = FIntPoint::ZeroValue;
	float BuildAreaRadius = 0.0f;
};

USTRUCT()
struct RTS_API FRTSHeadquartersDeploymentResult
{
	GENERATED_BODY()

	bool bAccepted = false;
	ERTSDeploymentRefusal Refusal = ERTSDeploymentRefusal::InvalidCommandVehicle;
	int32 StableStructureId = INDEX_NONE;
	int64 EconomyTransactionId = 0;
};
