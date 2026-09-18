// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NightSkyBattleHudActor.generated.h"

class UNightSkyBattleWidget;
class UNightSkyModifierWidget;

UCLASS()
class NIGHTSKYENGINE_API ANightSkyBattleHudActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ANightSkyBattleHudActor();
virtual void BeginPlay() override;
UPROPERTY(BlueprintReadOnly)
UNightSkyModifierWidget* ModifierWidget;

	UPROPERTY(BlueprintReadWrite)
	UNightSkyBattleWidget* TopWidget;
	UPROPERTY(BlueprintReadWrite)
	UNightSkyBattleWidget* BottomWidget;
};
