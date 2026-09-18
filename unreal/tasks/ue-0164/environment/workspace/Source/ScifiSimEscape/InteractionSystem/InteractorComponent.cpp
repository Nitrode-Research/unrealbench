#include "InteractionSystem/InteractorComponent.h"

#include "InteractionSystem/InteractableComponent.h"

UInteractorComponent::UInteractorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UInteractorComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UInteractorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UInteractorComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UInteractorComponent::StartSphereDetection()
{
}

void UInteractorComponent::StopSphereDetection()
{
}

void UInteractorComponent::PerformSphereDetection()
{
}

UInteractableComponent* UInteractorComponent::SelectBestCandidate() const
{
	return nullptr;
}

AActor* UInteractorComponent::FindLookedAtActor() const
{
	return nullptr;
}

void UInteractorComponent::ClearDetectionState()
{
}

FVector UInteractorComponent::GetViewLocation() const
{
	return FVector::ZeroVector;
}

FVector UInteractorComponent::GetViewForward() const
{
	return FVector::ForwardVector;
}

void UInteractorComponent::ReconcileHighlightedInteractables(
	const TSet<TWeakObjectPtr<UInteractableComponent>>& CurrentInteractables)
{
}

void UInteractorComponent::CreateHighlightOverlays()
{
}

void UInteractorComponent::HighlightInteractable(UInteractableComponent* Interactable)
{
}

void UInteractorComponent::UnhighlightInteractable(UInteractableComponent* Interactable)
{
}

void UInteractorComponent::RefreshHighlight(UInteractableComponent* Interactable) const
{
}

void UInteractorComponent::HandleHighlightedDisplayDataChanged()
{
}

void UInteractorComponent::SetFocusedInteractable(UInteractableComponent* NewFocusedInteractable)
{
}

bool UInteractorComponent::Interact()
{
	return false;
}

bool UInteractorComponent::Hack()
{
	return false;
}

UInteractableComponent* UInteractorComponent::GetFocusedInteractable() const
{
	return nullptr;
}

void UInteractorComponent::HandleFocusedDisplayDataChanged()
{
}

bool UInteractorComponent::IsValidInteractableComponent(
	const TWeakObjectPtr<UInteractableComponent>& InteractableComponent) const
{
	return false;
}
