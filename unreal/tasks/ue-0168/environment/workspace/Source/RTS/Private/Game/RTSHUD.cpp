// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/RTSHUD.h"

#include "Combat/RTSCombatTypes.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSTurretCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/UserInterfaceSettings.h"
#include "EngineUtils.h"
#include "Game/RTSPlayerController.h"
#include "Orders/RTSCommandSubsystem.h"
#include "Orders/RTSOrderTypes.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Production/RTSProductionComponent.h"
#include "Reclaim/RTSReclaimComponent.h"
#include "Reclaim/RTSWreckage.h"
#include "Selection/RTSSelectionSubsystem.h"
#include "Structures/RTSDeploymentViewSubsystem.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "UI/RTSPresentationText.h"
#include "UI/SRTSStatusOverlay.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"
#include "World/RTSMaterialDeposit.h"
#include "World/RTSStartingZone.h"

void ARTSHUD::BeginPlay()
{
	Super::BeginPlay();

	ARTSPlayerController* Controller = Cast<ARTSPlayerController>(GetOwningPlayerController());
	ULocalPlayer* LocalPlayer = Controller != nullptr ? Controller->GetLocalPlayer() : nullptr;
	if (Controller == nullptr || LocalPlayer == nullptr || LocalPlayer->ViewportClient == nullptr)
	{
		return;
	}

	// Player-layer Slate receives Unreal's viewport DPI transform and safe-area bounds. Keeping
	// spatial marks on Canvas avoids inventing a second projection path for world feedback.
	const TWeakObjectPtr<ARTSPlayerController> WeakController = Controller;
	StatusOverlayWidget = SNew(SRTSStatusOverlay)
		.Snapshot_Lambda([WeakController]()
		{
			return FRTSHUDSnapshotAdapter::Capture(WeakController.Get());
		});
	OverlayLocalPlayer = LocalPlayer;
	LocalPlayer->ViewportClient->AddViewportWidgetForPlayer(LocalPlayer, StatusOverlayWidget.ToSharedRef(), 10);
}

void ARTSHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (StatusOverlayWidget.IsValid())
	{
		if (ULocalPlayer* LocalPlayer = OverlayLocalPlayer.Get();
			LocalPlayer != nullptr && LocalPlayer->ViewportClient != nullptr)
		{
			LocalPlayer->ViewportClient->RemoveViewportWidgetForPlayer(LocalPlayer, StatusOverlayWidget.ToSharedRef());
		}
		StatusOverlayWidget.Reset();
		OverlayLocalPlayer.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void ARTSHUD::DrawHUD()
{
	Super::DrawHUD();
	DrawMarquee();
	DrawMoveDestination();
	DrawDeploymentPreview();
	DrawConstructionStatus();
	DrawProductionStatus();
	DrawSelectedStructures();
	DrawTurretFeedback();
	DrawMaterialDepositFeedback();
	DrawWreckageFeedback();

	const ARTSPlayerController* RTSController = Cast<ARTSPlayerController>(GetOwningPlayerController());
	const ULocalPlayer* LocalPlayer = RTSController != nullptr ? RTSController->GetLocalPlayer() : nullptr;
	const URTSSelectionSubsystem* Selection = LocalPlayer != nullptr
		? LocalPlayer->GetSubsystem<URTSSelectionSubsystem>()
		: nullptr;
	if (Selection == nullptr)
	{
		return;
	}
	const TConstArrayView<ARTSCombatUnit*> SelectedUnits = Selection->GetLivingUnits();
	if (const UWorld* World = GetWorld())
	{
		TSet<const ARTSStructure*> StructureAttackTargets;
		for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
		{
			if (UnitIterator->IsAlive())
			{
				DrawUnitAffiliation(**UnitIterator, SelectedUnits.Contains(*UnitIterator));
				DrawAttackFeedback(**UnitIterator);
				const FRTSOrderSnapshot Order = UnitIterator->GetOrderComponent()->GetSnapshot();
				if (Order.Kind == ERTSOrderKind::Attack
					&& IsValid(Order.TargetStructure)
					&& Order.TargetStructure->IsAlive())
				{
					StructureAttackTargets.Add(Order.TargetStructure.Get());
				}
			}
		}

		const TConstArrayView<ARTSStructure*> SelectedStructures = Selection->GetLivingStructures();
		for (TActorIterator<ARTSStructure> StructureIterator(World); StructureIterator; ++StructureIterator)
		{
			if (StructureIterator->IsAlive())
			{
				DrawStructureAffiliation(
					**StructureIterator,
					SelectedStructures.Contains(*StructureIterator),
					StructureAttackTargets.Contains(*StructureIterator));
			}
		}
	}

	DrawAttackTargets(SelectedUnits);

	for (const ARTSCombatUnit* SelectedUnit : SelectedUnits)
	{
		if (IsValid(SelectedUnit))
		{
			DrawSelectedUnit(*SelectedUnit);
		}
	}
}

void ARTSHUD::DrawTurretFeedback()
{
	if (GetWorld() == nullptr)
	{
		return;
	}
	for (TActorIterator<ARTSStructure> Iterator(GetWorld()); Iterator; ++Iterator)
	{
		const URTSTurretCombatComponent* Turret = Iterator->GetTurretCombatComponent();
		if (Turret == nullptr)
		{
			continue;
		}
		const FRTSTurretCombatSnapshot Snapshot = Turret->GetSnapshot();
		const ARTSCombatUnit* Target = Snapshot.Target.Get();
		if (Snapshot.State != ERTSTurretState::Firing || !IsValid(Target))
		{
			continue;
		}
		const FVector Start = Project(Iterator->GetActorLocation() + FVector(0.0f, 0.0f, 560.0f));
		const FVector End = Project(Target->GetStatusAnchorWorldLocation());
		if (Start.Z > 0.0f && End.Z > 0.0f)
		{
			DrawLine(Start.X, Start.Y, End.X, End.Y, FLinearColor(1.0f, 0.25f, 0.08f), 3.0f * GetViewportUIScale());
		}
	}
}

void ARTSHUD::DrawMaterialDepositFeedback()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	TSet<int32> ClaimedDepositIds;
	for (TActorIterator<ARTSStructure> StructureIterator(World); StructureIterator; ++StructureIterator)
	{
		if (StructureIterator->IsAlive() && StructureIterator->GetStableDepositId() != INDEX_NONE)
		{
			ClaimedDepositIds.Add(StructureIterator->GetStableDepositId());
		}
	}

	const float UIScale = GetViewportUIScale();
	for (TActorIterator<ARTSMaterialDeposit> DepositIterator(World); DepositIterator; ++DepositIterator)
	{
		const int32 DepositId = DepositIterator->GetStableDepositId();
		if (ClaimedDepositIds.Contains(DepositId))
		{
			continue;
		}

		const FBox Bounds = DepositIterator->GetComponentsBoundingBox(true);
		const FVector Screen = Project(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Max.Z + 40.0f));
		if (Screen.Z <= 0.0f)
		{
			continue;
		}
		DrawText(
			FString::Printf(TEXT("DEPOSIT D%d     BUILD EXTRACTOR [1]"), DepositId),
			FLinearColor(0.78f, 0.48f, 0.18f),
			Screen.X - 75.0f * UIScale,
			Screen.Y - 12.0f * UIScale,
			nullptr,
			0.72f * UIScale);
	}
}

