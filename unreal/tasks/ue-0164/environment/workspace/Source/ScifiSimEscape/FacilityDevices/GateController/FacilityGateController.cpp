#include "FacilityDevices/GateController/FacilityGateController.h"

#include "Components/StaticMeshComponent.h"
#include "Facility/FacilityRules.h"
#include "Facility/FacilityStateSubsystem.h"
#include "InteractionSystem/InteractableComponent.h"

#define LOCTEXT_NAMESPACE "FacilityGateController"

AFacilityGateController::AFacilityGateController()
{
	// Nothing to tick. The controller only changes when the facility state does.
	PrimaryActorTick.bCanEverTick = false;

	// The gate and its control run on DOORS. The level the handheld opens the gate at is the settings', 2 in the GDD.
	DefaultCircuit = EFacilityCircuit::EFC_Doors;

	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(Root);
}

void AFacilityGateController::RefreshDisplayData()
{
	// TODO: Restore security device behavior.
}

void AFacilityGateController::OnHacked(AActor* Hacker)
{
	// TODO: Restore security device behavior.
	Super::OnHacked(Hacker);
}

bool AFacilityGateController::Hack()
{
	// TODO: Restore security device behavior.
	return false;
}

bool AFacilityGateController::CanHack() const
{
	// TODO: Restore security device behavior.
	return false;
}

#undef LOCTEXT_NAMESPACE
