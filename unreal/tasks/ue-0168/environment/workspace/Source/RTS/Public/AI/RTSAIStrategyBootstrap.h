// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Configuration/RTSMilestone3Configuration.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GenericTeamAgentInterface.h"
#include "RTSAIStrategyBootstrap.generated.h"

/** Map-owned opt-in that starts one typed AI faction without making map identity a string rule. */
UCLASS()
class RTS_API ARTSAIStrategyBootstrap final : public AActor
{
	GENERATED_BODY()

public:
	ARTSAIStrategyBootstrap();
	void Configure(FGenericTeamId NewTeamId, ERTSAIProfileKind NewProfileKind);
	FGenericTeamId GetTeamId() const;
	ERTSAIProfileKind GetProfileKind() const;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleInstanceOnly, Category = "RTS AI")
	FGenericTeamId TeamId = FGenericTeamId::NoTeam;

	UPROPERTY(VisibleInstanceOnly, Category = "RTS AI")
	ERTSAIProfileKind ProfileKind = ERTSAIProfileKind::Normal;
};
