// Fill out your copyright notice in the Description page of Project Settings.


#include "NightSkyBattleHudActor.h"
#include "NightSkyModifierWidget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NightSkyBattleHudActor)

// Sets default values
ANightSkyBattleHudActor::ANightSkyBattleHudActor()
{
	PrimaryActorTick.bCanEverTick = true;
}
void ANightSkyBattleHudActor::BeginPlay()
{
    Super::BeginPlay();
    if (GetWorld()->GetNetMode() != NM_DedicatedServer)
    {
        ModifierWidget = CreateWidget<UNightSkyModifierWidget>(
            GetWorld(), UNightSkyModifierWidget::StaticClass());
        if (ModifierWidget)
        {
            ModifierWidget->AddToViewport(20);
            ModifierWidget->SetPositionInViewport(FVector2D(24, 160));
        }
    }
}
