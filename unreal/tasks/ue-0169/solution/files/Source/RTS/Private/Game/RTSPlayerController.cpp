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
	Super::BeginPlay();
	CreateInputConfiguration();

	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			InputSubsystem->AddMappingContext(InputMappingContext, 0);
		}
		if (URTSSelectionSubsystem* Selection = LocalPlayer->GetSubsystem<URTSSelectionSubsystem>())
		{
			if (URTSPresentationSubsystem* Presentation = GetWorld() != nullptr
				? GetWorld()->GetSubsystem<URTSPresentationSubsystem>()
				: nullptr)
			{
				Presentation->ObserveSelection(*Selection);
			}
		}
	}

	FInputModeGameOnly InputMode;
	InputMode.SetConsumeCaptureMouseDown(false);
	SetInputMode(InputMode);
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer(); LocalPlayer != nullptr && LocalPlayer->ViewportClient != nullptr)
	{
		LocalPlayer->ViewportClient->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringMouseDown);
		LocalPlayer->ViewportClient->SetMouseLockMode(EMouseLockMode::DoNotLock);
	}
}

void ARTSPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	CreateInputConfiguration();

	UEnhancedInputComponent* EnhancedInput = CastChecked<UEnhancedInputComponent>(InputComponent);
	EnhancedInput->BindAction(
		CameraPanAction,
		ETriggerEvent::Triggered,
		this,
		&ARTSPlayerController::HandlePanInput);
	EnhancedInput->BindAction(
		CameraPanAction,
		ETriggerEvent::Completed,
		this,
		&ARTSPlayerController::HandlePanInput);
	EnhancedInput->BindAction(
		CameraZoomAction,
		ETriggerEvent::Triggered,
		this,
		&ARTSPlayerController::HandleZoomInput);
	EnhancedInput->BindAction(
		SelectAction,
		ETriggerEvent::Started,
		this,
		&ARTSPlayerController::HandleSelectionStarted);
	EnhancedInput->BindAction(
		SelectAction,
		ETriggerEvent::Completed,
		this,
		&ARTSPlayerController::HandleSelectionCompleted);
	EnhancedInput->BindAction(
		ContextCommandAction,
		ETriggerEvent::Started,
		this,
		&ARTSPlayerController::HandleContextCommandStarted);
	EnhancedInput->BindAction(
		DeploymentAction,
		ETriggerEvent::Started,
		this,
		&ARTSPlayerController::HandleDeploymentStarted);
	EnhancedInput->BindAction(ExtractorPlacementAction, ETriggerEvent::Started, this, &ARTSPlayerController::HandleExtractorPlacementStarted);
	EnhancedInput->BindAction(GeneratorPlacementAction, ETriggerEvent::Started, this, &ARTSPlayerController::HandleGeneratorPlacementStarted);
	EnhancedInput->BindAction(FactoryPlacementAction, ETriggerEvent::Started, this, &ARTSPlayerController::HandleFactoryPlacementStarted);
	EnhancedInput->BindAction(SupplyDepotPlacementAction, ETriggerEvent::Started, this, &ARTSPlayerController::HandleSupplyDepotPlacementStarted);
	EnhancedInput->BindAction(TurretPlacementAction, ETriggerEvent::Started, this, &ARTSPlayerController::HandleTurretPlacementStarted);
	EnhancedInput->BindAction(InfantryProductionAction, ETriggerEvent::Started, this, &ARTSPlayerController::HandleInfantryProductionStarted);
	EnhancedInput->BindAction(LightVehicleProductionAction, ETriggerEvent::Started, this, &ARTSPlayerController::HandleLightVehicleProductionStarted);
	EnhancedInput->BindAction(HeavyVehicleProductionAction, ETriggerEvent::Started, this, &ARTSPlayerController::HandleHeavyVehicleProductionStarted);
	EnhancedInput->BindAction(
		CancelAction,
		ETriggerEvent::Started,
		this,
		&ARTSPlayerController::HandleCancelStarted);
}

