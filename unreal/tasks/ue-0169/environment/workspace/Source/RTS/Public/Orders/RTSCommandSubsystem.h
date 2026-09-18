// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Orders/RTSOrderTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSCommandSubsystem.generated.h"

class ARTSCombatUnit;
class ARTSStructure;
class ARTSWreckage;

DECLARE_MULTICAST_DELEGATE_OneParam(FRTSCommandAccepted, const FRTSCommandResult&);

/** Non-ticking world ownership seam for validated group move and attack commands. */
UCLASS()
class RTS_API URTSCommandSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	FRTSCommandResult IssueMove(
		TConstArrayView<ARTSCombatUnit*> Units,
		const FVector& RequestedDestination);

	FRTSCommandResult IssueAttack(
		TConstArrayView<ARTSCombatUnit*> Units,
		ARTSCombatUnit& RequestedTarget);
	FRTSCommandResult IssueAttack(
		TConstArrayView<ARTSCombatUnit*> Units,
		ARTSStructure& RequestedTarget);
	FRTSCommandResult IssueReclaim(
		TConstArrayView<ARTSCombatUnit*> Units,
		ARTSWreckage& RequestedTarget);

	TOptional<FVector> GetLastMoveDestination() const;
	FRTSCommandAccepted& OnCommandAccepted();

private:
	int64 NextGroupCommandId = 1;
	TOptional<FVector> LastMoveDestination;
	FRTSCommandAccepted CommandAccepted;
};
