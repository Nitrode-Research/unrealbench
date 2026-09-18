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
	// Restore the published gameplay contract.
	Super::BeginPlay();
}

void AFacilityCoolantValve::OnFacilityStateChanged()
{
	// Restore the published gameplay contract.
}

void AFacilityCoolantValve::RefreshDisplayData()
{
	// Restore the published gameplay contract.
}

bool AFacilityCoolantValve::Use()
{
	// Restore the published gameplay contract.
	return {};
}

bool AFacilityCoolantValve::Open()
{
	// Restore the published gameplay contract.
	return {};
}

bool AFacilityCoolantValve::Close()
{
	// Restore the published gameplay contract.
	return {};
}

bool AFacilityCoolantValve::IsOpen() const
{
	// Restore the published gameplay contract.
	return {};
}

bool AFacilityCoolantValve::CanOpen() const
{
	// Restore the published gameplay contract.
	return {};
}

void AFacilityCoolantValve::ReconcileOpen(const bool bInstant)
{
	// Restore the published gameplay contract.
}

#undef LOCTEXT_NAMESPACE
