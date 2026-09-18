// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/RTSWorldOverlayShowcaseActor.h"

#include "AI/RTSAIStrategySubsystem.h"
#include "Camera/RTSCameraPawn.h"
#include "Components/SceneComponent.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Presentation/RTSWorldOverlaySubsystem.h"
#include "TimerManager.h"
#include "Units/RTSTeams.h"
#include "UnrealClient.h"

ARTSWorldOverlayShowcaseActor::ARTSWorldOverlayShowcaseActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void ARTSWorldOverlayShowcaseActor::BeginPlay()
{
	Super::BeginPlay();
	if (FParse::Param(FCommandLine::Get(), TEXT("RTSOverlayShowcase")))
	{
		ActivateShowcase();
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RTSOverlayShowcaseCapture")))
	{
		GetWorldTimerManager().SetTimer(
			PackagedCaptureTimer,
			this,
			&ARTSWorldOverlayShowcaseActor::FindPackagedCaptureCamera,
			0.1f,
			true,
			0.25f);
	}
}

void ARTSWorldOverlayShowcaseActor::ActivateShowcase()
{
	if (bActivated || GetWorld() == nullptr)
	{
		return;
	}
	URTSWorldOverlaySubsystem* Overlays = GetWorld()->GetSubsystem<URTSWorldOverlaySubsystem>();
	if (Overlays == nullptr)
	{
		return;
	}

	bActivated = true;
	const FVector Center = GetActorLocation();
	auto SubmitCircle = [Overlays, Center](
		const ERTSWorldOverlayMode Mode,
		const int32 StableId,
		const FVector2D Offset,
		const float Radius,
		const FGenericTeamId TeamId)
	{
		FRTSWorldOverlayDescriptor Descriptor;
		Descriptor.Mode = Mode;
		Descriptor.SourceKind = ERTSWorldOverlaySourceKind::Showcase;
		Descriptor.StableSourceId = StableId;
		Descriptor.WorldTransform.SetLocation(Center + FVector(Offset.X, Offset.Y, 0.0f));
		Descriptor.Radius = Radius;
		Descriptor.TeamId = TeamId;
		Descriptor.AnimationPhaseSeconds = StableId * 0.17f;
		Overlays->Submit(Descriptor);
	};
	auto SubmitFootprint = [Overlays, Center](
		const int32 StableId,
		const FVector2D Offset,
		const ERTSWorldOverlayValidity Validity)
	{
		FRTSWorldOverlayDescriptor Descriptor;
		Descriptor.Mode = ERTSWorldOverlayMode::PlacementFootprint;
		Descriptor.SourceKind = ERTSWorldOverlaySourceKind::Showcase;
		Descriptor.StableSourceId = StableId;
		Descriptor.WorldTransform.SetLocation(Center + FVector(Offset.X, Offset.Y, 0.0f));
		Descriptor.HalfExtent = FVector2D(300.0f, 220.0f);
		Descriptor.Validity = Validity;
		Descriptor.TeamId = RTSTeams::Player;
		Descriptor.AnimationPhaseSeconds = StableId * 0.17f;
		Overlays->Submit(Descriptor);
	};

	SubmitCircle(ERTSWorldOverlayMode::BuildArea, 1, FVector2D(0.0f, -300.0f), 650.0f, RTSTeams::Player);
	SubmitFootprint(2, FVector2D(-750.0f, -500.0f), ERTSWorldOverlayValidity::Valid);
	SubmitFootprint(3, FVector2D(750.0f, -500.0f), ERTSWorldOverlayValidity::Invalid);
	SubmitCircle(ERTSWorldOverlayMode::TurretRange, 4, FVector2D(-500.0f, 600.0f), 400.0f, RTSTeams::Player);
	SubmitCircle(ERTSWorldOverlayMode::TurretRange, 5, FVector2D(500.0f, 600.0f), 550.0f, RTSTeams::Enemy);
}

void ARTSWorldOverlayShowcaseActor::FindPackagedCaptureCamera()
{
	APlayerController* Controller = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	ARTSCameraPawn* Camera = Controller != nullptr ? Cast<ARTSCameraPawn>(Controller->GetPawn()) : nullptr;
	if (Camera == nullptr)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(PackagedCaptureTimer);
	if (URTSAIStrategySubsystem* Strategy = GetWorld()->GetSubsystem<URTSAIStrategySubsystem>())
	{
		Strategy->StopFaction(RTSTeams::Enemy);
	}
	ActivateShowcase();
	PackagedCaptureCamera = Camera;
	PackagedCaptureCameraLocation = GetActorLocation();
	PackagedCaptureCameraLocation.Z = 100.0f;
	Camera->AddZoomInput(100.0f);
	PinPackagedCaptureCamera();
	GetWorldTimerManager().SetTimer(
		PackagedCaptureTimer,
		this,
		&ARTSWorldOverlayShowcaseActor::CapturePackagedNormal,
		1.5f,
		false);
}

void ARTSWorldOverlayShowcaseActor::CapturePackagedNormal()
{
	PinPackagedCaptureCamera();
	RequestPackagedScreenshot(TEXT("M3Slice6-Overlay-Packaged-Normal.png"));
	GetWorldTimerManager().SetTimer(
		PackagedCaptureTimer,
		this,
		&ARTSWorldOverlayShowcaseActor::PreparePackagedExtreme,
		0.5f,
		false);
}

void ARTSWorldOverlayShowcaseActor::PreparePackagedExtreme()
{
	if (ARTSCameraPawn* Camera = PackagedCaptureCamera.Get())
	{
		Camera->AddZoomInput(-100.0f);
	}
	if (URTSWorldOverlaySubsystem* Overlays = GetWorld()->GetSubsystem<URTSWorldOverlaySubsystem>())
	{
		FRTSWorldOverlayDescriptor ConfiguredTurret;
		ConfiguredTurret.Mode = ERTSWorldOverlayMode::TurretRange;
		ConfiguredTurret.SourceKind = ERTSWorldOverlaySourceKind::Showcase;
		ConfiguredTurret.StableSourceId = 5;
		ConfiguredTurret.WorldTransform.SetLocation(GetActorLocation() + FVector(500.0f, 600.0f, 0.0f));
		ConfiguredTurret.Radius = FRTSMilestone2Configuration::Load().DefensiveTurret.AttackRange;
		ConfiguredTurret.TeamId = RTSTeams::Enemy;
		Overlays->Submit(ConfiguredTurret);
	}
	GetWorldTimerManager().SetTimer(
		PackagedCaptureTimer,
		this,
		&ARTSWorldOverlayShowcaseActor::CapturePackagedExtreme,
		1.0f,
		false);
}

void ARTSWorldOverlayShowcaseActor::CapturePackagedExtreme()
{
	PinPackagedCaptureCamera();
	RequestPackagedScreenshot(TEXT("M3Slice6-Overlay-Packaged-Extreme-Configured.png"));
	GetWorldTimerManager().SetTimer(
		PackagedCaptureTimer,
		this,
		&ARTSWorldOverlayShowcaseActor::FinishPackagedCapture,
		0.5f,
		false);
}

void ARTSWorldOverlayShowcaseActor::FinishPackagedCapture()
{
	FPlatformMisc::RequestExit(false);
}

void ARTSWorldOverlayShowcaseActor::PinPackagedCaptureCamera()
{
	ARTSCameraPawn* Camera = PackagedCaptureCamera.Get();
	APlayerController* Controller = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (Camera == nullptr || Controller == nullptr)
	{
		return;
	}
	Camera->SetKeyboardPanInput(FVector2D::ZeroVector);
	Camera->SetEdgePanInput(FVector2D::ZeroVector);
	Camera->SetActorLocation(PackagedCaptureCameraLocation);
	if (bPackagedCaptureCameraCentered)
	{
		return;
	}

	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	Controller->GetViewportSize(ViewportWidth, ViewportHeight);
	FVector RayOrigin;
	FVector RayDirection;
	if (ViewportWidth > 0
		&& ViewportHeight > 0
		&& Controller->DeprojectScreenPositionToWorld(
			ViewportWidth * 0.5f,
			ViewportHeight * 0.5f,
			RayOrigin,
			RayDirection)
		&& RayDirection.Z < -KINDA_SMALL_NUMBER)
	{
		const FVector GroundCenter = RayOrigin + RayDirection * (-RayOrigin.Z / RayDirection.Z);
		const FVector Adjustment = GetActorLocation() - GroundCenter;
		PackagedCaptureCameraLocation.X += Adjustment.X;
		PackagedCaptureCameraLocation.Y += Adjustment.Y;
		Camera->SetActorLocation(PackagedCaptureCameraLocation);
		bPackagedCaptureCameraCentered = true;
	}
}

void ARTSWorldOverlayShowcaseActor::RequestPackagedScreenshot(const TCHAR* Filename) const
{
	const FString ScreenshotDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/RTS"));
	IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);
	const FString ScreenshotPath = FPaths::Combine(ScreenshotDirectory, Filename);
	IFileManager::Get().Delete(*ScreenshotPath, false, true);
	FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
}
