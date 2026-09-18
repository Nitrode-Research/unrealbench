// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "Match/RTSMatchTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSMatchSubsystem.generated.h"

class ARTSCombatUnit;
class ARTSStructure;

DECLARE_MULTICAST_DELEGATE_OneParam(FRTSMatchResolved, const FRTSMatchSnapshot&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRTSMatchEventRecorded, const FRTSMatchEventReceipt&);

/** Owns each faction's singular command identity without string or tag dispatch. */
UCLASS()
class RTS_API URTSMatchSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	bool RegisterCommandVehicle(ARTSCombatUnit& CommandVehicle);
	bool PromoteCommandVehicleToHeadquarters(ARTSCombatUnit& CommandVehicle, ARTSStructure& Headquarters);
	bool RevertHeadquartersPromotion(ARTSCombatUnit& CommandVehicle, ARTSStructure& Headquarters);
	FRTSMatchResult ReportCommandVehicleDestroyed(ARTSCombatUnit& CommandVehicle);
	FRTSMatchResult ReportHeadquartersDestroyed(ARTSStructure& Headquarters);
	FRTSMatchEventReceipt ReportUnitProduced(FGenericTeamId TeamId, int32 StableUnitId);
	FRTSMatchEventReceipt ReportUnitLost(FGenericTeamId TeamId, int32 StableUnitId);
	FRTSMatchEventReceipt ReportMaterialsReclaimed(
		FGenericTeamId TeamId,
		int32 StableWreckageId,
		int32 MaterialAmount);
	ARTSCombatUnit* GetCommandVehicle(FGenericTeamId TeamId) const;
	ARTSStructure* GetHeadquarters(FGenericTeamId TeamId) const;
	FRTSMatchSnapshot GetSnapshot() const;
	bool IsResolved() const;
	FRTSMatchResolved& OnMatchResolved();
	FRTSMatchEventRecorded& OnMatchEventRecorded();

private:
	FRTSMatchEventReceipt RecordEvent(
		ERTSMatchEventKind Kind,
		FGenericTeamId TeamId,
		int32 StableSourceId,
		int32 Amount);
	FRTSFactionMatchStatistics* FindStatistics(FGenericTeamId TeamId);
	FRTSMatchResult Resolve(FGenericTeamId LosingTeamId, ERTSMatchResolution Resolution);
	static FGenericTeamId OpponentOf(FGenericTeamId TeamId);

	TMap<uint8, TWeakObjectPtr<ARTSCombatUnit>> CommandVehicles;
	TMap<uint8, TWeakObjectPtr<ARTSStructure>> HeadquartersByTeam;
	FRTSMatchSnapshot Snapshot;
	FRTSMatchResolved MatchResolved;
	FRTSMatchEventRecorded MatchEventRecorded;
	TSet<uint64> RecordedEventKeys;
	int32 NextEventSequence = 1;
};
