#include "FacilityDevices/AlarmConsole/FacilityAlarmConsole.h"

#include "Components/StaticMeshComponent.h"
#include "Facility/FacilityStateSubsystem.h"
#include "InteractionSystem/InteractableComponent.h"

#define LOCTEXT_NAMESPACE "FacilityAlarmConsole"

AFacilityAlarmConsole::AFacilityAlarmConsole()
{
	// Nothing to tick. The console only changes when the facility state does.
	PrimaryActorTick.bCanEverTick = false;

	// SECURITY powers the camera, the console and the server rack.
	DefaultCircuit = EFacilityCircuit::EFC_Security;

	Console = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Console"));
	Console->SetupAttachment(Root);
}

bool AFacilityAlarmConsole::Silence()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->SilenceAlarm(DeviceId);
}

bool AFacilityAlarmConsole::CanSilence() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanSilenceAlarm(DeviceId);
}

void AFacilityAlarmConsole::RefreshDisplayData()
{
	if (CanSilence())
	{
		Interactable->SetDisplayData(LOCTEXT("Silence", "Silence alarm"), true);
		return;
	}

	// The rules refuse for two reasons: a dead console, or nothing to silence.
	Interactable->SetDisplayData(IsPowered() ? LOCTEXT("Quiet", "Alarm is quiet") : DescribeNoPower(GetCircuit()), false);
}

#undef LOCTEXT_NAMESPACE