void ARTSHUD::DrawWreckageFeedback()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const float UIScale = GetViewportUIScale();
	for (TActorIterator<ARTSWreckage> Iterator(World); Iterator; ++Iterator)
	{
		const FRTSWreckageSnapshot Snapshot = Iterator->GetSnapshot();
		if (!Iterator->IsAvailable())
		{
			continue;
		}
		const FVector Screen = Project(Iterator->GetStatusAnchorWorldLocation());
		if (Screen.Z <= 0.0f)
		{
			continue;
		}
		const bool bCommandWreck = Snapshot.ReclaimMaterialValue == 0
			&& ((Snapshot.SourceKind == ERTSWreckageSourceKind::Unit
					&& Snapshot.SourceUnitType == ERTSUnitType::CommandVehicle)
				|| (Snapshot.SourceKind == ERTSWreckageSourceKind::Structure
					&& Snapshot.SourceStructureType == ERTSStructureType::Headquarters));
		const FString FeedbackText = bCommandWreck
			? FString::Printf(TEXT("COMMAND WRECK W%d     MATCH OVER"), Snapshot.StableWreckageId)
			: FString::Printf(
				TEXT("WRECK W%d     +%d MATERIALS     INFANTRY RIGHT CLICK"),
				Snapshot.StableWreckageId,
				Snapshot.ReclaimMaterialValue);
		DrawText(
			FeedbackText,
			FLinearColor(0.95f, 0.68f, 0.24f),
			Screen.X - 54.0f * UIScale,
			Screen.Y - 12.0f * UIScale,
			nullptr,
			0.78f * UIScale);

		ARTSCombatUnit* Reclaimer = nullptr;
		for (TActorIterator<ARTSCombatUnit> UnitIterator(World); UnitIterator; ++UnitIterator)
		{
			if (UnitIterator->GetStableUnitId() == Snapshot.LeaseHolderStableUnitId
				&& UnitIterator->IsAlive())
			{
				Reclaimer = *UnitIterator;
				break;
			}
		}
		const URTSReclaimComponent* Reclaim = Reclaimer != nullptr ? Reclaimer->GetReclaimComponent() : nullptr;
		if (Reclaim == nullptr)
		{
			continue;
		}
		const float Progress = Reclaim->GetSnapshot().Progress;
		const float BarWidth = 90.0f * UIScale;
		const float BarHeight = 9.0f * UIScale;
		const float Border = 2.0f * UIScale;
		DrawRect(FLinearColor::Black, Screen.X - BarWidth * 0.5f, Screen.Y + 8.0f * UIScale, BarWidth, BarHeight);
		DrawRect(
			FLinearColor(0.95f, 0.55f, 0.12f),
			Screen.X - BarWidth * 0.5f + Border,
			Screen.Y + 8.0f * UIScale + Border,
			(BarWidth - Border * 2.0f) * Progress,
			FMath::Max(1.0f, BarHeight - Border * 2.0f));
		const FVector ReclaimerScreen = Project(Reclaimer->GetStatusAnchorWorldLocation());
		if (ReclaimerScreen.Z > 0.0f)
		{
			DrawLine(
				ReclaimerScreen.X,
				ReclaimerScreen.Y,
				Screen.X,
				Screen.Y,
				FLinearColor(1.0f, 0.55f, 0.12f),
				2.0f * UIScale);
		}
	}
}

