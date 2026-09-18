// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/RTSAIStrategyBootstrap.h"

#include "AI/RTSAIStrategySubsystem.h"

ARTSAIStrategyBootstrap::ARTSAIStrategyBootstrap()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ARTSAIStrategyBootstrap::Configure(
	const FGenericTeamId NewTeamId,
	const ERTSAIProfileKind NewProfileKind)
{
	// Restore this contractor-owned implementation.
}

FGenericTeamId ARTSAIStrategyBootstrap::GetTeamId() const
{
	return TeamId;
}

ERTSAIProfileKind ARTSAIStrategyBootstrap::GetProfileKind() const
{
	return ProfileKind;
}

void ARTSAIStrategyBootstrap::BeginPlay()
{
	// Restore this contractor-owned implementation.
	Super::BeginPlay();
}
