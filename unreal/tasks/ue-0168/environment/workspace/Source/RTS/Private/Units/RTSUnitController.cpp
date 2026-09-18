// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/RTSUnitController.h"

#include "Navigation/CrowdFollowingComponent.h"
#include "Orders/RTSUnitOrderComponent.h"

ARTSUnitController::ARTSUnitController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
{
}

void ARTSUnitController::OnMoveCompleted(
	const FAIRequestID RequestId,
	const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestId, Result);
	if (URTSUnitOrderComponent* OrderComponent = GetPawn() != nullptr
		? GetPawn()->FindComponentByClass<URTSUnitOrderComponent>()
		: nullptr)
	{
		OrderComponent->HandleMoveCompleted(RequestId, Result);
	}
}
