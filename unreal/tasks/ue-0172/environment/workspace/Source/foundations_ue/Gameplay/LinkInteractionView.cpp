#include "Gameplay/LinkPlayerController.h"
#include "Gameplay/LinkInteractionActor.h"
#include "Gameplay/LinkSession.h"
#include "Gameplay/LinkAudio.h"
#include "UI/LinkHUD.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"

namespace
{
FString PlainText(const FString& Text)
{
    // Restore the documented B gameplay contract.
    return {};
}
}
void ALinkPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    // Restore the documented B gameplay contract.
    Super::EndPlay(Reason);
}
void ALinkPlayerController::CloseInteractionView()
{
    // Restore the documented B gameplay contract.
    
}
void ALinkPlayerController::TogglePauseOrClose()
{
    // Restore the documented B gameplay contract.
    
}
FString ALinkPlayerController::InventoryLabel(int32 PlayerIndex) const
{
    // Restore the documented B gameplay contract.
    return {};
}
void ALinkPlayerController::ToggleAudio(){
    // Restore the documented B gameplay contract.
    
}
FString ALinkPlayerController::AudioLabel() const
{
    // Restore the documented B gameplay contract.
    return {};
}
FString ALinkPlayerController::HoverLabel() const
{
    // Restore the documented B gameplay contract.
    return {};
}
FString ALinkPlayerController::ActionLabel(int32 EntryId) const
{
    // Restore the documented B gameplay contract.
    return {};
}
void ALinkPlayerController::OpenInteraction(ALinkInteractionActor* Target)
{
    // Restore the documented B gameplay contract.
    
}
bool ALinkPlayerController::ChooseAction(int32 EntryId)
{
    // Restore the documented B gameplay contract.
    return {};
}
void ALinkPlayerController::PumpInteraction()
{
    // Restore the documented B gameplay contract.
    
}
void ALinkPlayerController::UpdatePartnerArrival()
{
    // Restore the documented B gameplay contract.
    
}