void ARTSPlayerController::PlayerTick(const float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	UpdateEdgePan();
	UpdateSelectionMarquee();
	UpdateStructurePlacementPreview();
	UpdateWorldOverlays();
}

void ARTSPlayerController::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	TeamId = NewTeamId;
}

FGenericTeamId ARTSPlayerController::GetGenericTeamId() const
{
	return TeamId;
}

void ARTSPlayerController::SelectAtScreenPosition(
	const FVector2D& ScreenPosition,
	const bool bToggleSelection)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	URTSSelectionSubsystem* Selection = LocalPlayer != nullptr
		? LocalPlayer->GetSubsystem<URTSSelectionSubsystem>()
		: nullptr;
	if (Selection == nullptr)
	{
		return;
	}

	ARTSCombatUnit* HitUnit = FindUnitAtScreenPosition(ScreenPosition);
	if (HitUnit == nullptr)
	{
		if (ARTSStructure* HitStructure = FindStructureAtScreenPosition(ScreenPosition))
		{
			if (bToggleSelection)
			{
				Selection->ToggleStructure(HitStructure);
			}
			else
			{
				Selection->ReplaceWithStructure(HitStructure);
			}
			return;
		}
	}
	ARTSCombatUnit* Candidates[] = {HitUnit};
	if (bToggleSelection)
	{
		Selection->Toggle(HitUnit != nullptr
			? TConstArrayView<ARTSCombatUnit*>(Candidates, 1)
			: TConstArrayView<ARTSCombatUnit*>());
	}
	else
	{
		Selection->ReplaceWith(HitUnit != nullptr
			? TConstArrayView<ARTSCombatUnit*>(Candidates, 1)
			: TConstArrayView<ARTSCombatUnit*>());
	}
}

void ARTSPlayerController::SelectInsideScreenRect(
	const FBox2D& ScreenRect,
	const bool bToggleSelection)
{
	const FBox2D NormalizedScreenRect = NormalizeScreenRect(ScreenRect);
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	URTSSelectionSubsystem* Selection = LocalPlayer != nullptr
		? LocalPlayer->GetSubsystem<URTSSelectionSubsystem>()
		: nullptr;
	UWorld* World = GetWorld();
	if (Selection == nullptr || World == nullptr)
	{
		return;
	}

	TArray<ARTSCombatUnit*> Candidates;
	for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
	{
		ARTSCombatUnit* Unit = *UnitIterator;
		if (!Unit->IsAlive())
		{
			continue;
		}

		const FBox UnitBounds = Unit->GetComponentsBoundingBox(true);
		const FVector BoundsCorners[] =
		{
			FVector(UnitBounds.Min.X, UnitBounds.Min.Y, UnitBounds.Min.Z),
			FVector(UnitBounds.Min.X, UnitBounds.Min.Y, UnitBounds.Max.Z),
			FVector(UnitBounds.Min.X, UnitBounds.Max.Y, UnitBounds.Min.Z),
			FVector(UnitBounds.Min.X, UnitBounds.Max.Y, UnitBounds.Max.Z),
			FVector(UnitBounds.Max.X, UnitBounds.Min.Y, UnitBounds.Min.Z),
			FVector(UnitBounds.Max.X, UnitBounds.Min.Y, UnitBounds.Max.Z),
			FVector(UnitBounds.Max.X, UnitBounds.Max.Y, UnitBounds.Min.Z),
			FVector(UnitBounds.Max.X, UnitBounds.Max.Y, UnitBounds.Max.Z)
		};
		FBox2D ProjectedBounds(ForceInit);
		for (const FVector& Corner : BoundsCorners)
		{
			FVector2D ProjectedCorner;
			if (ProjectWorldLocationToScreen(Corner, ProjectedCorner))
			{
				ProjectedBounds += ProjectedCorner;
			}
		}

		if (ProjectedBounds.bIsValid && ProjectedBounds.Intersect(NormalizedScreenRect))
		{
			Candidates.Add(Unit);
		}
	}

	if (bToggleSelection)
	{
		Selection->Toggle(Candidates);
	}
	else
	{
		Selection->ReplaceWith(Candidates);
	}
}

