// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/RTSCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/ConfigCacheIni.h"
#include "World/RTSCameraBounds.h"

FRTSCameraTuning FRTSCameraTuning::Load()
{
	FRTSCameraTuning Tuning;
	constexpr TCHAR Section[] = TEXT("RTS.Milestone1.Camera");
	GConfig->GetFloat(Section, TEXT("PitchDegrees"), Tuning.PitchDegrees, GGameIni);
	GConfig->GetFloat(Section, TEXT("YawDegrees"), Tuning.YawDegrees, GGameIni);
	GConfig->GetFloat(Section, TEXT("InitialZoomDistance"), Tuning.InitialZoomDistance, GGameIni);
	GConfig->GetFloat(Section, TEXT("MinimumZoomDistance"), Tuning.MinimumZoomDistance, GGameIni);
	GConfig->GetFloat(Section, TEXT("MaximumZoomDistance"), Tuning.MaximumZoomDistance, GGameIni);
	GConfig->GetFloat(Section, TEXT("PanHalfExtent"), Tuning.PanHalfExtent, GGameIni);
	GConfig->GetFloat(Section, TEXT("NearPanSpeed"), Tuning.NearPanSpeed, GGameIni);
	GConfig->GetFloat(Section, TEXT("FarPanSpeed"), Tuning.FarPanSpeed, GGameIni);
	GConfig->GetFloat(Section, TEXT("ZoomStep"), Tuning.ZoomStep, GGameIni);
	GConfig->GetFloat(
		Section,
		TEXT("ZoomInterpolationSpeed"),
		Tuning.ZoomInterpolationSpeed,
		GGameIni);

	Tuning.PitchDegrees = FMath::Clamp(Tuning.PitchDegrees, -80.0f, -25.0f);
	Tuning.YawDegrees = FMath::UnwindDegrees(Tuning.YawDegrees);
	Tuning.MinimumZoomDistance = FMath::Max(1000.0f, Tuning.MinimumZoomDistance);
	Tuning.MaximumZoomDistance = FMath::Max(Tuning.MinimumZoomDistance, Tuning.MaximumZoomDistance);
	Tuning.InitialZoomDistance = FMath::Clamp(
		Tuning.InitialZoomDistance,
		Tuning.MinimumZoomDistance,
		Tuning.MaximumZoomDistance);
	Tuning.PanHalfExtent = FMath::Max(0.0f, Tuning.PanHalfExtent);
	Tuning.NearPanSpeed = FMath::Max(1.0f, Tuning.NearPanSpeed);
	Tuning.FarPanSpeed = FMath::Max(Tuning.NearPanSpeed, Tuning.FarPanSpeed);
	Tuning.ZoomStep = FMath::Max(1.0f, Tuning.ZoomStep);
	Tuning.ZoomInterpolationSpeed = FMath::Max(0.01f, Tuning.ZoomInterpolationSpeed);
	return Tuning;
}

ARTSCameraPawn::ARTSCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	CameraTuning = FRTSCameraTuning::Load();
	PlanarHalfExtent = FVector2D(CameraTuning.PanHalfExtent);
	TargetZoomDistance = CameraTuning.InitialZoomDistance;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(SceneRoot);
	SpringArm->SetRelativeRotation(FRotator(
		CameraTuning.PitchDegrees,
		CameraTuning.YawDegrees,
		0.0f));
	SpringArm->TargetArmLength = CameraTuning.InitialZoomDistance;
	SpringArm->bDoCollisionTest = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
}

void ARTSCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	ARTSCameraBounds* MapBounds = nullptr;
	for (TActorIterator<ARTSCameraBounds> ActorIterator(GetWorld()); ActorIterator; ++ActorIterator)
	{
		if (MapBounds != nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("The active RTS map contains more than one camera-bounds actor."));
			return;
		}
		MapBounds = *ActorIterator;
	}
	if (MapBounds != nullptr)
	{
		PlanarHalfExtent = MapBounds->GetPlanarHalfExtent();
	}
}

void ARTSCameraPawn::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const FVector2D CombinedInput = (KeyboardPanInput + EdgePanInput).GetClampedToMaxSize(1.0f);
	const float ZoomAlpha = FMath::GetRangePct(
		CameraTuning.MinimumZoomDistance,
		CameraTuning.MaximumZoomDistance,
		SpringArm->TargetArmLength);
	const float PanSpeed = FMath::Lerp(
		CameraTuning.NearPanSpeed,
		CameraTuning.FarPanSpeed,
		ZoomAlpha);
	const FVector PlanarViewForward = SpringArm->GetForwardVector().GetSafeNormal2D();
	const FVector PlanarViewRight = SpringArm->GetRightVector().GetSafeNormal2D();
	const FVector PanDirection =
		PlanarViewForward * CombinedInput.Y + PlanarViewRight * CombinedInput.X;
	FVector NewLocation = GetActorLocation();
	NewLocation += PanDirection * PanSpeed * DeltaSeconds;
	NewLocation.X = FMath::Clamp(
		NewLocation.X,
		-PlanarHalfExtent.X,
		PlanarHalfExtent.X);
	NewLocation.Y = FMath::Clamp(
		NewLocation.Y,
		-PlanarHalfExtent.Y,
		PlanarHalfExtent.Y);
	SetActorLocation(NewLocation);

	SpringArm->TargetArmLength = FMath::FInterpTo(
		SpringArm->TargetArmLength,
		TargetZoomDistance,
		DeltaSeconds,
		CameraTuning.ZoomInterpolationSpeed);
}

void ARTSCameraPawn::SetKeyboardPanInput(const FVector2D& NewPanInput)
{
	KeyboardPanInput = NewPanInput;
}

void ARTSCameraPawn::SetEdgePanInput(const FVector2D& NewPanInput)
{
	EdgePanInput = NewPanInput;
}

void ARTSCameraPawn::AddZoomInput(const float ZoomSteps)
{
	TargetZoomDistance = FMath::Clamp(
		TargetZoomDistance - ZoomSteps * CameraTuning.ZoomStep,
		CameraTuning.MinimumZoomDistance,
		CameraTuning.MaximumZoomDistance);
}

float ARTSCameraPawn::GetZoomDistance() const
{
	return SpringArm->TargetArmLength;
}

FVector2D ARTSCameraPawn::GetPlanarLocation() const
{
	const FVector Location = GetActorLocation();
	return FVector2D(Location.X, Location.Y);
}