void ARTSHUD::DrawSelectedStructures()
{
	const ARTSPlayerController* Controller = Cast<ARTSPlayerController>(GetOwningPlayerController());
	const ULocalPlayer* LocalPlayer = Controller != nullptr ? Controller->GetLocalPlayer() : nullptr;
	const URTSSelectionSubsystem* Selection = LocalPlayer != nullptr
		? LocalPlayer->GetSubsystem<URTSSelectionSubsystem>()
		: nullptr;
	if (Selection == nullptr)
	{
		return;
	}
	const float CellSize = FRTSMilestone2Configuration::Load().World.PlacementCellSize;
	for (const ARTSStructure* Structure : Selection->GetLivingStructures())
	{
		if (!IsValid(Structure))
		{
			continue;
		}
		const FIntPoint Footprint = Structure->GetFootprintCells();
		const FVector2D HalfExtent(Footprint.X * CellSize * 0.5f, Footprint.Y * CellSize * 0.5f);
		const FVector Center = Structure->GetActorLocation() + FVector(0.0f, 0.0f, 12.0f);
		const FVector Corners[] = {
			Center + FVector(-HalfExtent.X, -HalfExtent.Y, 0.0f),
			Center + FVector(HalfExtent.X, -HalfExtent.Y, 0.0f),
			Center + FVector(HalfExtent.X, HalfExtent.Y, 0.0f),
			Center + FVector(-HalfExtent.X, HalfExtent.Y, 0.0f)};
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector Start = Project(Corners[Index]);
			const FVector End = Project(Corners[(Index + 1) % 4]);
			if (Start.Z > 0.0f && End.Z > 0.0f)
			{
				DrawLine(Start.X, Start.Y, End.X, End.Y, FLinearColor(0.15f, 1.0f, 0.75f), 3.0f * GetViewportUIScale());
			}
		}
	}
}

void ARTSHUD::DrawConstructionStatus()
{
	if (GetWorld() == nullptr)
	{
		return;
	}
	for (TActorIterator<ARTSStructure> StructureIterator(GetWorld()); StructureIterator; ++StructureIterator)
	{
		const ARTSStructure& Structure = **StructureIterator;
		if (!Structure.IsAlive() || Structure.IsConstructed())
		{
			continue;
		}
		const FVector Screen = Project(Structure.GetActorLocation() + FVector(0.0f, 0.0f, 650.0f));
		if (Screen.Z <= 0.0f)
		{
			continue;
		}
		const float Progress = Structure.GetConstructionProgress();
		const float UIScale = GetViewportUIScale();
		const float BarWidth = 96.0f * UIScale;
		const float BarHeight = 10.0f * UIScale;
		const float Border = 2.0f * UIScale;
		DrawRect(FLinearColor::Black, Screen.X - BarWidth * 0.5f, Screen.Y, BarWidth, BarHeight);
		DrawRect(
			FLinearColor(0.95f, 0.72f, 0.12f),
			Screen.X - BarWidth * 0.5f + Border,
			Screen.Y + Border,
			(BarWidth - Border * 2.0f) * Progress,
			BarHeight - Border * 2.0f);
		DrawText(
			FString::Printf(
				TEXT("%s  %d%%"),
				RTSPresentationText::StructureTypeLabel(Structure.GetStructureType()),
				FMath::RoundToInt(Progress * 100.0f)),
			FLinearColor(1.0f, 0.88f, 0.45f),
			Screen.X - BarWidth * 0.5f,
			Screen.Y - 22.0f * UIScale,
			nullptr,
			0.8f * UIScale);
	}
}