void ARTSPlayerController::IssueContextCommandAtScreenPosition(const FVector2D& ScreenPosition)
{
	UWorld* World = GetWorld();
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	URTSSelectionSubsystem* Selection = LocalPlayer != nullptr
		? LocalPlayer->GetSubsystem<URTSSelectionSubsystem>()
		: nullptr;
	URTSCommandSubsystem* Commands = World != nullptr ? World->GetSubsystem<URTSCommandSubsystem>() : nullptr;
	if (Selection == nullptr || Commands == nullptr || Selection->GetLivingUnits().IsEmpty())
	{
		return;
	}
	if (URTSDeploymentViewSubsystem* DeploymentView = LocalPlayer->GetSubsystem<URTSDeploymentViewSubsystem>())
	{
		DeploymentView->CancelPreview();
	}
	// Wreckage blocks Visibility but intentionally ignores the unit-selection channel. Resolve it
	// first so that channel cannot see through the wreck and turn the same click into a unit command.
	if (ARTSWreckage* Wreckage = FindWreckageAtScreenPosition(ScreenPosition))
	{
		Commands->IssueReclaim(Selection->GetLivingUnits(), *Wreckage);
		return;
	}
	if (ARTSStructure* Target = FindStructureAtScreenPosition(ScreenPosition))
	{
		Commands->IssueAttack(Selection->GetLivingUnits(), *Target);
		return;
	}
	if (ARTSCombatUnit* Target = FindUnitAtScreenPosition(ScreenPosition))
	{
		Commands->IssueAttack(Selection->GetLivingUnits(), *Target);
		return;
	}

	FHitResult GroundHit;
	if (GetHitResultAtScreenPosition(ScreenPosition, ECC_Visibility, false, GroundHit)
		&& GroundHit.GetActor() != nullptr)
	{
		Commands->IssueMove(Selection->GetLivingUnits(), GroundHit.ImpactPoint);
	}
}

