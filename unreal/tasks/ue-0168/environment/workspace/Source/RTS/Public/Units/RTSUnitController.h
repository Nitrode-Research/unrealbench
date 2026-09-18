// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AIController.h"
#include "RTSUnitController.generated.h"

/** Navigation adapter using Unreal's Detour crowd-following implementation. */
UCLASS()
class RTS_API ARTSUnitController final : public AAIController
{
	GENERATED_BODY()

public:
	ARTSUnitController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void OnMoveCompleted(FAIRequestID RequestId, const FPathFollowingResult& Result) override;
};