void ARTSHUD::DrawProductionStatus()
{
	if (GetWorld() == nullptr)
	{
		return;
	}
	for (TActorIterator<ARTSStructure> StructureIterator(GetWorld()); StructureIterator; ++StructureIterator)
	{
		const ARTSStructure& Structure = **StructureIterator;
		const URTSProductionComponent* Production = Structure.GetProductionComponent();
		if (!Structure.IsAlive() || !Structure.IsConstructed() || Production == nullptr)
		{
			continue;
		}
		const FRTSProductionSnapshot Snapshot = Production->GetSnapshot();
		if (Snapshot.Queue.IsEmpty())
		{
			continue;
		}
		const FRTSProductionQueueEntrySnapshot& FrontEntry = Snapshot.Queue[0];
		const FVector Screen = Project(Structure.GetActorLocation() + FVector(0.0f, 0.0f, 650.0f));
		if (Screen.Z <= 0.0f)
		{
			continue;
		}
		const float UIScale = GetViewportUIScale();
		const float BarWidth = 128.0f * UIScale;
		const float BarHeight = 10.0f * UIScale;
		const float Border = 2.0f * UIScale;
		const FLinearColor ProgressColor = Structure.GetGenericTeamId() == RTSTeams::Player
			? FLinearColor(0.15f, 0.75f, 1.0f)
			: FLinearColor(1.0f, 0.32f, 0.16f);
		DrawRect(FLinearColor::Black, Screen.X - BarWidth * 0.5f, Screen.Y, BarWidth, BarHeight);
		DrawRect(
			ProgressColor,
			Screen.X - BarWidth * 0.5f + Border,
			Screen.Y + Border,
			(BarWidth - Border * 2.0f) * FrontEntry.Progress,
			BarHeight - Border * 2.0f);
		DrawText(
			FString::Printf(
				TEXT("PRODUCING %s  %d%%  QUEUE %d"),
				RTSPresentationText::UnitTypeLabel(FrontEntry.UnitType),
				FMath::RoundToInt(FrontEntry.Progress * 100.0f),
				Snapshot.Queue.Num()),
			ProgressColor,
			Screen.X - BarWidth * 0.5f,
			Screen.Y - 22.0f * UIScale,
			nullptr,
			0.8f * UIScale);
	}
}

void ARTSHUD::DrawDeploymentPreview()
{
	const ARTSPlayerController* RTSController = Cast<ARTSPlayerController>(GetOwningPlayerController());
	const ULocalPlayer* LocalPlayer = RTSController != nullptr ? RTSController->GetLocalPlayer() : nullptr;
	const URTSDeploymentViewSubsystem* View = LocalPlayer != nullptr
		? LocalPlayer->GetSubsystem<URTSDeploymentViewSubsystem>()
		: nullptr;
	if (View == nullptr)
	{
		return;
	}

	if (ARTSCombatUnit* CommandVehicle = View->GetCommandVehicle())
	{
		const URTSStructureSubsystem* Structures = GetWorld() != nullptr
			? GetWorld()->GetSubsystem<URTSStructureSubsystem>()
			: nullptr;
		if (Structures == nullptr)
		{
			return;
		}
		const FRTSHeadquartersDeploymentPreview Preview =
			Structures->EvaluateHeadquartersDeployment(*CommandVehicle);
		const FRTSMilestone2Configuration Configuration = FRTSMilestone2Configuration::Load();
		const FVector2D HalfFootprint(
			Preview.FootprintCells.X * Configuration.World.PlacementCellSize * 0.5f,
			Preview.FootprintCells.Y * Configuration.World.PlacementCellSize * 0.5f);
		for (TActorIterator<ARTSStartingZone> ZoneIterator(GetWorld()); ZoneIterator; ++ZoneIterator)
		{
			if (ZoneIterator->GetGenericTeamId() != CommandVehicle->GetGenericTeamId())
			{
				continue;
			}
			const float DeployableCenterRadius = FMath::Max(
				0.0f,
				ZoneIterator->GetRadius() - HalfFootprint.Size());
			constexpr int32 LimitSegments = 48;
			for (int32 SegmentIndex = 0; SegmentIndex < LimitSegments; SegmentIndex += 2)
			{
				const float StartAngle = 2.0f * UE_PI * SegmentIndex / LimitSegments;
				const float EndAngle = 2.0f * UE_PI * (SegmentIndex + 1) / LimitSegments;
				const FVector Start = Project(ZoneIterator->GetActorLocation() + FVector(
					FMath::Cos(StartAngle) * DeployableCenterRadius,
					FMath::Sin(StartAngle) * DeployableCenterRadius,
					12.0f));
				const FVector End = Project(ZoneIterator->GetActorLocation() + FVector(
					FMath::Cos(EndAngle) * DeployableCenterRadius,
					FMath::Sin(EndAngle) * DeployableCenterRadius,
					12.0f));
				if (Start.Z > 0.0f && End.Z > 0.0f)
				{
					DrawLine(
						Start.X,
						Start.Y,
						End.X,
						End.Y,
						FLinearColor(0.2f, 0.85f, 1.0f),
						GetViewportUIScale());
				}
			}
			FVector2D VehicleDirection(
				CommandVehicle->GetActorLocation().X - ZoneIterator->GetActorLocation().X,
				CommandVehicle->GetActorLocation().Y - ZoneIterator->GetActorLocation().Y);
			VehicleDirection = VehicleDirection.GetSafeNormal();
			if (VehicleDirection.IsNearlyZero())
			{
				VehicleDirection = FVector2D(0.0f, -1.0f);
			}
			const FVector LimitLabel = Project(ZoneIterator->GetActorLocation() + FVector(
				VehicleDirection.X * DeployableCenterRadius,
				VehicleDirection.Y * DeployableCenterRadius,
				12.0f));
			if (LimitLabel.Z > 0.0f)
			{
				DrawText(
					TEXT("HQ CENTER LIMIT"),
					FLinearColor(0.2f, 0.85f, 1.0f),
					LimitLabel.X - 52.0f * GetViewportUIScale(),
					LimitLabel.Y,
					nullptr,
					0.7f * GetViewportUIScale());
			}
			break;
		}
		// The start-zone limit remains a screen-space instruction. The footprint and build radius
		// are world-space HLSL overlays submitted by the controller from this same preview.
		return;
	}
}