ARTSCombatUnit* ARTSPlayerController::FindUnitAtScreenPosition(const FVector2D& ScreenPosition)
{
	FHitResult UnitHit;
	if (GetHitResultAtScreenPosition(ScreenPosition, ECC_GameTraceChannel1, false, UnitHit))
	{
		if (ARTSCombatUnit* HitUnit = Cast<ARTSCombatUnit>(UnitHit.GetActor()))
		{
			return HitUnit;
		}
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	ARTSCombatUnit* ClosestUnit = nullptr;
	float ClosestDistanceSquared = FMath::Square(UnitStatusTargetRadius);
	for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
	{
		ARTSCombatUnit* Candidate = *UnitIterator;
		if (!Candidate->IsAlive())
		{
			continue;
		}

		FVector2D StatusScreenPosition;
		if (!ProjectWorldLocationToScreen(Candidate->GetStatusAnchorWorldLocation(), StatusScreenPosition))
		{
			continue;
		}

		const float DistanceSquared = FVector2D::DistSquared(ScreenPosition, StatusScreenPosition);
		if (DistanceSquared < ClosestDistanceSquared
			|| (FMath::IsNearlyEqual(DistanceSquared, ClosestDistanceSquared)
				&& ClosestUnit != nullptr
				&& Candidate->GetStableUnitId() < ClosestUnit->GetStableUnitId()))
		{
			ClosestUnit = Candidate;
			ClosestDistanceSquared = DistanceSquared;
		}
	}
	return ClosestUnit;
}

ARTSStructure* ARTSPlayerController::FindStructureAtScreenPosition(const FVector2D& ScreenPosition)
{
	FHitResult StructureHit;
	if (GetHitResultAtScreenPosition(ScreenPosition, ECC_Visibility, false, StructureHit))
	{
		ARTSStructure* Structure = Cast<ARTSStructure>(StructureHit.GetActor());
		return IsValid(Structure) && Structure->IsAlive() ? Structure : nullptr;
	}
	return nullptr;
}

ARTSWreckage* ARTSPlayerController::FindWreckageAtScreenPosition(const FVector2D& ScreenPosition)
{
	FHitResult WreckageHit;
	if (GetHitResultAtScreenPosition(ScreenPosition, ECC_Visibility, false, WreckageHit))
	{
		ARTSWreckage* Wreckage = Cast<ARTSWreckage>(WreckageHit.GetActor());
		return IsValid(Wreckage) && Wreckage->IsAvailable() ? Wreckage : nullptr;
	}
	return nullptr;
}

void ARTSPlayerController::CreateInputConfiguration()
{
	if (InputMappingContext != nullptr)
	{
		return;
	}

	InputMappingContext = NewObject<UInputMappingContext>(this, TEXT("RTSInputMapping"));
	CameraPanAction = NewObject<UInputAction>(this, TEXT("RTSCameraPan"));
	CameraPanAction->ValueType = EInputActionValueType::Axis2D;
	CameraZoomAction = NewObject<UInputAction>(this, TEXT("RTSCameraZoom"));
	CameraZoomAction->ValueType = EInputActionValueType::Axis1D;
	SelectAction = NewObject<UInputAction>(this, TEXT("RTSSelect"));
	SelectAction->ValueType = EInputActionValueType::Boolean;
	ContextCommandAction = NewObject<UInputAction>(this, TEXT("RTSContextCommand"));
	ContextCommandAction->ValueType = EInputActionValueType::Boolean;
	DeploymentAction = NewObject<UInputAction>(this, TEXT("RTSDeployHeadquarters"));
	DeploymentAction->ValueType = EInputActionValueType::Boolean;
	ExtractorPlacementAction = NewObject<UInputAction>(this, TEXT("RTSPlaceExtractor"));
	ExtractorPlacementAction->ValueType = EInputActionValueType::Boolean;
	GeneratorPlacementAction = NewObject<UInputAction>(this, TEXT("RTSPlaceGenerator"));
	GeneratorPlacementAction->ValueType = EInputActionValueType::Boolean;
	FactoryPlacementAction = NewObject<UInputAction>(this, TEXT("RTSPlaceFactory"));
	FactoryPlacementAction->ValueType = EInputActionValueType::Boolean;
	SupplyDepotPlacementAction = NewObject<UInputAction>(this, TEXT("RTSPlaceSupplyDepot"));
	SupplyDepotPlacementAction->ValueType = EInputActionValueType::Boolean;
	TurretPlacementAction = NewObject<UInputAction>(this, TEXT("RTSPlaceTurret"));
	TurretPlacementAction->ValueType = EInputActionValueType::Boolean;
	InfantryProductionAction = NewObject<UInputAction>(this, TEXT("RTSProduceInfantry"));
	InfantryProductionAction->ValueType = EInputActionValueType::Boolean;
	LightVehicleProductionAction = NewObject<UInputAction>(this, TEXT("RTSProduceLightVehicle"));
	LightVehicleProductionAction->ValueType = EInputActionValueType::Boolean;
	HeavyVehicleProductionAction = NewObject<UInputAction>(this, TEXT("RTSProduceHeavyVehicle"));
	HeavyVehicleProductionAction->ValueType = EInputActionValueType::Boolean;
	CancelAction = NewObject<UInputAction>(this, TEXT("RTSCancel"));
	CancelAction->ValueType = EInputActionValueType::Boolean;

	AddPanKeyMapping(*InputMappingContext, *CameraPanAction, EKeys::W, true, false);
	AddPanKeyMapping(*InputMappingContext, *CameraPanAction, EKeys::Up, true, false);
	AddPanKeyMapping(*InputMappingContext, *CameraPanAction, EKeys::S, true, true);
	AddPanKeyMapping(*InputMappingContext, *CameraPanAction, EKeys::Down, true, true);
	AddPanKeyMapping(*InputMappingContext, *CameraPanAction, EKeys::D, false, false);
	AddPanKeyMapping(*InputMappingContext, *CameraPanAction, EKeys::Right, false, false);
	AddPanKeyMapping(*InputMappingContext, *CameraPanAction, EKeys::A, false, true);
	AddPanKeyMapping(*InputMappingContext, *CameraPanAction, EKeys::Left, false, true);
	InputMappingContext->MapKey(CameraZoomAction, EKeys::MouseWheelAxis);
	InputMappingContext->MapKey(SelectAction, EKeys::LeftMouseButton);
	InputMappingContext->MapKey(ContextCommandAction, EKeys::RightMouseButton);
	InputMappingContext->MapKey(DeploymentAction, EKeys::E);
	InputMappingContext->MapKey(ExtractorPlacementAction, EKeys::One);
	InputMappingContext->MapKey(GeneratorPlacementAction, EKeys::Two);
	InputMappingContext->MapKey(FactoryPlacementAction, EKeys::Three);
	InputMappingContext->MapKey(SupplyDepotPlacementAction, EKeys::Four);
	InputMappingContext->MapKey(TurretPlacementAction, EKeys::Five);
	InputMappingContext->MapKey(InfantryProductionAction, EKeys::Z);
	InputMappingContext->MapKey(LightVehicleProductionAction, EKeys::X);
	InputMappingContext->MapKey(HeavyVehicleProductionAction, EKeys::C);
	InputMappingContext->MapKey(CancelAction, EKeys::Escape);
}

void ARTSPlayerController::HandlePanInput(const FInputActionValue& Value)
{
	if (ARTSCameraPawn* CameraPawn = GetRTSCameraPawn())
	{
		CameraPawn->SetKeyboardPanInput(Value.Get<FVector2D>());
	}
}

void ARTSPlayerController::HandleZoomInput(const FInputActionValue& Value)
{
	if (ARTSCameraPawn* CameraPawn = GetRTSCameraPawn())
	{
		CameraPawn->AddZoomInput(Value.Get<float>());
	}
}

void ARTSPlayerController::HandleSelectionStarted(const FInputActionValue& Value)
{
	bSelectionHeld = TryGetMousePosition(SelectionStart);
}

void ARTSPlayerController::HandleSelectionCompleted(const FInputActionValue& Value)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	URTSDeploymentViewSubsystem* DeploymentView = LocalPlayer != nullptr
		? LocalPlayer->GetSubsystem<URTSDeploymentViewSubsystem>()
		: nullptr;
	if (DeploymentView != nullptr && DeploymentView->IsPreviewActive())
	{
		bSelectionHeld = false;
		if (ARTSHUD* HUD = GetRTSHUD())
		{
			HUD->SetSelectionMarquee({});
		}
		ARTSCombatUnit* CommandVehicle = DeploymentView->GetCommandVehicle();
		URTSStructureSubsystem* Structures = GetWorld() != nullptr
			? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
			: nullptr;
		if (IsValid(CommandVehicle) && Structures != nullptr)
		{
			DeploymentView->SetLastResult(Structures->TryDeployHeadquarters(*CommandVehicle));
		}
		else
		{
			DeploymentView->CancelPreview();
		}
		return;
	}
	if (DeploymentView != nullptr && DeploymentView->IsStructurePreviewActive())
	{
		bSelectionHeld = false;
		if (ARTSHUD* HUD = GetRTSHUD())
		{
			HUD->SetSelectionMarquee({});
		}
		URTSStructureSubsystem* Structures = GetWorld() != nullptr
			? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
			: nullptr;
		const TOptional<FRTSPlacementRequest> Request = DeploymentView->GetPlacementRequest();
		if (Structures != nullptr && Request.IsSet())
		{
			DeploymentView->SetLastPlacementResult(Structures->TryPlace(Request.GetValue()));
		}
		else
		{
			DeploymentView->CancelPreview();
		}
		return;
	}

	FVector2D SelectionEnd;
	if (!bSelectionHeld || !TryGetMousePosition(SelectionEnd))
	{
		bSelectionHeld = false;
		if (ARTSHUD* HUD = GetRTSHUD())
		{
			HUD->SetSelectionMarquee({});
		}
		return;
	}

	bSelectionHeld = false;
	if (ARTSHUD* HUD = GetRTSHUD())
	{
		HUD->SetSelectionMarquee({});
	}

	const bool bToggleSelection = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
	if (FVector2D::Distance(SelectionStart, SelectionEnd) < SelectionDragThreshold)
	{
		SelectAtScreenPosition(SelectionEnd, bToggleSelection);
	}
	else
	{
		SelectInsideScreenRect(NormalizeScreenRect(FBox2D(SelectionStart, SelectionEnd)), bToggleSelection);
	}
}

