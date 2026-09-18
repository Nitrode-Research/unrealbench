#include "FacilityDevices/CoolantValve/FacilityCoolantValve.h"

#include "Components/StaticMeshComponent.h"
#include "Facility/FacilityStateSubsystem.h"
#include "InteractionSystem/InteractableComponent.h"

#define LOCTEXT_NAMESPACE "FacilityCoolantValve"

AFacilityCoolantValve::AFacilityCoolantValve()
{
	// Nothing to tick. The valve only changes when the facility state does.
	PrimaryActorTick.bCanEverTick = false;

	Wheel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Wheel"));
	Wheel->SetupAttachment(Root);
}

void AFacilityCoolantValve::BeginPlay()
{
	Super::BeginPlay();

	// Start as the facility says, with the wheel already where it belongs.
	ReconcileOpen(true);
}

void AFacilityCoolantValve::OnFacilityStateChanged()
{
	Super::OnFacilityStateChanged();

	// A reset starts a new attempt, so the valve is put in its state rather than turned to it.
	const UFacilityStateSubsystem* Facility = FindFacility();
	ReconcileOpen(Facility != nullptr && Facility->IsResetting());
}

void AFacilityCoolantValve::RefreshDisplayData()
{
	if (IsOpen())
	{
		Interactable->SetDisplayData(LOCTEXT("Close", "Close coolant valve"), true);
		return;
	}

	// Opening floods Plant, so the prompt says so before the press. Without the pry bar it stays seized.
	if (CanOpen())
	{
		Interactable->SetDisplayData(LOCTEXT("Open", "Force coolant valve open (floods Plant)"), true);
		return;
	}

	Interactable->SetDisplayData(LOCTEXT("Seized", "Valve seized (needs the pry bar)"), false);
}

bool AFacilityCoolantValve::Use()
{
	// The one verb does the sensible thing: open a closed valve, close an open one.
	return IsOpen() ? Close() : Open();
}

bool AFacilityCoolantValve::Open()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->OpenCoolantValve();
}

bool AFacilityCoolantValve::Close()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CloseCoolantValve();
}

bool AFacilityCoolantValve::IsOpen() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->IsCoolantValveOpen();
}

bool AFacilityCoolantValve::CanOpen() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanOpenCoolantValve();
}

void AFacilityCoolantValve::ReconcileOpen(const bool bInstant)
{
	const bool bOpen = IsOpen();
	if (bInstant == false && bOpen == bLastReportedOpen)
	{
		return;
	}

	// Recorded before the hooks run, so a change made in response compares against the new value.
	bLastReportedOpen = bOpen;

	OnOpenChanged(bOpen, bInstant);
	ReceiveOpenChanged(bOpen, bInstant);
}

#undef LOCTEXT_NAMESPACE