void ARTSHUD::DrawAttackTargets(const TConstArrayView<ARTSCombatUnit*> SelectedUnits)
{
	struct FAttackTargetProjection
	{
		const ARTSCombatUnit* Target = nullptr;
		int32 SelectedAttackerCount = 0;
	};

	TArray<FAttackTargetProjection> TargetProjections;
	for (const ARTSCombatUnit* SelectedUnit : SelectedUnits)
	{
		if (!IsValid(SelectedUnit))
		{
			continue;
		}

		const FRTSOrderSnapshot Order = SelectedUnit->GetOrderComponent()->GetSnapshot();
		const ARTSCombatUnit* Target = Order.Target.Get();
		if (Order.Kind != ERTSOrderKind::Attack
			|| !IsValid(Target)
			|| !Target->IsAlive())
		{
			continue;
		}

		FAttackTargetProjection* Projection = TargetProjections.FindByPredicate(
			[Target](const FAttackTargetProjection& Candidate)
			{
				return Candidate.Target == Target;
			});
		if (Projection == nullptr)
		{
			Projection = &TargetProjections.AddDefaulted_GetRef();
			Projection->Target = Target;
		}
		++Projection->SelectedAttackerCount;
	}

	// Stable ordering keeps overlapping labels and rendered acceptance evidence reproducible.
	TargetProjections.Sort([](const FAttackTargetProjection& Left, const FAttackTargetProjection& Right)
	{
		return Left.Target->GetStableUnitId() < Right.Target->GetStableUnitId();
	});

	for (const FAttackTargetProjection& Projection : TargetProjections)
	{
		const ARTSCombatUnit* Target = Projection.Target;

		const FVector TargetScreenPosition = Project(
			Target->GetActorLocation() + FVector(0.0f, 0.0f, 40.0f));
		if (TargetScreenPosition.Z <= 0.0f)
		{
			continue;
		}

		const float UIScale = GetViewportUIScale();
		const float ReticleRadius = FMath::Clamp(
			GetUnitScreenRadius(*Target) + 10.0f * UIScale,
			18.0f * UIScale,
			30.0f * UIScale);
		const FColor ReticleColor(255, 70, 45);
		DrawLine(
			TargetScreenPosition.X,
			TargetScreenPosition.Y - ReticleRadius,
			TargetScreenPosition.X + ReticleRadius,
			TargetScreenPosition.Y,
			FLinearColor(ReticleColor),
			2.0f * UIScale);
		DrawLine(
			TargetScreenPosition.X + ReticleRadius,
			TargetScreenPosition.Y,
			TargetScreenPosition.X,
			TargetScreenPosition.Y + ReticleRadius,
			FLinearColor(ReticleColor),
			2.0f * UIScale);
		DrawLine(
			TargetScreenPosition.X,
			TargetScreenPosition.Y + ReticleRadius,
			TargetScreenPosition.X - ReticleRadius,
			TargetScreenPosition.Y,
			FLinearColor(ReticleColor),
			2.0f * UIScale);
		DrawLine(
			TargetScreenPosition.X - ReticleRadius,
			TargetScreenPosition.Y,
			TargetScreenPosition.X,
			TargetScreenPosition.Y - ReticleRadius,
			FLinearColor(ReticleColor),
			2.0f * UIScale);
		DrawText(
			FString::Printf(
				TEXT("ATTACK E%d  x%d"),
				Target->GetStableUnitId(),
				Projection.SelectedAttackerCount),
			FLinearColor(1.0f, 0.2f, 0.12f),
			TargetScreenPosition.X + ReticleRadius + 8.0f * UIScale,
			TargetScreenPosition.Y - 8.0f * UIScale,
			nullptr,
			0.8f * UIScale);
	}

	struct FStructureAttackProjection
	{
		const ARTSStructure* Target = nullptr;
		int32 SelectedAttackerCount = 0;
	};
	TArray<FStructureAttackProjection> StructureTargets;
	for (const ARTSCombatUnit* SelectedUnit : SelectedUnits)
	{
		if (!IsValid(SelectedUnit))
		{
			continue;
		}
		const FRTSOrderSnapshot Order = SelectedUnit->GetOrderComponent()->GetSnapshot();
		const ARTSStructure* Target = Order.TargetStructure.Get();
		if (Order.Kind != ERTSOrderKind::Attack || !IsValid(Target) || !Target->IsAlive())
		{
			continue;
		}
		FStructureAttackProjection* Projection = StructureTargets.FindByPredicate(
			[Target](const FStructureAttackProjection& Candidate) { return Candidate.Target == Target; });
		if (Projection == nullptr)
		{
			Projection = &StructureTargets.AddDefaulted_GetRef();
			Projection->Target = Target;
		}
		++Projection->SelectedAttackerCount;
	}
	StructureTargets.Sort([](const FStructureAttackProjection& Left, const FStructureAttackProjection& Right)
	{
		return Left.Target->GetStableStructureId() < Right.Target->GetStableStructureId();
	});
	for (const FStructureAttackProjection& Projection : StructureTargets)
	{
		const FVector Screen = Project(Projection.Target->GetActorLocation() + FVector(0.0f, 0.0f, 560.0f));
		if (Screen.Z <= 0.0f)
		{
			continue;
		}
		const float UIScale = GetViewportUIScale();
		const float Radius = 30.0f * UIScale;
		const FLinearColor Color(1.0f, 0.18f, 0.1f);
		DrawLine(Screen.X, Screen.Y - Radius, Screen.X + Radius, Screen.Y, Color, 3.0f * UIScale);
		DrawLine(Screen.X + Radius, Screen.Y, Screen.X, Screen.Y + Radius, Color, 3.0f * UIScale);
		DrawLine(Screen.X, Screen.Y + Radius, Screen.X - Radius, Screen.Y, Color, 3.0f * UIScale);
		DrawLine(Screen.X - Radius, Screen.Y, Screen.X, Screen.Y - Radius, Color, 3.0f * UIScale);
		DrawText(
			FString::Printf(TEXT("ATTACK S%d  x%d"), Projection.Target->GetStableStructureId(), Projection.SelectedAttackerCount),
			Color,
			Screen.X + Radius + 8.0f * UIScale,
			Screen.Y - 8.0f * UIScale,
			nullptr,
			0.8f * UIScale);
	}
}