void ARTSPlayerController::HandleContextCommandStarted(const FInputActionValue& Value)
{
	FVector2D ScreenPosition;
	if (TryGetMousePosition(ScreenPosition))
	{
		IssueContextCommandAtScreenPosition(ScreenPosition);
	}
}

void ARTSPlayerController::HandleDeploymentStarted(const FInputActionValue& Value)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	URTSSelectionSubsystem* Selection = LocalPlayer != nullptr
		? LocalPlayer->GetSubsystem<URTSSelectionSubsystem>()
		: nullptr;
	URTSDeploymentViewSubsystem* DeploymentView = LocalPlayer != nullptr
		? LocalPlayer->GetSubsystem<URTSDeploymentViewSubsystem>()
		: nullptr;
	if (Selection == nullptr || DeploymentView == nullptr)
	{
		return;
	}

	for (ARTSCombatUnit* Unit : Selection->GetLivingUnits())
	{
		if (IsValid(Unit)
			&& Unit->IsMilestone2Unit()
			&& Unit->GetUnitType() == ERTSUnitType::CommandVehicle)
		{
			DeploymentView->BeginHeadquartersPreview(*Unit);
			return;
		}
	}
}

void ARTSPlayerController::HandleExtractorPlacementStarted(const FInputActionValue& Value)
{
	BeginStructurePlacement(ERTSStructureType::MaterialExtractor);
}

