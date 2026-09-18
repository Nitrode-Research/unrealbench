// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Structures/RTSStructureTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "RTSDeploymentViewSubsystem.generated.h"

class ARTSCombatUnit;

/** Per-player preview state; authoritative legality remains in the world structure subsystem. */
UCLASS()
class RTS_API URTSDeploymentViewSubsystem final : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	void BeginHeadquartersPreview(ARTSCombatUnit& CommandVehicle);
	void CancelPreview();
	bool IsPreviewActive() const;
	ARTSCombatUnit* GetCommandVehicle() const;
	void SetLastResult(const FRTSHeadquartersDeploymentResult& Result);
	TOptional<FRTSHeadquartersDeploymentResult> GetLastResult() const;
	void BeginStructurePreview(ERTSStructureType StructureType, FGenericTeamId TeamId);
	void UpdateStructurePreviewLocation(const FVector& DesiredWorldLocation);
	bool IsStructurePreviewActive() const;
	TOptional<FRTSPlacementRequest> GetPlacementRequest() const;
	void SetLastPlacementResult(const FRTSPlacementResult& Result);
	TOptional<FRTSPlacementResult> GetLastPlacementResult() const;

private:
	TWeakObjectPtr<ARTSCombatUnit> PreviewCommandVehicle;
	TOptional<FRTSHeadquartersDeploymentResult> LastResult;
	TOptional<FRTSPlacementRequest> PlacementRequest;
	TOptional<FRTSPlacementResult> LastPlacementResult;
};
