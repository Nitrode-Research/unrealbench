#include "FacilityDevices/FacilityDeviceBase.h"

#include "Facility/FacilityStateSubsystem.h"
#include "ScifiSimEscape.h"

#define LOCTEXT_NAMESPACE "FacilityDevice"

void AFacilityDeviceBase::BeginPlay()
{
	// Registration comes first, so the first display refresh in the base already knows the device.
	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		if (DeviceId.IsNone())
		{
			UE_LOG(LogScifiSimEscape, Warning,
				TEXT("%s has no DeviceId. It cannot register with the facility and will never be powered."),
				*GetName());
		}

		// Routing and, from the settings, the handheld level the id needs.
		Facility->RegisterDevice(this, DeviceId, DefaultCircuit, SharesDeviceId());

		if (DeviceId.IsNone() == false && NeedsCircuit() && GetCircuit() == EFacilityCircuit::EFC_None)
		{
			UE_LOG(LogScifiSimEscape, Warning,
				TEXT("%s (%s) is wired to no circuit and will never be powered."),
				*GetName(), *DeviceId.ToString());
		}
	}

	Super::BeginPlay();

	// Deliver the starting state once, after Blueprint BeginPlay, so a child never has to poll. Then
	// refresh again, in case what the child shows depends on what it set up in OnPowerChanged.
	ReconcilePower(true);
	RefreshDisplayData();
}

void AFacilityDeviceBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		Facility->UnregisterDevice(this, DeviceId);
	}

	Super::EndPlay(EndPlayReason);
}

EFacilityCircuit AFacilityDeviceBase::GetCircuit() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr ? Facility->GetDeviceCircuit(DeviceId) : EFacilityCircuit::EFC_None;
}

bool AFacilityDeviceBase::IsPowered() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->IsDevicePowered(DeviceId);
}

int32 AFacilityDeviceBase::GetHackLevel() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr ? Facility->GetHackLevel(DeviceId) : 0;
}

FText AFacilityDeviceBase::DescribeNoPower(const EFacilityCircuit Circuit)
{
	if (Circuit == EFacilityCircuit::EFC_None)
	{
		return LOCTEXT("NotWired", "Not wired");
	}

	return FText::Format(LOCTEXT("CircuitOff", "{0} is off"), UEnum::GetDisplayValueAsText(Circuit));
}

void AFacilityDeviceBase::PreFacilityStateChanged()
{
	// Power first, so OnFacilityStateChanged in the child sees the new answer.
	ReconcilePower(false);
}

void AFacilityDeviceBase::ReconcilePower(const bool bForceNotify)
{
	const bool bPowered = IsPowered();
	if (bForceNotify == false && bPowered == bLastReportedPowered)
	{
		return;
	}

	// Recorded before the hooks run. A child that changes the facility from inside OnPowerChanged
	// re-enters here through a nested OnStateChanged and must compare against the value it was
	// just given, not a stale one.
	bLastReportedPowered = bPowered;

	OnPowerChanged(bPowered);
	ReceivePowerChanged(bPowered);
}

#undef LOCTEXT_NAMESPACE
