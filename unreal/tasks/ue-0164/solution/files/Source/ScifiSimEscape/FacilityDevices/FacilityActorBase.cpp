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
	Super::BeginPlay();

	Interactable->OnFocusedChanged.AddDynamic(this, &AFacilityActorBase::HandleFocusChanged);
	Interactable->OnHacked.AddDynamic(this, &AFacilityActorBase::HandleHacked);

	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		Facility->OnStateChanged.AddDynamic(this, &AFacilityActorBase::HandleFacilityStateChanged);
	}

	RefreshDisplayData();
}

void AFacilityActorBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		Facility->OnStateChanged.RemoveDynamic(this, &AFacilityActorBase::HandleFacilityStateChanged);
	}

	Interactable->OnHacked.RemoveDynamic(this, &AFacilityActorBase::HandleHacked);
	Interactable->OnFocusedChanged.RemoveDynamic(this, &AFacilityActorBase::HandleFocusChanged);

	Super::EndPlay(EndPlayReason);
}

UFacilityStateSubsystem* AFacilityActorBase::FindFacility() const
{
	return UWorld::GetSubsystem<UFacilityStateSubsystem>(GetWorld());
}

void AFacilityActorBase::HandleFacilityStateChanged()
{
	PreFacilityStateChanged();

	OnFacilityStateChanged();
	ReceiveFacilityStateChanged();

	RefreshDisplayData();
}

void AFacilityActorBase::HandleFocusChanged(const bool bFocused)
{
	if (bFocused)
	{
		RefreshDisplayData();
	}

	OnFocusChanged(bFocused);
	ReceiveFocusChanged(bFocused);
}

void AFacilityActorBase::HandleHacked(AActor* Hacker)
{
	OnHacked(Hacker);
	ReceiveHacked(Hacker);
}

bool AFacilityActorBase::IsPlayerPawn(const AActor* Actor)
{
	const APawn* Pawn = Cast<const APawn>(Actor);
	const AController* Controller = Pawn != nullptr ? Pawn->GetController() : nullptr;
	return Controller != nullptr && Controller->IsPlayerController();
}
