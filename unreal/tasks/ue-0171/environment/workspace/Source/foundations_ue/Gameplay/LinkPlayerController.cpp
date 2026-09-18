#include "Gameplay/LinkPlayerController.h"
#include "Gameplay/LinkCharacter.h"
#include "Gameplay/LinkChain.h"
#include "Gameplay/LinkInteractionActor.h"
#include "Gameplay/LinkSession.h"
#include "Gameplay/LinkAudio.h"
#include "Gameplay/LinkSceneLayout.h"
#include "UI/LinkHUD.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "TimerManager.h"

ALinkPlayerController::ALinkPlayerController()
{
    bShowMouseCursor = true;
    bAutoManageActiveCameraTarget = false;
}

void ALinkPlayerController::BeginPlay()
{
    // Restore the documented A gameplay contract.
    Super::BeginPlay();
}

void ALinkPlayerController::PlayerTick(float DeltaTime)
{
    // Restore the documented A gameplay contract.
    Super::PlayerTick(DeltaTime);
}

void ALinkPlayerController::UpdateCameraManager(float DeltaSeconds)
{
    // Restore the documented A gameplay contract.
    
}
void ALinkPlayerController::SetCameraWeight(int32 Percent) {
    // Restore the documented A gameplay contract.
    
}
void ALinkPlayerController::PreviewInteractionCamera(FName Id)
{
    // Restore the documented A gameplay contract.
    
}

void ALinkPlayerController::UpdateSharedView(float DeltaTime, bool bSnap)
{
    // Restore the documented A gameplay contract.
    
}

void ALinkPlayerController::SetupInputComponent()
{
    // Restore the documented A gameplay contract.
    Super::SetupInputComponent();
}

ALinkCharacter* ALinkPlayerController::GetCharacterAt(int32 Index) const
{
    // Restore the documented A gameplay contract.
    return nullptr;
}

bool ALinkPlayerController::CommandMove(int32 Index, const FVector& Target)
{
    // Restore the documented A gameplay contract.
    return {};
}

bool ALinkPlayerController::NavigateCharacter(int32 Index, const FVector& Target)
{
    // Restore the documented A gameplay contract.
    return {};
}

void ALinkPlayerController::ApplyCommandFrame(const FLinkCommandFrame& Frame, const FVector& Target)
{
    // Restore the documented A gameplay contract.
    
}

void ALinkPlayerController::UpdateCommands()
{
    // Restore the documented A gameplay contract.
    
}

bool ALinkPlayerController::FindRoute(ALinkCharacter* From, const FVector& Target, TArray<FVector>& OutCorners) const
{
    // Restore the documented A gameplay contract.
    return {};
}

void ALinkPlayerController::UpdateMovement()
{
    // Restore the documented A gameplay contract.
    
}

ALinkInteractionActor* ALinkPlayerController::GetInteractionFor(int32 PlayerIndex) const
{
    return Interactions.IsValidIndex(PlayerIndex)?Interactions[PlayerIndex]:nullptr;
}
void ALinkPlayerController::LeaveInteraction(int32 PlayerIndex)
{
    if (auto* Existing=GetInteractionFor(PlayerIndex)) { Existing->Leave(PlayerIndex); Interactions[PlayerIndex]=nullptr; }
}
bool ALinkPlayerController::InteractWith(ALinkInteractionActor* Target,int32 PlayerIndex)
{
    auto* CrewMember=GetCharacterAt(PlayerIndex);
    if(bTransitioning){return false;}
    if (!Target || Target->GetWorld()!=GetWorld() || !CrewMember) { return false; }
    if (GetInteractionFor(PlayerIndex)==Target && Target->IsReady(PlayerIndex))
    {
        OpenInteraction(Target); Commands.Reset(); return true;
    }
    if (GetInteractionFor(PlayerIndex)!=Target)
    {
        LeaveInteraction(PlayerIndex);
        if (!Target->Focus(PlayerIndex,CrewMember)) { return false; }
        Interactions[PlayerIndex]=Target;
        CommandFeedbackTimes[PlayerIndex]=GetWorld()->GetTimeSeconds();CommandFeedbackLocations[PlayerIndex]=Target->WaypointFor(PlayerIndex);
        if(auto* Audio=GetWorld()->GetSubsystem<ULinkAudio>()){Audio->Command();}
        CrewMember->SetMotionMode(ELinkMotionMode::Interact);
        auto* Other=GetCharacterAt(1-PlayerIndex);
        if (Other && Other->GetMotionMode()==ELinkMotionMode::Navigate) { Other->SetMotionMode(ELinkMotionMode::Follow); }
    }
    return true;
}

void ALinkPlayerController::CommandLeft() {
    // Restore the documented A gameplay contract.
    
}
void ALinkPlayerController::CommandRight() {
    // Restore the documented A gameplay contract.
    
}
