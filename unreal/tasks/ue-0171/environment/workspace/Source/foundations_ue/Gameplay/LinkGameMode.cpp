#include "Gameplay/LinkGameMode.h"
#include "Gameplay/LinkPlayerController.h"
#include "Gameplay/LinkSession.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

ALinkGameMode::ALinkGameMode()
{
    PlayerControllerClass = ALinkPlayerController::StaticClass();
    DefaultPawnClass = nullptr;
}

void ALinkGameMode::StartPlay()
{
    // Restore the documented A gameplay contract.
    Super::StartPlay();
}
void ALinkGameMode::InitGame(const FString& MapName,const FString& Options,FString& ErrorMessage)
{
    Super::InitGame(MapName,Options,ErrorMessage);
    // Restore shared facts before any interaction actor acquires its map.
    GetGameInstance()->GetSubsystem<ULinkSession>()->ApplyPendingFacts();
}
