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
	Super::BeginPlay();

	if (Circuit == EFacilityCircuit::EFC_None)
	{
		UE_LOG(LogScifiSimEscape, Warning,
			TEXT("%s is not wired to a breaker. Set its Circuit, or pressing it flips nothing."), *GetName());
	}

	Interactable->OnInteracted.AddDynamic(this, &APanelSwitch::HandleInteracted);

	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		Facility->OnStateChanged.AddDynamic(this, &APanelSwitch::HandleFacilityStateChanged);
	}

	// Start where the facility says the breaker is, without animating there.
	RefreshFromFacility(true);
}

void APanelSwitch::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		Facility->OnStateChanged.RemoveDynamic(this, &APanelSwitch::HandleFacilityStateChanged);
	}

	Interactable->OnInteracted.RemoveDynamic(this, &APanelSwitch::HandleInteracted);

	Super::EndPlay(EndPlayReason);
}

void APanelSwitch::HandleInteracted(AActor* InteractingActor)
{
	// A press only asks for the flip. What the panel actually does, a trip included, comes back through
	// OnStateChanged, and that is where the switch moves.
	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		Facility->FlipBreaker(Circuit);
	}
}

void APanelSwitch::HandleFacilityStateChanged()
{
	RefreshFromFacility(false);
}

void APanelSwitch::RefreshFromFacility(const bool bInstant)
{
	// The lever shows the breaker's position, not whether the generator happens to carry the circuit.
	const UFacilityStateSubsystem* Facility = FindFacility();
	const bool bNowOn = Facility != nullptr && Facility->IsBreakerOn(Circuit);
	const bool bChanged = bNowOn != bIsOn;

	// Recorded before Blueprint hears about it, so a change made in response compares against the new
	// value when it comes back through OnStateChanged.
	bIsOn = bNowOn;

	if (Circuit == EFacilityCircuit::EFC_None)
	{
		Interactable->SetDisplayData(LOCTEXT("NotWired", "Not wired to a breaker"), false);
	}
	else if (Facility != nullptr && Facility->GetCircuitState(Circuit) == ECircuitState::Shorted)
	{
		// The flood shorted it. The breaker will not stay thrown, and the panel refuses the flip.
		Interactable->SetDisplayData(FText::Format(LOCTEXT("Shorted", "{0} shorted"), UEnum::GetDisplayValueAsText(Circuit)), false);
	}
	else
	{
		const FText CircuitName = UEnum::GetDisplayValueAsText(Circuit);
		const FText Action = bIsOn
			? FText::Format(LOCTEXT("TurnOff", "Turn {0} off"), CircuitName)
			: FText::Format(LOCTEXT("TurnOn", "Turn {0} on"), CircuitName);
		Interactable->SetDisplayData(Action, true);
	}

	if (bInstant || bChanged)
	{
		ReceiveSwitchChanged(bIsOn, bInstant);
	}
}

UFacilityStateSubsystem* APanelSwitch::FindFacility() const
{
	return UWorld::GetSubsystem<UFacilityStateSubsystem>(GetWorld());
}

#undef LOCTEXT_NAMESPACE
