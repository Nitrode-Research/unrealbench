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
	Super::BeginPlay();

	// Start as the facility says, without a blast.
	ReconcileOverloaded(true);
}

void AFacilityServerRack::OnFacilityStateChanged()
{
	Super::OnFacilityStateChanged();

	// A reset starts a new attempt, so the rack is put in its state rather than reacting to it.
	const UFacilityStateSubsystem* Facility = FindFacility();
	ReconcileOverloaded(Facility != nullptr && Facility->IsResetting());
}

void AFacilityServerRack::RefreshDisplayData()
{
	// The interact line.
	if (IsOverloaded())
	{
		Interactable->SetDisplayData(LOCTEXT("Burning", "Server rack burning"), false);
	}
	else if (CanOverload())
	{
		// The fire brings the alarm up and floods Plant, so the prompt says so before the press.
		Interactable->SetDisplayData(LOCTEXT("Overload", "Rig server rack to overload (fire, alarm)"), true);
	}
	else if (IsPowered() == false)
	{
		Interactable->SetDisplayData(DescribeNoPower(GetCircuit()), false);
	}
	else
	{
		Interactable->SetDisplayData(LOCTEXT("NeedsPryBar", "Server rack (needs the pry bar to rig)"), false);
	}

	// The hack line. The GDD gives the rack a level but no effect for the hack, so the handheld has no business here yet.
	Interactable->SetHackDisplayData(FText::GetEmpty(), false);
}

bool AFacilityServerRack::Use()
{
	return Overload();
}

bool AFacilityServerRack::Overload()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->OverloadServerRack(DeviceId);
}

bool AFacilityServerRack::IsOverloaded() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->IsServerRackOverloaded();
}

bool AFacilityServerRack::CanOverload() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanOverloadServerRack(DeviceId);
}

void AFacilityServerRack::ReconcileOverloaded(const bool bInstant)
{
	const bool bOverloaded = IsOverloaded();
	if (bInstant == false && bOverloaded == bLastReportedOverloaded)
	{
		return;
	}

	// Recorded before the hooks run, so a change made in response compares against the new value.
	bLastReportedOverloaded = bOverloaded;

	OnOverloadedChanged(bOverloaded, bInstant);
	ReceiveOverloadedChanged(bOverloaded, bInstant);
}

#undef LOCTEXT_NAMESPACE
