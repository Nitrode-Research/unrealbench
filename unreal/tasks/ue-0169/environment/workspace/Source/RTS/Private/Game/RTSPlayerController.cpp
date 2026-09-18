// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/RTSPlayerController.h"

#include "Camera/RTSCameraPawn.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Game/RTSHUD.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Orders/RTSCommandSubsystem.h"
#include "Production/RTSProductionSubsystem.h"
#include "Presentation/RTSWorldOverlaySubsystem.h"
#include "Presentation/RTSPresentationSubsystem.h"
#include "Reclaim/RTSWreckage.h"
#include "Selection/RTSSelectionSubsystem.h"
#include "Structures/RTSDeploymentViewSubsystem.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "UnrealClient.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"
#include "World/RTSMaterialDeposit.h"

namespace
{
constexpr float SelectionDragThreshold = 6.0f;
constexpr float EdgePanThreshold = 18.0f;
constexpr float UnitStatusTargetRadius = 24.0f;

FBox2D NormalizeScreenRect(const FBox2D& ScreenRect)
{
	return FBox2D(
		FVector2D(
			FMath::Min(ScreenRect.Min.X, ScreenRect.Max.X),
			FMath::Min(ScreenRect.Min.Y, ScreenRect.Max.Y)),
		FVector2D(
			FMath::Max(ScreenRect.Min.X, ScreenRect.Max.X),
			FMath::Max(ScreenRect.Min.Y, ScreenRect.Max.Y)));
}

void AddPanKeyMapping(
	UInputMappingContext& MappingContext,
	const UInputAction& PanAction,
	const FKey Key,
	const bool bMapToVerticalAxis,
	const bool bNegate)
{
	FEnhancedActionKeyMapping& Mapping = MappingContext.MapKey(&PanAction, Key);
	if (bMapToVerticalAxis)
	{
		Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(&MappingContext));
	}
	if (bNegate)
	{
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(&MappingContext));
	}
}
}

ARTSPlayerController::ARTSPlayerController()
	: TeamId(RTSTeams::Player)
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void ARTSPlayerController::BeginPlay()
{
	// Restore this contractor-owned implementation.
	Super::BeginPlay();
}

void ARTSPlayerController::SetupInputComponent()
{
	// Restore this contractor-owned implementation.
	Super::SetupInputComponent();
}

void ARTSPlayerController::PlayerTick(const float DeltaTime)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	// Restore this contractor-owned implementation.
}

FGenericTeamId ARTSPlayerController::GetGenericTeamId() const
{
	return TeamId;
}

void ARTSPlayerController::SelectAtScreenPosition(
	const FVector2D& ScreenPosition,
	const bool bToggleSelection)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::SelectInsideScreenRect(
	const FBox2D& ScreenRect,
	const bool bToggleSelection)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::IssueContextCommandAtScreenPosition(const FVector2D& ScreenPosition)
{
	// Restore this contractor-owned implementation.
}

ARTSCombatUnit* ARTSPlayerController::FindUnitAtScreenPosition(const FVector2D& ScreenPosition)
{
	// Restore this contractor-owned implementation.
	return {};
}

ARTSStructure* ARTSPlayerController::FindStructureAtScreenPosition(const FVector2D& ScreenPosition)
{
	// Restore this contractor-owned implementation.
	return {};
}

ARTSWreckage* ARTSPlayerController::FindWreckageAtScreenPosition(const FVector2D& ScreenPosition)
{
	// Restore this contractor-owned implementation.
	return {};
}

void ARTSPlayerController::CreateInputConfiguration()
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::HandlePanInput(const FInputActionValue& Value)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::HandleZoomInput(const FInputActionValue& Value)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::HandleSelectionStarted(const FInputActionValue& Value)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::HandleSelectionCompleted(const FInputActionValue& Value)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::HandleContextCommandStarted(const FInputActionValue& Value)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::HandleDeploymentStarted(const FInputActionValue& Value)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::HandleExtractorPlacementStarted(const FInputActionValue& Value)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::HandleGeneratorPlacementStarted(const FInputActionValue& Value)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::HandleFactoryPlacementStarted(const FInputActionValue& Value)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::HandleSupplyDepotPlacementStarted(const FInputActionValue& Value)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::HandleTurretPlacementStarted(const FInputActionValue& Value)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::HandleInfantryProductionStarted(const FInputActionValue& Value)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::HandleLightVehicleProductionStarted(const FInputActionValue& Value)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::HandleHeavyVehicleProductionStarted(const FInputActionValue& Value)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::TryEnqueueFocusedFactory(const ERTSUnitType UnitType)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::BeginStructurePlacement(const ERTSStructureType StructureType)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::UpdateStructurePlacementPreview()
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::HandleCancelStarted(const FInputActionValue& Value)
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::UpdateWorldOverlays()
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::UpdateEdgePan()
{
	// Restore this contractor-owned implementation.
}

void ARTSPlayerController::UpdateSelectionMarquee()
{
	// Restore this contractor-owned implementation.
}

bool ARTSPlayerController::TryGetMousePosition(FVector2D& ScreenPosition) const
{
	// Restore this contractor-owned implementation.
	return {};
}

ARTSCameraPawn* ARTSPlayerController::GetRTSCameraPawn() const
{
	return Cast<ARTSCameraPawn>(GetPawn());
}

ARTSHUD* ARTSPlayerController::GetRTSHUD() const
{
	return Cast<ARTSHUD>(GetHUD());
}
