// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RTSGameMode.generated.h"

/** Milestone-owned composition root for native RTS gameplay classes. */
UCLASS()
class RTS_API ARTSGameMode final : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARTSGameMode();
};
