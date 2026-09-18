#include "InteractionSystem/InteractableComponent.h"

#include "Components/MeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

// Sets default values for this component's properties
UInteractableComponent::UInteractableComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	bIsFocused = false;
}

// Called when the game starts
void UInteractableComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UInteractableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetHighlightOverlay(nullptr);
	Super::EndPlay(EndPlayReason);
}

void UInteractableComponent::OnFocused()
{
	bIsFocused = true;
	OnFocusedChanged.Broadcast(bIsFocused);
}

void UInteractableComponent::OnUnFocused()
{
	bIsFocused = false;
	OnFocusedChanged.Broadcast(bIsFocused);
}

void UInteractableComponent::Interact(AActor* InteractingActor)
{
	OnInteracted.Broadcast(InteractingActor);
}

void UInteractableComponent::Hack(AActor* HackingActor)
{
	OnHacked.Broadcast(HackingActor);
}

void UInteractableComponent::SetDisplayData(const FText& InDisplayText, const bool bInCanInteract)
{
	if (DisplayData.bCanInteract == bInCanInteract && DisplayData.InteractionDisplayText.EqualTo(InDisplayText))
	{
		return;
	}

	DisplayData.InteractionDisplayText = InDisplayText;
	DisplayData.bCanInteract = bInCanInteract;
	OnDisplayDataChanged.Broadcast();
}

void UInteractableComponent::SetHackDisplayData(const FText& InHackDisplayText, const bool bInCanHack)
{
	if (DisplayData.bCanHack == bInCanHack && DisplayData.HackDisplayText.EqualTo(InHackDisplayText))
	{
		return;
	}

	DisplayData.HackDisplayText = InHackDisplayText;
	DisplayData.bCanHack = bInCanHack;
	OnDisplayDataChanged.Broadcast();
}

void UInteractableComponent::SetHighlightOverlay(UMaterialInterface* Overlay)
{
	if (bEnableHighlight == false)
	{
		Overlay = nullptr;
	}

	if (Overlay == HighlightOverlay)
	{
		return;
	}

	// Coming on: take the overlay slot of every mesh on the owner, remembering what each one drew before.
	if (HighlightOverlay == nullptr)
	{
		static const FName NoHighlightTag(TEXT("NoHighlight"));

		const TInlineComponentArray<UMeshComponent*> Meshes(GetOwner());
		for (UMeshComponent* Mesh : Meshes)
		{
			if (Mesh->ComponentHasTag(NoHighlightTag))
			{
				continue;
			}

			FInteractableHighlightedMesh& HighlightedMesh = HighlightedMeshes.AddDefaulted_GetRef();
			HighlightedMesh.Mesh = Mesh;
			HighlightedMesh.OverlayBeforeHighlight = Mesh->OverlayMaterial;
		}
	}

	HighlightOverlay = Overlay;

	for (const FInteractableHighlightedMesh& HighlightedMesh : HighlightedMeshes)
	{
		if (UMeshComponent* Mesh = HighlightedMesh.Mesh.Get())
		{
			Mesh->SetOverlayMaterial(Overlay != nullptr ? Overlay : HighlightedMesh.OverlayBeforeHighlight.Get());
		}
	}

	// Coming off: every mesh has its own overlay back.
	if (Overlay == nullptr)
	{
		HighlightedMeshes.Reset();
	}
}