void ARTSHUD::DrawMoveDestination()
{
	const UWorld* World = GetWorld();
	const URTSCommandSubsystem* Commands = World != nullptr ? World->GetSubsystem<URTSCommandSubsystem>() : nullptr;
	const TOptional<FVector> Destination = Commands != nullptr ? Commands->GetLastMoveDestination() : TOptional<FVector>();
	if (!Destination.IsSet())
	{
		return;
	}

	const FVector ScreenPosition = Project(Destination.GetValue());
	if (ScreenPosition.Z <= 0.0f)
	{
		return;
	}

	const float UIScale = GetViewportUIScale();
	const float MarkerRadius = 10.0f * UIScale;
	const FColor MarkerColor(255, 210, 60);
	DrawLine(
		ScreenPosition.X - MarkerRadius,
		ScreenPosition.Y,
		ScreenPosition.X + MarkerRadius,
		ScreenPosition.Y,
		FLinearColor(MarkerColor),
		2.0f * UIScale);
	DrawLine(
		ScreenPosition.X,
		ScreenPosition.Y - MarkerRadius,
		ScreenPosition.X,
		ScreenPosition.Y + MarkerRadius,
		FLinearColor(MarkerColor),
		2.0f * UIScale);
	DrawText(
		TEXT("MOVE"),
		FLinearColor(1.0f, 0.82f, 0.2f),
		ScreenPosition.X + 14.0f * UIScale,
		ScreenPosition.Y - 8.0f * UIScale,
		nullptr,
		0.78f * UIScale);
}

