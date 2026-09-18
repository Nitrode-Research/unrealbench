// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTSWorldOverlayShowcaseActor.generated.h"

class USceneComponent;
class ARTSCameraPawn;

/** Authored capture anchor; it submits the deterministic Slice 6 showcase only when requested. */
UCLASS()
class RTS_API ARTSWorldOverlayShowcaseActor final : public AActor
{
	GENERATED_BODY()

public:
	ARTSWorldOverlayShowcaseActor();
	virtual void BeginPlay() override;
	void ActivateShowcase();

private:
	void FindPackagedCaptureCamera();
	void CapturePackagedNormal();
	void PreparePackagedExtreme();
	void CapturePackagedExtreme();
	void FinishPackagedCapture();
	void PinPackagedCaptureCamera();
	void RequestPackagedScreenshot(const TCHAR* Filename) const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	FTimerHandle PackagedCaptureTimer;
	TWeakObjectPtr<ARTSCameraPawn> PackagedCaptureCamera;
	FVector PackagedCaptureCameraLocation = FVector::ZeroVector;
	bool bActivated = false;
	bool bPackagedCaptureCameraCentered = false;
};