void ARTSPlayerController::HandleGeneratorPlacementStarted(const FInputActionValue& Value)
{
	BeginStructurePlacement(ERTSStructureType::PowerGenerator);
}

void ARTSPlayerController::HandleFactoryPlacementStarted(const FInputActionValue& Value)
{
	BeginStructurePlacement(ERTSStructureType::Factory);
}

void ARTSPlayerController::HandleSupplyDepotPlacementStarted(const FInputActionValue& Value)
{
	BeginStructurePlacement(ERTSStructureType::SupplyDepot);
}

void ARTSPlayerController::HandleTurretPlacementStarted(const FInputActionValue& Value)
{
	BeginStructurePlacement(ERTSStructureType::DefensiveTurret);
}

void ARTSPlayerController::HandleInfantryProductionStarted(const FInputActionValue& Value)
{
	TryEnqueueFocusedFactory(ERTSUnitType::InfantrySquad);
}

void ARTSPlayerController::HandleLightVehicleProductionStarted(const FInputActionValue& Value)
{
	TryEnqueueFocusedFactory(ERTSUnitType::LightVehicle);
}

void ARTSPlayerController::HandleHeavyVehicleProductionStarted(const FInputActionValue& Value)
{
	TryEnqueueFocusedFactory(ERTSUnitType::HeavyVehicle);
}

void ARTSPlayerController::TryEnqueueFocusedFactory(const ERTSUnitType UnitType)
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const URTSSelectionSubsystem* Selection = LocalPlayer != nullptr
		? LocalPlayer->GetSubsystem<URTSSelectionSubsystem>()
		: nullptr;
	ARTSStructure* Structure = Selection != nullptr ? Selection->GetFocusedStructure() : nullptr;
	URTSProductionSubsystem* Production = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSProductionSubsystem>()
		: nullptr;
	if (Structure != nullptr && Production != nullptr)
	{
		Production->TryEnqueue(TeamId, Structure->GetStableStructureId(), UnitType);
	}
}

