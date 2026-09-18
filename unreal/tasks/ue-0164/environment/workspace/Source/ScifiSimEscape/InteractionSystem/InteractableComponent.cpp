#include "InteractionSystem/InteractableComponent.h"

UInteractableComponent::UInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bIsFocused = false;
}

void UInteractableComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UInteractableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UInteractableComponent::OnFocused()
{
}

void UInteractableComponent::OnUnFocused()
{
}

void UInteractableComponent::Interact(AActor* InteractingActor)
{
}

void UInteractableComponent::Hack(AActor* HackingActor)
{
}

void UInteractableComponent::SetDisplayData(const FText& InDisplayText, const bool bInCanInteract)
{
}

void UInteractableComponent::SetHackDisplayData(const FText& InHackDisplayText, const bool bInCanHack)
{
}

void UInteractableComponent::SetHighlightOverlay(UMaterialInterface* Overlay)
{
}
