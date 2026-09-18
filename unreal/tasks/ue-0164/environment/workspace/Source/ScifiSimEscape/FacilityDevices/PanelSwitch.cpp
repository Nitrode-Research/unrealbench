#include "FacilityDevices/PanelSwitch.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Facility/FacilityStateSubsystem.h"
#include "InteractionSystem/InteractableComponent.h"
#include "ScifiSimEscape.h"

#define LOCTEXT_NAMESPACE "PanelSwitch"

APanelSwitch::APanelSwitch()
{
	// Nothing to tick. The switch only changes when the facility state does.
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);

	Interactable = CreateDefaultSubobject<UInteractableComponent>(TEXT("Interactable"));
}

void APanelSwitch::BeginPlay()
{
	// TODO: Restore task behavior.
	Super::BeginPlay();
}

void APanelSwitch::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// TODO: Restore task behavior.
	Super::EndPlay(EndPlayReason);
}

void APanelSwitch::HandleInteracted(AActor* InteractingActor)
{
	// TODO: Restore task behavior.
}

void APanelSwitch::HandleFacilityStateChanged()
{
	// TODO: Restore task behavior.
}

void APanelSwitch::RefreshFromFacility(const bool bInstant)
{
	// TODO: Restore task behavior.
}

UFacilityStateSubsystem* APanelSwitch::FindFacility() const
{
	return UWorld::GetSubsystem<UFacilityStateSubsystem>(GetWorld());
}

#undef LOCTEXT_NAMESPACE
