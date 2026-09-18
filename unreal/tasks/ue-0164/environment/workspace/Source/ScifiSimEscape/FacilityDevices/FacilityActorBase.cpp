#include "FacilityDevices/FacilityActorBase.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Facility/FacilityStateSubsystem.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "InteractionSystem/InteractableComponent.h"

AFacilityActorBase::AFacilityActorBase()
{
	// Nothing in the base needs a tick. A child that moves turns it on.
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Interactable = CreateDefaultSubobject<UInteractableComponent>(TEXT("Interactable"));
}

void AFacilityActorBase::BeginPlay()
{
	// Restore the published gameplay contract.
	Super::BeginPlay();
}

void AFacilityActorBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Restore the published gameplay contract.
	Super::EndPlay(EndPlayReason);
}

UFacilityStateSubsystem* AFacilityActorBase::FindFacility() const
{
	// Restore the published gameplay contract.
	return {};
}

void AFacilityActorBase::HandleFacilityStateChanged()
{
	// Restore the published gameplay contract.
}

void AFacilityActorBase::HandleFocusChanged(const bool bFocused)
{
	// Restore the published gameplay contract.
}

void AFacilityActorBase::HandleHacked(AActor* Hacker)
{
	// Restore the published gameplay contract.
}

bool AFacilityActorBase::IsPlayerPawn(const AActor* Actor)
{
	// Restore the published gameplay contract.
	return {};
}
