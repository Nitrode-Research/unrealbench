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
	// TODO: Restore security device behavior.
	return false;
}

bool AFacilityAlarmConsole::CanSilence() const
{
	// TODO: Restore security device behavior.
	return false;
}

void AFacilityAlarmConsole::RefreshDisplayData()
{
	// TODO: Restore security device behavior.
}

#undef LOCTEXT_NAMESPACE
