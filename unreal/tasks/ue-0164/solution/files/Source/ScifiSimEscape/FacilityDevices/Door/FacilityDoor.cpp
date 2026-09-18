#include "FacilityDevices/Door/FacilityDoor.h"

#include "Components/StaticMeshComponent.h"
#include "Facility/FacilityRules.h"
#include "Facility/FacilityStateSubsystem.h"
#include "FacilityDevices/Pickup/FacilityPickup.h"
#include "InteractionSystem/InteractableComponent.h"

#define LOCTEXT_NAMESPACE "FacilityDoor"

AFacilityDoor::AFacilityDoor()
{
	// Ticks only while the panel moves. SlideTo turns it on, settling turns it off.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// Most doors run on DOORS. A locker is set to None in its Blueprint and becomes mechanical.
	DefaultCircuit = EFacilityCircuit::EFC_Doors;

	DisplayName = LOCTEXT("Door", "door");

	Frame = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Frame"));
	Frame->SetupAttachment(Root);

	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(Root);
	Panel->Mobility = EComponentMobility::Movable;
}

void AFacilityDoor::BeginPlay()
{
	// Seed the door before the base begins play, so the first display refresh already sees it. The
	// base registers the circuit straight after, and that is when a lockdown door first follows it.
	// The lock comes from the settings inside RegisterDoor.
	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		Facility->RegisterDoor(DeviceId, Kind, bInitiallyOpen);
	}

	// The panel is authored closed. Read it before anything moves it.
	ClosedPose = Panel->GetRelativeLocation();

	Super::BeginPlay();

	// Read back rather than trusting bInitiallyOpen: the state keeps a door the player used across the
	// actor being destroyed and re-created, and a lockdown door may already have followed its circuit.
	SnapTo(IsOpen());
}

void AFacilityDoor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bMoving == false)
	{
		SetActorTickEnabled(false);
		return;
	}

	const FVector Target = GetPose(bTargetOpen);
	const FVector Next = FMath::VInterpConstantTo(Panel->GetRelativeLocation(), Target, DeltaSeconds, SlideSpeed);
	Panel->SetRelativeLocation(Next);

	if (Next.Equals(Target))
	{
		bMoving = false;
		SetActorTickEnabled(false);

		OnPanelSettled(bTargetOpen);
		ReceivePanelSettled(bTargetOpen);
	}
}

void AFacilityDoor::OnFacilityStateChanged()
{
	Super::OnFacilityStateChanged();

	// The state is the only thing that moves the panel, whether the player used the door or its
	// circuit sealed it. A reset starts a new attempt, so the panel is put in its pose rather than
	// slid there.
	const UFacilityStateSubsystem* Facility = FindFacility();
	if (Facility != nullptr && Facility->IsResetting())
	{
		SnapTo(IsOpen());
	}
	else
	{
		SlideTo(IsOpen());
	}
}

void AFacilityDoor::RefreshDisplayData()
{
	RefreshInteractDisplay();
	RefreshHackDisplay();
}

void AFacilityDoor::RefreshInteractDisplay()
{
	// Whatever holds the door, the pry bar gets past it if the player carries one. Forcing is loud, so the prompt says so.
	if (CanForce())
	{
		Interactable->SetDisplayData(FText::Format(LOCTEXT("Force", "Pry open {0} (loud)"), DisplayName), true);
		return;
	}

	switch (GetObstacle())
	{
	case EDoorObstacle::Forced:
		Interactable->SetDisplayData(LOCTEXT("Forced", "Pried open"), false);
		return;

	case EDoorObstacle::Sealed:
		Interactable->SetDisplayData(LOCTEXT("Sealed", "Sealed"), false);
		return;

	case EDoorObstacle::FailedOpen:
		Interactable->SetDisplayData(LOCTEXT("FailedOpen", "Failed open"), false);
		return;

	case EDoorObstacle::NoPower:
		Interactable->SetDisplayData(DescribeNoPower(GetCircuit()), false);
		return;

	case EDoorObstacle::Locked:
		Interactable->SetDisplayData(DescribeLock(), false);
		return;

	default:
		break;
	}

	if (IsOpen())
	{
		Interactable->SetDisplayData(FText::Format(LOCTEXT("Close", "Close {0}"), DisplayName), true);
		return;
	}

	// Opening the gate under an alert raises the alarm, so the prompt says so before the press. A lock
	// the flood ate through is worth a word too, since the door used to ask for a key.
	const UFacilityStateSubsystem* Facility = FindFacility();
	const bool bLoud = Facility != nullptr && Facility->IsDoorLoudToOpen(DeviceId);
	const bool bCorroded = Facility != nullptr && Facility->IsDoorLockCorroded(DeviceId);
	FText Format = LOCTEXT("Open", "Open {0}");
	if (bLoud)
	{
		Format = LOCTEXT("OpenLoud", "Open {0} (loud)");
	}
	else if (bCorroded)
	{
		Format = LOCTEXT("OpenCorroded", "Open {0} (lock corroded)");
	}
	Interactable->SetDisplayData(FText::Format(Format, DisplayName), true);
}

