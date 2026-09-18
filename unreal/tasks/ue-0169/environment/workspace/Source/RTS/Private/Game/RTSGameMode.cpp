// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/RTSGameMode.h"

#include "Camera/RTSCameraPawn.h"
#include "Game/RTSHUD.h"
#include "Game/RTSPlayerController.h"

ARTSGameMode::ARTSGameMode()
{
	DefaultPawnClass = ARTSCameraPawn::StaticClass();
	PlayerControllerClass = ARTSPlayerController::StaticClass();
	HUDClass = ARTSHUD::StaticClass();
}