void ARTSPlayerController::BeginStructurePlacement(const ERTSStructureType StructureType)
{
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (URTSDeploymentViewSubsystem* View = LocalPlayer->GetSubsystem<URTSDeploymentViewSubsystem>())
		{
			View->BeginStructurePreview(StructureType, TeamId);
			UpdateStructurePlacementPreview();
		}
	}
}

void ARTSPlayerController::UpdateStructurePlacementPreview()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	URTSDeploymentViewSubsystem* View = LocalPlayer != nullptr
		? LocalPlayer->GetSubsystem<URTSDeploymentViewSubsystem>()
		: nullptr;
	FVector2D ScreenPosition;
	if (View == nullptr || !View->IsStructurePreviewActive() || !TryGetMousePosition(ScreenPosition))
	{
		return;
	}

	FHitResult Hit;
	if (!GetHitResultAtScreenPosition(ScreenPosition, ECC_Visibility, false, Hit))
	{
		return;
	}
	const ARTSMaterialDeposit* Deposit = Cast<ARTSMaterialDeposit>(Hit.GetActor());
	View->UpdateStructurePreviewLocation(Deposit != nullptr
		? Deposit->GetActorLocation()
		: Hit.ImpactPoint);
}

void ARTSPlayerController::HandleCancelStarted(const FInputActionValue& Value)
{
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (URTSDeploymentViewSubsystem* DeploymentView =
			LocalPlayer->GetSubsystem<URTSDeploymentViewSubsystem>())
		{
			DeploymentView->CancelPreview();
			UpdateWorldOverlays();
		}
	}
}

void ARTSPlayerController::UpdateWorldOverlays()
{
	UWorld* World = GetWorld();
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	URTSDeploymentViewSubsystem* View = LocalPlayer != nullptr
		? LocalPlayer->GetSubsystem<URTSDeploymentViewSubsystem>()
		: nullptr;
	URTSStructureSubsystem* Structures = World != nullptr
		? World->GetSubsystem<URTSStructureSubsystem>()
		: nullptr;
	URTSWorldOverlaySubsystem* Overlays = World != nullptr
		? World->GetSubsystem<URTSWorldOverlaySubsystem>()
		: nullptr;
	if (View == nullptr || Structures == nullptr || Overlays == nullptr)
	{
		return;
	}

	constexpr int32 PreviewSourceId = 1;
	if (ARTSCombatUnit* CommandVehicle = View->GetCommandVehicle())
	{
		const FRTSHeadquartersDeploymentPreview Preview =
			Structures->EvaluateHeadquartersDeployment(*CommandVehicle);
		const float CellSize = FRTSMilestone2Configuration::Load().World.PlacementCellSize;
		FRTSWorldOverlayDescriptor Footprint;
		Footprint.Mode = ERTSWorldOverlayMode::PlacementFootprint;
		Footprint.SourceKind = ERTSWorldOverlaySourceKind::DeploymentPreview;
		Footprint.StableSourceId = PreviewSourceId;
		Footprint.WorldTransform.SetLocation(Preview.GroundLocation);
		Footprint.HalfExtent = FVector2D(Preview.FootprintCells.X, Preview.FootprintCells.Y)
			* CellSize * 0.5f;
		Footprint.Validity = Preview.bValid
			? ERTSWorldOverlayValidity::Valid
			: ERTSWorldOverlayValidity::Invalid;
		Footprint.TeamId = CommandVehicle->GetGenericTeamId();
		Overlays->Submit(Footprint);

		FRTSWorldOverlayDescriptor BuildArea = Footprint;
		BuildArea.Mode = ERTSWorldOverlayMode::BuildArea;
		BuildArea.Radius = Preview.BuildAreaRadius;
		Overlays->Submit(BuildArea);
	}
	else
	{
		Overlays->RemoveSource(ERTSWorldOverlaySourceKind::DeploymentPreview, PreviewSourceId);
	}

	const TOptional<FRTSPlacementRequest> Request = View->GetPlacementRequest();
	if (Request.IsSet())
	{
		const FRTSPlacementPreview Preview = Structures->EvaluatePlacement(Request.GetValue());
		const float CellSize = FRTSMilestone2Configuration::Load().World.PlacementCellSize;
		FRTSWorldOverlayDescriptor Footprint;
		Footprint.Mode = ERTSWorldOverlayMode::PlacementFootprint;
		Footprint.SourceKind = ERTSWorldOverlaySourceKind::PlacementPreview;
		Footprint.StableSourceId = PreviewSourceId;
		Footprint.WorldTransform.SetLocation(Preview.SnappedGroundLocation);
		Footprint.HalfExtent = FVector2D(Preview.FootprintCells.X, Preview.FootprintCells.Y)
			* CellSize * 0.5f;
		Footprint.Validity = Preview.bValid
			? ERTSWorldOverlayValidity::Valid
			: ERTSWorldOverlayValidity::Invalid;
		Footprint.TeamId = Request->TeamId;
		Overlays->Submit(Footprint);
	}
	else
	{
		Overlays->RemoveSource(ERTSWorldOverlaySourceKind::PlacementPreview, PreviewSourceId);
	}
}

