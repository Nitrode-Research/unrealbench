#include "FacilityDevices/ServerRack/FacilityServerRack.h"

#include "Components/StaticMeshComponent.h"
#include "Facility/FacilityStateSubsystem.h"
#include "InteractionSystem/InteractableComponent.h"

#define LOCTEXT_NAMESPACE "FacilityServerRack"

AFacilityServerRack::AFacilityServerRack()
{
	// Nothing to tick. The rack only changes when the facility state does.
	PrimaryActorTick.bCanEverTick = false;

	// SECURITY powers the camera, the console and the server rack.
	DefaultCircuit = EFacilityCircuit::EFC_Security;

	Cabinet = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cabinet"));
	Cabinet->SetupAttachment(Root);
}

void AFacilityServerRack::BeginPlay()
{
	// Restore the published gameplay contract.
	Super::BeginPlay();
}

void AFacilityServerRack::OnFacilityStateChanged()
{
	// Restore the published gameplay contract.
}

void AFacilityServerRack::RefreshDisplayData()
{
	// Restore the published gameplay contract.
}

bool AFacilityServerRack::Use()
{
	// Restore the published gameplay contract.
	return {};
}

bool AFacilityServerRack::Overload()
{
	// Restore the published gameplay contract.
	return {};
}

bool AFacilityServerRack::IsOverloaded() const
{
	// Restore the published gameplay contract.
	return {};
}

bool AFacilityServerRack::CanOverload() const
{
	// Restore the published gameplay contract.
	return {};
}

void AFacilityServerRack::ReconcileOverloaded(const bool bInstant)
{
	// Restore the published gameplay contract.
}

#undef LOCTEXT_NAMESPACE
