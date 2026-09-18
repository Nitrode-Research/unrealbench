// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "RTSCameraPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;

/** Repository-readable camera composition tuned against the Milestone 1 sandbox. */
USTRUCT()
struct RTS_API FRTSCameraTuning
{
	GENERATED_BODY()

	float PitchDegrees = -42.0f;
	float YawDegrees = 40.0f;
	float InitialZoomDistance = 3500.0f;
	float MinimumZoomDistance = 3000.0f;
	float MaximumZoomDistance = 7500.0f;
	float PanHalfExtent = 4000.0f;
	float NearPanSpeed = 3000.0f;
	float FarPanSpeed = 6500.0f;
	float ZoomStep = 1200.0f;
	float ZoomInterpolationSpeed = 8.0f;

	static FRTSCameraTuning Load();
};

/** Native top-down view pawn. Slice 1 adds input and bounded movement. */
UCLASS()
class RTS_API ARTSCameraPawn final : public APawn
{
	GENERATED_BODY()

public:
	ARTSCameraPawn();
	virtual void Tick(float DeltaSeconds) override;

	void SetKeyboardPanInput(const FVector2D& NewPanInput);
	void SetEdgePanInput(const FVector2D& NewPanInput);
	void AddZoomInput(float ZoomSteps);
	float GetZoomDistance() const;
	FVector2D GetPlanarLocation() const;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	FRTSCameraTuning CameraTuning;
	FVector2D PlanarHalfExtent = FVector2D(4000.0f, 4000.0f);
	FVector2D KeyboardPanInput = FVector2D::ZeroVector;
	FVector2D EdgePanInput = FVector2D::ZeroVector;
	float TargetZoomDistance = 3500.0f;
};
