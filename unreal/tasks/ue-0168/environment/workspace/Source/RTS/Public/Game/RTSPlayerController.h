// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Configuration/RTSMilestone2Configuration.h"
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "RTSPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class ARTSCombatUnit;
class ARTSStructure;
class ARTSWreckage;
struct FInputActionValue;

/** Native viewport adapter for camera input and selection gestures. */
UCLASS()
class RTS_API ARTSPlayerController final : public APlayerController, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ARTSPlayerController();
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FGenericTeamId GetGenericTeamId() const override;

	void SelectAtScreenPosition(const FVector2D& ScreenPosition, bool bToggleSelection);
	void SelectInsideScreenRect(const FBox2D& ScreenRect, bool bToggleSelection);
	void IssueContextCommandAtScreenPosition(const FVector2D& ScreenPosition);

private:
	void CreateInputConfiguration();
	void HandlePanInput(const FInputActionValue& Value);
	void HandleZoomInput(const FInputActionValue& Value);
	void HandleSelectionStarted(const FInputActionValue& Value);
	void HandleSelectionCompleted(const FInputActionValue& Value);
	void HandleContextCommandStarted(const FInputActionValue& Value);
	void HandleDeploymentStarted(const FInputActionValue& Value);
	void HandleExtractorPlacementStarted(const FInputActionValue& Value);
	void HandleGeneratorPlacementStarted(const FInputActionValue& Value);
	void HandleFactoryPlacementStarted(const FInputActionValue& Value);
	void HandleSupplyDepotPlacementStarted(const FInputActionValue& Value);
	void HandleTurretPlacementStarted(const FInputActionValue& Value);
	void HandleInfantryProductionStarted(const FInputActionValue& Value);
	void HandleLightVehicleProductionStarted(const FInputActionValue& Value);
	void HandleHeavyVehicleProductionStarted(const FInputActionValue& Value);
	void HandleCancelStarted(const FInputActionValue& Value);
	void BeginStructurePlacement(ERTSStructureType StructureType);
	void UpdateStructurePlacementPreview();
	void UpdateWorldOverlays();
	void UpdateEdgePan();
	void UpdateSelectionMarquee();
	ARTSCombatUnit* FindUnitAtScreenPosition(const FVector2D& ScreenPosition);
	ARTSStructure* FindStructureAtScreenPosition(const FVector2D& ScreenPosition);
	ARTSWreckage* FindWreckageAtScreenPosition(const FVector2D& ScreenPosition);
	void TryEnqueueFocusedFactory(ERTSUnitType UnitType);
	bool TryGetMousePosition(FVector2D& ScreenPosition) const;
	class ARTSCameraPawn* GetRTSCameraPawn() const;
	class ARTSHUD* GetRTSHUD() const;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> InputMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CameraPanAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CameraZoomAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SelectAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ContextCommandAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DeploymentAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ExtractorPlacementAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> GeneratorPlacementAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> FactoryPlacementAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SupplyDepotPlacementAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TurretPlacementAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> InfantryProductionAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LightVehicleProductionAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> HeavyVehicleProductionAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CancelAction;

	FGenericTeamId TeamId;
	FVector2D SelectionStart = FVector2D::ZeroVector;
	bool bSelectionHeld = false;
};
