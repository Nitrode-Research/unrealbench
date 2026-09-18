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
    Super::StartPlay();
    // Project fixtures have runtime navigation. Build after all actors register.
    GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
    {
        if (auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
        {
            Navigation->Build();
        }
    }));
}
void ALinkGameMode::InitGame(const FString& MapName,const FString& Options,FString& ErrorMessage)
{
    Super::InitGame(MapName,Options,ErrorMessage);
    // Restore shared facts before any interaction actor acquires its map.
    GetGameInstance()->GetSubsystem<ULinkSession>()->ApplyPendingFacts();
}
