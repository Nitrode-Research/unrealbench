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
	const UFacilityStateSubsystem* Facility = FindFacility();
	const FName GateId = Facility != nullptr ? FFacilityRules::FindDoorOfKind(Facility->GetState(), EDoorKind::Gate) : NAME_None;

	// The interact line: there is nothing to use by hand, so it says what the gate is doing.
	if (Facility == nullptr || GateId.IsNone())
	{
		Interactable->SetDisplayData(LOCTEXT("NoGate", "No gate"), false);
	}
	else if (Facility->IsDoorOpen(GateId))
	{
		Interactable->SetDisplayData(LOCTEXT("GateOpen", "Gate open"), false);
	}
	else
	{
		switch (Facility->GetDoorObstacle(GateId))
		{
		case EDoorObstacle::Sealed:
			Interactable->SetDisplayData(LOCTEXT("GateSealed", "Gate sealed"), false);
			break;

		case EDoorObstacle::NoPower:
			Interactable->SetDisplayData(DescribeNoPower(GetCircuit()), false);
			break;

		case EDoorObstacle::Locked:
			Interactable->SetDisplayData(LOCTEXT("GateLocked", "Gate locked"), false);
			break;

		default:
			Interactable->SetDisplayData(LOCTEXT("GateUnlocked", "Gate unlocked"), false);
			break;
		}
	}

	// The hack line.
	const int32 HackLevel = GetHackLevel();
	if (Facility == nullptr || HackLevel <= 0)
	{
		Interactable->SetHackDisplayData(FText::GetEmpty(), false);
	}
	else if (Facility->CanHackGateController(DeviceId))
	{
		// A hack beyond the handheld can still be attempted. It fails, and the noise raises the alarm.
		const bool bSucceeds = Facility->WouldHackSucceed(HackLevel);
		Interactable->SetHackDisplayData(
			bSucceeds ? LOCTEXT("Hack", "Hack gate controller (opens the gate)") : LOCTEXT("HackFails", "Hack gate controller (will fail, loud)"), true);
	}
	else if (IsPowered() == false)
	{
		Interactable->SetHackDisplayData(DescribeNoPower(GetCircuit()), false);
	}
	else
	{
		// The gate is open, sealed or unlocked: nothing for the handheld. The interact line says which.
		Interactable->SetHackDisplayData(FText::GetEmpty(), false);
	}
}

void AFacilityGateController::OnHacked(AActor* Hacker)
{
	Super::OnHacked(Hacker);
	Hack();
}

bool AFacilityGateController::Hack()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->HackGateController(DeviceId);
}

bool AFacilityGateController::CanHack() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanHackGateController(DeviceId);
}

#undef LOCTEXT_NAMESPACE
