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
	TeamId = NewTeamId;
	ProfileKind = NewProfileKind;
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
	Super::BeginPlay();
	if (URTSAIStrategySubsystem* Strategy = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<URTSAIStrategySubsystem>()
		: nullptr)
	{
		const FRTSMilestone3Configuration Configuration = FRTSMilestone3Configuration::Load();
		Strategy->StartFaction(TeamId, Configuration.GetProfile(ProfileKind));
	}
}