void ARTSPlayerController::UpdateEdgePan()
{
	ARTSCameraPawn* CameraPawn = GetRTSCameraPawn();
	FVector2D MousePosition;
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	GetViewportSize(ViewportWidth, ViewportHeight);
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const FViewport* Viewport = LocalPlayer != nullptr && LocalPlayer->ViewportClient != nullptr
		? LocalPlayer->ViewportClient->Viewport
		: nullptr;
	if (CameraPawn == nullptr
		|| Viewport == nullptr
		|| !Viewport->HasFocus()
		|| !TryGetMousePosition(MousePosition)
		|| ViewportWidth <= 0
		|| ViewportHeight <= 0)
	{
		if (CameraPawn != nullptr)
		{
			CameraPawn->SetEdgePanInput(FVector2D::ZeroVector);
		}
		return;
	}

	FVector2D EdgeInput = FVector2D::ZeroVector;
	if (MousePosition.X <= EdgePanThreshold)
	{
		EdgeInput.X = -1.0f;
	}
	else if (MousePosition.X >= ViewportWidth - EdgePanThreshold)
	{
		EdgeInput.X = 1.0f;
	}
	if (MousePosition.Y <= EdgePanThreshold)
	{
		EdgeInput.Y = 1.0f;
	}
	else if (MousePosition.Y >= ViewportHeight - EdgePanThreshold)
	{
		EdgeInput.Y = -1.0f;
	}
	CameraPawn->SetEdgePanInput(EdgeInput);
}

void ARTSPlayerController::UpdateSelectionMarquee()
{
	ARTSHUD* HUD = GetRTSHUD();
	if (HUD == nullptr)
	{
		return;
	}

	FVector2D CurrentMousePosition;
	if (!bSelectionHeld || !TryGetMousePosition(CurrentMousePosition)
		|| FVector2D::Distance(SelectionStart, CurrentMousePosition) < SelectionDragThreshold)
	{
		HUD->SetSelectionMarquee({});
		return;
	}
	HUD->SetSelectionMarquee(NormalizeScreenRect(FBox2D(SelectionStart, CurrentMousePosition)));
}

bool ARTSPlayerController::TryGetMousePosition(FVector2D& ScreenPosition) const
{
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	ScreenPosition = FVector2D(MouseX, MouseY);
	return true;
}

ARTSCameraPawn* ARTSPlayerController::GetRTSCameraPawn() const
{
	return Cast<ARTSCameraPawn>(GetPawn());
}

ARTSHUD* ARTSPlayerController::GetRTSHUD() const
{
	return Cast<ARTSHUD>(GetHUD());
}
