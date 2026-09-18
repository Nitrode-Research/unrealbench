// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/RTSCameraPawn.h"
#include "Engine/World.h"
#include "Math/RotationMatrix.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1CameraBoundsTest,
	"Task0169.Headless.RTS.Milestone1.Camera.Bounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSMilestone1CameraScreenRelativePanTest,
	"Task0169.Headless.RTS.Milestone1.Camera.ScreenRelativePan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMilestone1CameraBoundsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	World->AddToRoot();
	ARTSCameraPawn* CameraPawn = World->SpawnActor<ARTSCameraPawn>();
	if (!TestNotNull(TEXT("The native RTS camera fixture spawns"), CameraPawn))
	{
		World->DestroyWorld(false);
		World->RemoveFromRoot();
		return false;
	}

	const FRotator InitialRotation = CameraPawn->GetActorRotation();
	const FRTSCameraTuning CameraTuning = FRTSCameraTuning::Load();
	TestEqual(TEXT("The fixed view uses the calibrated diagonal yaw"), CameraTuning.YawDegrees, 40.0f);
	TestEqual(TEXT("The fixed view uses the calibrated top-down pitch"), CameraTuning.PitchDegrees, -42.0f);
	TestEqual(TEXT("The default view opens at the calibrated combat distance"), CameraTuning.InitialZoomDistance, 3500.0f);
	TestEqual(
		TEXT("The camera opens at the configured combat-readable distance"),
		CameraPawn->GetZoomDistance(),
		CameraTuning.InitialZoomDistance);
	CameraPawn->SetKeyboardPanInput(FVector2D(1.0f, 1.0f));
	CameraPawn->Tick(10.0f);
	const FVector2D MaximumPanLocation = CameraPawn->GetPlanarLocation();
	TestTrue(TEXT("Camera X pan remains inside the sandbox"), MaximumPanLocation.X <= CameraTuning.PanHalfExtent);
	TestTrue(TEXT("Camera Y pan remains inside the sandbox"), MaximumPanLocation.Y <= CameraTuning.PanHalfExtent);

	CameraPawn->AddZoomInput(100.0f);
	CameraPawn->Tick(10.0f);
	TestEqual(
		TEXT("Zoom clamps at command-level detail"),
		CameraPawn->GetZoomDistance(),
		CameraTuning.MinimumZoomDistance);
	CameraPawn->AddZoomInput(-100.0f);
	CameraPawn->Tick(10.0f);
	TestEqual(
		TEXT("Zoom clamps at the arena overview"),
		CameraPawn->GetZoomDistance(),
		CameraTuning.MaximumZoomDistance);
	TestTrue(
		TEXT("Pan and zoom never alter the fixed camera rotation"),
		CameraPawn->GetActorRotation().Equals(InitialRotation));

	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return !HasAnyErrors();
}

bool FRTSMilestone1CameraScreenRelativePanTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	World->AddToRoot();
	ARTSCameraPawn* CameraPawn = World->SpawnActor<ARTSCameraPawn>(
		FVector::ZeroVector,
		FRotator(0.0f, 90.0f, 0.0f));
	if (!TestNotNull(TEXT("The rotated RTS camera fixture spawns"), CameraPawn))
	{
		World->DestroyWorld(false);
		World->RemoveFromRoot();
		return false;
	}

	const FRTSCameraTuning CameraTuning = FRTSCameraTuning::Load();
	const FRotator ViewYaw(
		0.0f,
		CameraPawn->GetActorRotation().Yaw + CameraTuning.YawDegrees,
		0.0f);
	const FVector PlanarForward = ViewYaw.Vector().GetSafeNormal2D();
	const FVector PlanarRight = FRotationMatrix(ViewYaw).GetScaledAxis(EAxis::Y).GetSafeNormal2D();

	CameraPawn->SetKeyboardPanInput(FVector2D(1.0f, 0.0f));
	CameraPawn->Tick(0.1f);
	const FVector RightPanDisplacement = CameraPawn->GetActorLocation();
	TestTrue(
		TEXT("D pans toward the right edge of a yawed camera"),
		FVector::DotProduct(RightPanDisplacement, PlanarRight) > 0.0f);
	TestTrue(
		TEXT("D does not pan forward through a yawed camera"),
		FMath::IsNearlyZero(FVector::DotProduct(RightPanDisplacement, PlanarForward), 0.1f));

	CameraPawn->SetActorLocation(FVector::ZeroVector);
	CameraPawn->SetKeyboardPanInput(FVector2D(0.0f, 1.0f));
	CameraPawn->Tick(0.1f);
	const FVector ForwardPanDisplacement = CameraPawn->GetActorLocation();
	TestTrue(
		TEXT("W pans toward the top edge of a yawed camera"),
		FVector::DotProduct(ForwardPanDisplacement, PlanarForward) > 0.0f);
	TestTrue(
		TEXT("W does not pan sideways through a yawed camera"),
		FMath::IsNearlyZero(FVector::DotProduct(ForwardPanDisplacement, PlanarRight), 0.1f));

	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return !HasAnyErrors();
}

#endif
