#include "FacilityDevices/Lift/FacilityLiftCallPanel.h"

#include "Components/StaticMeshComponent.h"
#include "Facility/FacilityStateSubsystem.h"
#include "FacilityDevices/FacilityDeviceBase.h"
#include "InteractionSystem/InteractableComponent.h"

#define LOCTEXT_NAMESPACE "FacilityLiftCallPanel"

AFacilityLiftCallPanel::AFacilityLiftCallPanel()
{
	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(Root);
}

bool AFacilityLiftCallPanel::Call()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CallLift(LiftId, Stop);
}

bool AFacilityLiftCallPanel::CanCall() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanUseLift(LiftId) && Facility->GetLiftStop(LiftId) != Stop;
}

bool AFacilityLiftCallPanel::IsCarHere() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->GetLiftStop(LiftId) == Stop;
}

bool AFacilityLiftCallPanel::Hack()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->HackLift(LiftId);
}

bool AFacilityLiftCallPanel::CanHack() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanHackLift(LiftId);
}

void AFacilityLiftCallPanel::OnHacked(AActor* Hacker)
{
	Super::OnHacked(Hacker);
	Hack();
}

void AFacilityLiftCallPanel::RefreshDisplayData()
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	const EFacilityCircuit Circuit = Facility != nullptr ? Facility->GetDeviceCircuit(LiftId) : EFacilityCircuit::EFC_None;

	// The interact line: the lift's reason when it will not answer, otherwise the call.
	switch (Facility != nullptr ? Facility->GetLiftObstacle(LiftId) : ELiftObstacle::NoPower)
	{
	case ELiftObstacle::Hacked:
		Interactable->SetDisplayData(LOCTEXT("Hacked", "Stopped by the handheld"), false);
		break;

	case ELiftObstacle::NoPower:
		Interactable->SetDisplayData(AFacilityDeviceBase::DescribeNoPower(Circuit), false);
		break;

	case ELiftObstacle::Lockdown:
		Interactable->SetDisplayData(LOCTEXT("Lockdown", "Lockdown"), false);
		break;

	default:
		if (IsCarHere())
		{
			Interactable->SetDisplayData(LOCTEXT("LiftHere", "Lift is here"), false);
		}
		else
		{
			Interactable->SetDisplayData(LOCTEXT("CallLift", "Call lift"), true);
		}
		break;
	}

	// The hack line, the same as on the car: the handheld stops the lift from either floor.
	const int32 HackLevel = Facility != nullptr ? Facility->GetHackLevel(LiftId) : 0;
	if (HackLevel <= 0)
	{
		Interactable->SetHackDisplayData(FText::GetEmpty(), false);
	}
	else if (Facility->CanHackLift(LiftId))
	{
		const bool bSucceeds = Facility->WouldHackSucceed(HackLevel);
		Interactable->SetHackDisplayData(
			bSucceeds ? LOCTEXT("Hack", "Hack lift (stops it)") : LOCTEXT("HackFails", "Hack lift (will fail, loud)"), true);
	}
	else if (Facility->IsDevicePowered(LiftId) == false)
	{
		Interactable->SetHackDisplayData(AFacilityDeviceBase::DescribeNoPower(Circuit), false);
	}
	else
	{
		Interactable->SetHackDisplayData(FText::GetEmpty(), false);
	}
}

#undef LOCTEXT_NAMESPACE