void AFacilityDoor::RefreshHackDisplay()
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	const int32 HackLevel = GetHackLevel();
	if (Facility == nullptr || HackLevel <= 0)
	{
		// Nothing the handheld has business with. The line stays empty.
		Interactable->SetHackDisplayData(FText::GetEmpty(), false);
		return;
	}

	if (Facility->CanHackDoor(DeviceId))
	{
		// A hack beyond the handheld can still be attempted. It fails, and the noise raises the alarm.
		const bool bSucceeds = Facility->WouldHackSucceed(HackLevel);
		Interactable->SetHackDisplayData(
			FText::Format(bSucceeds ? LOCTEXT("Hack", "Hack {0}") : LOCTEXT("HackFails", "Hack {0} (will fail, loud)"), DisplayName), true);
		return;
	}

	// A hackable door the handheld cannot reach right now. Power is the one reason worth a line of its
	// own: an open or unlocked door has nothing to hack, and a seal is on the interact line already.
	if (IsPowered() == false)
	{
		Interactable->SetHackDisplayData(DescribeNoPower(GetCircuit()), false);
		return;
	}

	Interactable->SetHackDisplayData(FText::GetEmpty(), false);
}

void AFacilityDoor::OnHacked(AActor* Hacker)
{
	Super::OnHacked(Hacker);
	Hack();
}

bool AFacilityDoor::Hack()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->HackDoor(DeviceId);
}

bool AFacilityDoor::CanHack() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanHackDoor(DeviceId);
}

bool AFacilityDoor::Use()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	if (Facility == nullptr)
	{
		return false;
	}

	// The pry bar only when it is the way past what holds the door. A door nothing holds opens by hand, quietly.
	return Facility->CanForceDoor(DeviceId) ? Facility->ForceDoor(DeviceId) : Facility->ToggleDoor(DeviceId);
}

bool AFacilityDoor::Toggle()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->ToggleDoor(DeviceId);
}

bool AFacilityDoor::CanForce() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanForceDoor(DeviceId);
}

bool AFacilityDoor::IsOpen() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr ? Facility->IsDoorOpen(DeviceId) : bInitiallyOpen;
}

EDoorObstacle AFacilityDoor::GetObstacle() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr ? Facility->GetDoorObstacle(DeviceId) : EDoorObstacle::None;
}

bool AFacilityDoor::NeedsCircuit() const
{
	// A standard door on no circuit is mechanical, like a locker. A lockdown door or the gate on none is a mistake.
	return Kind != EDoorKind::Standard;
}

void AFacilityDoor::SnapTo(const bool bOpen)
{
	bTargetOpen = bOpen;
	bMoving = false;
	SetActorTickEnabled(false);

	Panel->SetRelativeLocation(GetPose(bOpen));
}

void AFacilityDoor::SlideTo(const bool bOpen)
{
	if (bOpen == bTargetOpen)
	{
		return;
	}

	bTargetOpen = bOpen;
	bMoving = true;
	SetActorTickEnabled(true);
}

FVector AFacilityDoor::GetPose(const bool bOpen) const
{
	return bOpen ? ClosedPose + OpenOffset : ClosedPose;
}

int32 AFacilityDoor::GetLockLevel() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr ? FFacilityRules::GetDoorLockLevel(Facility->GetState(), DeviceId) : 0;
}

EFacilityItem AFacilityDoor::GetKeyItem() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr ? FFacilityRules::GetDoorKeyItem(Facility->GetState(), DeviceId) : EFacilityItem::None;
}

FText AFacilityDoor::DescribeLock() const
{
	const int32 LockLevel = GetLockLevel();
	const EFacilityItem KeyItem = GetKeyItem();
	const FText Card = FText::Format(LOCTEXT("Card", "a level {0} keycard"), FText::AsNumber(LockLevel));
	const FText Key = FText::Format(LOCTEXT("Key", "the {0}"), AFacilityPickup::DescribeItem(KeyItem));

	if (LockLevel > 0 && KeyItem != EFacilityItem::None)
	{
		return FText::Format(LOCTEXT("NeedsCardOrKey", "Needs {0} or {1}"), Card, Key);
	}

	return FText::Format(LOCTEXT("Needs", "Needs {0}"), KeyItem != EFacilityItem::None ? Key : Card);
}

#undef LOCTEXT_NAMESPACE