void ARTSHUD::DrawUnitAffiliation(const ARTSCombatUnit& Unit, const bool bSelected)
{
	const FVector UnitScreenPosition = Project(Unit.GetStatusAnchorWorldLocation());
	if (UnitScreenPosition.Z <= 0.0f)
	{
		return;
	}

	const FRTSHealthSnapshot Health = Unit.GetHealthComponent()->GetSnapshot();
	if (!bSelected && Health.CurrentHealth >= Health.MaximumHealth)
	{
		return;
	}

	const bool bFriendly = Unit.GetGenericTeamId() == RTSTeams::Player;
	const float UIScale = GetViewportUIScale();
	const FLinearColor TeamColor = bFriendly
		? FLinearColor(0.15f, 0.75f, 1.0f)
		: FLinearColor(1.0f, 0.2f, 0.15f);
	DrawText(
		FString::Printf(TEXT("%s %d"), bFriendly ? TEXT("P") : TEXT("E"), Unit.GetStableUnitId()),
		TeamColor,
		UnitScreenPosition.X - 14.0f * UIScale,
		UnitScreenPosition.Y - 14.0f * UIScale,
		nullptr,
		0.78f * UIScale);

	const float HealthRatio = Health.MaximumHealth > 0.0f
		? FMath::Clamp(Health.CurrentHealth / Health.MaximumHealth, 0.0f, 1.0f)
		: 0.0f;
	const float BarWidth = FMath::Clamp(
		GetUnitScreenRadius(Unit) * 2.4f,
		32.0f * UIScale,
		54.0f * UIScale);
	const float BarHeight = 7.0f * UIScale;
	const float Border = 2.0f * UIScale;
	DrawRect(
		FLinearColor::Black,
		UnitScreenPosition.X - BarWidth * 0.5f,
		UnitScreenPosition.Y + 6.0f * UIScale,
		BarWidth,
		BarHeight);
	DrawRect(
		FLinearColor(0.2f, 0.9f, 0.35f),
		UnitScreenPosition.X - BarWidth * 0.5f + Border,
		UnitScreenPosition.Y + 6.0f * UIScale + Border,
		(BarWidth - Border * 2.0f) * HealthRatio,
		FMath::Max(1.0f, BarHeight - Border * 2.0f));
}

void ARTSHUD::DrawStructureAffiliation(
	const ARTSStructure& Structure,
	const bool bSelected,
	const bool bAttackTarget)
{
	const FRTSHealthSnapshot Health = Structure.GetHealthComponent()->GetSnapshot();
	// Structure bars are combat context, not permanent map labels. Selection, received damage,
	// or an active attack commitment makes health decision-relevant; idle full-health bases stay clear.
	if (!bSelected && !bAttackTarget && Health.CurrentHealth >= Health.MaximumHealth)
	{
		return;
	}

	const FBox Bounds = Structure.GetComponentsBoundingBox(true);
	const FVector Screen = Project(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Max.Z + 45.0f));
	if (Screen.Z <= 0.0f)
	{
		return;
	}

	const float UIScale = GetViewportUIScale();
	const bool bFriendly = Structure.GetGenericTeamId() == RTSTeams::Player;
	const FLinearColor TeamColor = bFriendly
		? FLinearColor(0.15f, 0.75f, 1.0f)
		: FLinearColor(1.0f, 0.2f, 0.15f);
	const float HealthRatio = Health.MaximumHealth > 0.0f
		? FMath::Clamp(Health.CurrentHealth / Health.MaximumHealth, 0.0f, 1.0f)
		: 0.0f;
	const FLinearColor HealthColor = HealthRatio > 0.6f
		? FLinearColor(0.2f, 0.9f, 0.35f)
		: HealthRatio > 0.3f
			? FLinearColor(0.95f, 0.72f, 0.12f)
			: FLinearColor(1.0f, 0.2f, 0.15f);
	const int32 LongestFootprintSide = FMath::Max(
		Structure.GetFootprintCells().X,
		Structure.GetFootprintCells().Y);
	const float BarWidth = FMath::Clamp(
		40.0f + LongestFootprintSide * 14.0f,
		68.0f,
		124.0f) * UIScale;
	const float BarHeight = 9.0f * UIScale;
	const float Border = 2.0f * UIScale;
	DrawText(
		FString::Printf(
			TEXT("%s S%d  %d%%"),
			bFriendly ? TEXT("P") : TEXT("E"),
			Structure.GetStableStructureId(),
			FMath::RoundToInt(HealthRatio * 100.0f)),
		TeamColor,
		Screen.X - BarWidth * 0.5f,
		Screen.Y - 17.0f * UIScale,
		nullptr,
		0.75f * UIScale);
	DrawRect(
		FLinearColor::Black,
		Screen.X - BarWidth * 0.5f,
		Screen.Y,
		BarWidth,
		BarHeight);
	DrawRect(
		HealthColor,
		Screen.X - BarWidth * 0.5f + Border,
		Screen.Y + Border,
		(BarWidth - Border * 2.0f) * HealthRatio,
		FMath::Max(1.0f, BarHeight - Border * 2.0f));
}

