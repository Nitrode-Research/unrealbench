// Fill out your copyright notice in the Description page of Project Settings.


#include "FighterReplayRunner.h"

#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FighterReplayRunner)

// Sets default values
AFighterReplayRunner::AFighterReplayRunner()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AFighterReplayRunner::BeginPlay()
{
	Super::BeginPlay();
	GameInstance = Cast<UNightSkyGameInstance>(GetGameInstance());
}

void AFighterReplayRunner::Update(float DeltaTime)
{
    if (!GameState || !GameInstance || GameState->bPauseGame || GameInstance->bReplayPaused)
    {
        return;
    }
    ElapsedTime += DeltaTime;
    while (ElapsedTime >= OneFrame)
	{
		ElapsedTime -= OneFrame;
        if (!GameInstance->AdvanceReplay(GameState))
        {
            ElapsedTime = 0;
            break;
        }
    }
}
