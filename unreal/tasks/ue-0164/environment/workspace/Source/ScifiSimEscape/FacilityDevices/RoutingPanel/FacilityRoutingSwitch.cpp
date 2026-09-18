#include "FacilityDevices/RoutingPanel/FacilityRoutingSwitch.h"

#include "Components/StaticMeshComponent.h"
#include "Facility/FacilityStateSubsystem.h"
#include "InteractionSystem/InteractableComponent.h"
#include "ScifiSimEscape.h"

#define LOCTEXT_NAMESPACE "FacilityRoutingSwitch"

AFacilityRoutingSwitch::AFacilityRoutingSwitch()
{
	// Nothing to tick. The switch only changes when the facility state does.
	PrimaryActorTick.bCanEverTick = false;

	// The panel is control electronics like the camera and the console, so it draws from SECURITY unless placed otherwise.
	DefaultCircuit = EFacilityCircuit::EFC_Security;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
}

void AFacilityRoutingSwitch::BeginPlay()
{
	// TODO: Restore task behavior.
	Super::BeginPlay();
}

void AFacilityRoutingSwitch::OnFacilityStateChanged()
{
	// TODO: Restore task behavior.
	Super::OnFacilityStateChanged();
}

bool AFacilityRoutingSwitch::IsOn() const
{
	// TODO: Restore task behavior.
	return false;
}

void AFacilityRoutingSwitch::RefreshDisplayData()
{
	// TODO: Restore task behavior.
}

void AFacilityRoutingSwitch::OnHacked(AActor* Hacker)
{
	// TODO: Restore task behavior.
	Super::OnHacked(Hacker);
}

bool AFacilityRoutingSwitch::Hack()
{
	// TODO: Restore task behavior.
	return false;
}

bool AFacilityRoutingSwitch::CanHack() const
{
	// TODO: Restore task behavior.
	return false;
}

FText AFacilityRoutingSwitch::GetDeviceLabel() const
{
	// TODO: Restore task behavior.
	return FText::GetEmpty();
}

void AFacilityRoutingSwitch::ReconcileSwitch(const bool bInstant)
{
	// TODO: Restore task behavior.
}

#undef LOCTEXT_NAMESPACE