void ARTSHUD::DrawAttackFeedback(const ARTSCombatUnit& Unit)
{
	const FRTSOrderSnapshot Order = Unit.GetOrderComponent()->GetSnapshot();
	const UWorld* World = GetWorld();
	const AActor* TargetActor = IsValid(Order.Target)
		? static_cast<const AActor*>(Order.Target.Get())
		: static_cast<const AActor*>(Order.TargetStructure.Get());
	if (World == nullptr
		|| !IsValid(TargetActor)
		|| Order.LastAttackTimeSeconds < 0.0
		|| World->GetTimeSeconds() - Order.LastAttackTimeSeconds
			> FRTSCombatTuning::Load().AttackFeedbackDurationSeconds)
	{
		return;
	}

	const FVector AttackerScreenPosition = Project(Unit.GetActorLocation() + FVector(0.0f, 0.0f, 70.0f));
	const FVector TargetOffset = IsValid(Order.TargetStructure)
		? FVector(0.0f, 0.0f, 560.0f)
		: FVector(0.0f, 0.0f, 70.0f);
	const FVector TargetScreenPosition = Project(TargetActor->GetActorLocation() + TargetOffset);
	if (AttackerScreenPosition.Z > 0.0f && TargetScreenPosition.Z > 0.0f)
	{
		DrawLine(
			AttackerScreenPosition.X,
			AttackerScreenPosition.Y,
			TargetScreenPosition.X,
			TargetScreenPosition.Y,
			FLinearColor(FColor(255, 210, 70)),
			2.0f * GetViewportUIScale());
	}
}

void ARTSHUD::SetSelectionMarquee(const TOptional<FBox2D>& NewMarquee)
{
	SelectionMarquee = NewMarquee;
}

void ARTSHUD::DrawMarquee()
{
	if (!SelectionMarquee.IsSet())
	{
		return;
	}

	const FBox2D& Marquee = SelectionMarquee.GetValue();
	const FColor BorderColor(60, 210, 255);
	const float LineThickness = 2.0f * GetViewportUIScale();
	DrawLine(Marquee.Min.X, Marquee.Min.Y, Marquee.Max.X, Marquee.Min.Y, FLinearColor(BorderColor), LineThickness);
	DrawLine(Marquee.Max.X, Marquee.Min.Y, Marquee.Max.X, Marquee.Max.Y, FLinearColor(BorderColor), LineThickness);
	DrawLine(Marquee.Max.X, Marquee.Max.Y, Marquee.Min.X, Marquee.Max.Y, FLinearColor(BorderColor), LineThickness);
	DrawLine(Marquee.Min.X, Marquee.Max.Y, Marquee.Min.X, Marquee.Min.Y, FLinearColor(BorderColor), LineThickness);
}

void ARTSHUD::DrawSelectedUnit(const ARTSCombatUnit& Unit)
{
	const FVector UnitScreenPosition = Project(Unit.GetActorLocation());
	if (UnitScreenPosition.Z <= 0.0f)
	{
		return;
	}

	const float UIScale = GetViewportUIScale();
	const float MarkerRadius = FMath::Clamp(
		GetUnitScreenRadius(Unit) + 4.0f * UIScale,
		12.0f * UIScale,
		22.0f * UIScale);
	const FColor SelectionColor(60, 210, 255);
	DrawLine(
		UnitScreenPosition.X - MarkerRadius,
		UnitScreenPosition.Y,
		UnitScreenPosition.X,
		UnitScreenPosition.Y + MarkerRadius * 0.5f,
		FLinearColor(SelectionColor),
		2.0f * UIScale);
	DrawLine(
		UnitScreenPosition.X,
		UnitScreenPosition.Y + MarkerRadius * 0.5f,
		UnitScreenPosition.X + MarkerRadius,
		UnitScreenPosition.Y,
		FLinearColor(SelectionColor),
		2.0f * UIScale);
	DrawLine(
		UnitScreenPosition.X + MarkerRadius,
		UnitScreenPosition.Y,
		UnitScreenPosition.X,
		UnitScreenPosition.Y - MarkerRadius * 0.5f,
		FLinearColor(SelectionColor),
		2.0f * UIScale);
	DrawLine(
		UnitScreenPosition.X,
		UnitScreenPosition.Y - MarkerRadius * 0.5f,
		UnitScreenPosition.X - MarkerRadius,
		UnitScreenPosition.Y,
		FLinearColor(SelectionColor),
		2.0f * UIScale);
}

float ARTSHUD::GetUnitScreenRadius(const ARTSCombatUnit& Unit) const
{
	const FVector Center = Project(Unit.GetActorLocation());
	const float WorldRadius = Unit.GetCapsuleComponent()->GetScaledCapsuleRadius();
	const FVector HorizontalEdge = Project(Unit.GetActorLocation() + FVector(0.0f, WorldRadius, 0.0f));
	if (Center.Z <= 0.0f || HorizontalEdge.Z <= 0.0f)
	{
		return 10.0f * GetViewportUIScale();
	}

	const float UIScale = GetViewportUIScale();
	return FMath::Clamp(
		FVector2D::Distance(
			FVector2D(Center.X, Center.Y),
			FVector2D(HorizontalEdge.X, HorizontalEdge.Y)),
		8.0f * UIScale,
		18.0f * UIScale);
}

float ARTSHUD::GetViewportUIScale() const
{
	if (Canvas == nullptr)
	{
		return 1.0f;
	}

	return GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(
		FIntPoint(Canvas->SizeX, Canvas->SizeY));
}
