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
	// The job joins the panel before the base refreshes the prompt, so the first prompt already knows it.
	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		Facility->RegisterRoutingOption(DeviceId, Option);
	}

	Super::BeginPlay();

	// Start where the facility says the switch is, without animating there.
	ReconcileSwitch(true);
}

void AFacilityRoutingSwitch::OnFacilityStateChanged()
{
	Super::OnFacilityStateChanged();

	// A reset is a new attempt, so the lever snaps rather than moves.
	const UFacilityStateSubsystem* Facility = FindFacility();
	ReconcileSwitch(Facility != nullptr && Facility->IsResetting());
}

bool AFacilityRoutingSwitch::IsOn() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	if (Facility == nullptr)
	{
		return false;
	}

	if (Option.Action == ERoutingAction::CoolantPump)
	{
		return Facility->IsPlantFlooded();
	}

	return Option.DeviceId.IsNone() == false && Facility->GetDeviceCircuit(Option.DeviceId) == Option.Circuit;
}

void AFacilityRoutingSwitch::RefreshDisplayData()
{
	const UFacilityStateSubsystem* Facility = FindFacility();

	// The interact line: there is nothing to use by hand, so it says where the device is, or whether the floor is wet.
	if (Facility == nullptr)
	{
		Interactable->SetDisplayData(FText::GetEmpty(), false);
	}
	else if (Option.Action == ERoutingAction::CoolantPump)
	{
		Interactable->SetDisplayData(Facility->IsPlantFlooded() ? LOCTEXT("Flooded", "Plant flooded") : LOCTEXT("PumpIdle", "Coolant pump idle"), false);
	}
	else if (Option.DeviceId.IsNone() || Option.Circuit == EFacilityCircuit::EFC_None)
	{
		Interactable->SetDisplayData(LOCTEXT("NotSetUp", "Switch not set up"), false);
	}
	else if (Facility->GetState().DeviceCircuits.Contains(Option.DeviceId) == false)
	{
		Interactable->SetDisplayData(FText::Format(LOCTEXT("NoDevice", "No {0} registered"), GetDeviceLabel()), false);
	}
	else
	{
		const FText Circuit = UEnum::GetDisplayValueAsText(Facility->GetDeviceCircuit(Option.DeviceId));
		Interactable->SetDisplayData(Facility->IsDevicePatched(Option.DeviceId)
			? FText::Format(LOCTEXT("DeviceRerouted", "{0} on {1} (rerouted)"), GetDeviceLabel(), Circuit)
			: FText::Format(LOCTEXT("DeviceHome", "{0} on {1}"), GetDeviceLabel(), Circuit), false);
	}

	// The hack line.
	const int32 HackLevel = GetHackLevel();
	if (Facility == nullptr || HackLevel <= 0)
	{
		Interactable->SetHackDisplayData(FText::GetEmpty(), false);
	}
	else if (CanHack())
	{
		// A hack beyond the handheld can still be attempted. It fails, and the noise raises the alarm.
		if (Facility->WouldHackSucceed(HackLevel) == false)
		{
			Interactable->SetHackDisplayData(LOCTEXT("HackFails", "Hack panel (will fail, loud)"), true);
		}
		else if (Option.Action == ERoutingAction::CoolantPump)
		{
			Interactable->SetHackDisplayData(LOCTEXT("HackPump", "Hack panel (run coolant pump, floods Plant)"), true);
		}
		else if (Facility->IsDevicePatched(Option.DeviceId) && Facility->GetDeviceCircuit(Option.DeviceId) == Option.Circuit)
		{
			Interactable->SetHackDisplayData(FText::Format(LOCTEXT("HackHome", "Hack panel ({0} back to {1})"),
				GetDeviceLabel(), UEnum::GetDisplayValueAsText(Facility->GetHomeCircuit(Option.DeviceId))), true);
		}
		else
		{
			Interactable->SetHackDisplayData(FText::Format(LOCTEXT("HackRoute", "Hack panel ({0} to {1})"),
				GetDeviceLabel(), UEnum::GetDisplayValueAsText(Option.Circuit)), true);
		}
	}
	else if (IsPowered() == false)
	{
		Interactable->SetHackDisplayData(DescribeNoPower(GetCircuit()), false);
	}
	else
	{
		// The device sits on that circuit by placement, the floor is flooded already, or the job never
		// registered: nothing for the handheld. The interact line says which.
		Interactable->SetHackDisplayData(FText::GetEmpty(), false);
	}
}

void AFacilityRoutingSwitch::OnHacked(AActor* Hacker)
{
	Super::OnHacked(Hacker);
	Hack();
}

bool AFacilityRoutingSwitch::Hack()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	if (Facility == nullptr)
	{
		return false;
	}

	return Option.Action == ERoutingAction::CoolantPump
		? Facility->RunCoolantPump(DeviceId)
		: Facility->RouteDevice(DeviceId, Option.DeviceId, Option.Circuit);
}

bool AFacilityRoutingSwitch::CanHack() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	if (Facility == nullptr)
	{
		return false;
	}

	return Option.Action == ERoutingAction::CoolantPump
		? Facility->CanRunCoolantPump(DeviceId)
		: Facility->CanRouteDevice(DeviceId, Option.DeviceId, Option.Circuit);
}

FText AFacilityRoutingSwitch::GetDeviceLabel() const
{
	return DeviceLabel.IsEmpty() ? FText::FromName(Option.DeviceId) : DeviceLabel;
}

void AFacilityRoutingSwitch::ReconcileSwitch(const bool bInstant)
{
	const bool bOn = IsOn();
	if (bInstant == false && bOn == bLastReportedOn)
	{
		return;
	}

	// Recorded before Blueprint hears about it, so a change made in response compares against the new value.
	bLastReportedOn = bOn;
	ReceiveSwitchChanged(bOn, bInstant);
}

#undef LOCTEXT_NAMESPACE
