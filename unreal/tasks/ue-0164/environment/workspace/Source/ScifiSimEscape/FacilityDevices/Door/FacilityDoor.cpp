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
	// TODO: Restore the actor behavior described in instruction.md.
	Super::BeginPlay();
}

void AFacilityDoor::Tick(const float DeltaSeconds)
{
	// TODO: Restore the actor behavior described in instruction.md.
	Super::Tick(DeltaSeconds);
}

void AFacilityDoor::OnFacilityStateChanged()
{
	// TODO: Restore the actor behavior described in instruction.md.
	Super::OnFacilityStateChanged();
}

void AFacilityDoor::RefreshDisplayData()
{
	// TODO: Restore the actor behavior described in instruction.md.
}

void AFacilityDoor::RefreshInteractDisplay()
{
	// TODO: Restore the actor behavior described in instruction.md.
}

void AFacilityDoor::RefreshHackDisplay()
{
	// TODO: Restore the actor behavior described in instruction.md.
}

void AFacilityDoor::OnHacked(AActor* Hacker)
{
	// TODO: Restore the actor behavior described in instruction.md.
	Super::OnHacked(Hacker);
}

bool AFacilityDoor::Hack()
{
	// TODO: Restore the actor behavior described in instruction.md.
	return false;
}

bool AFacilityDoor::CanHack() const
{
	// TODO: Restore the actor behavior described in instruction.md.
	return false;
}

bool AFacilityDoor::Use()
{
	// TODO: Restore the actor behavior described in instruction.md.
	return false;
}

bool AFacilityDoor::Toggle()
{
	// TODO: Restore the actor behavior described in instruction.md.
	return false;
}

bool AFacilityDoor::CanForce() const
{
	// TODO: Restore the actor behavior described in instruction.md.
	return false;
}

bool AFacilityDoor::IsOpen() const
{
	// TODO: Restore the actor behavior described in instruction.md.
	return false;
}

EDoorObstacle AFacilityDoor::GetObstacle() const
{
	// TODO: Restore the actor behavior described in instruction.md.
	return EDoorObstacle::None;
}

bool AFacilityDoor::NeedsCircuit() const
{
	// TODO: Restore the actor behavior described in instruction.md.
	return false;
}

void AFacilityDoor::SnapTo(const bool bOpen)
{
	// TODO: Restore the actor behavior described in instruction.md.
}

void AFacilityDoor::SlideTo(const bool bOpen)
{
	// TODO: Restore the actor behavior described in instruction.md.
}

FVector AFacilityDoor::GetPose(const bool bOpen) const
{
	// TODO: Restore the actor behavior described in instruction.md.
	return FVector::ZeroVector;
}

int32 AFacilityDoor::GetLockLevel() const
{
	// TODO: Restore the actor behavior described in instruction.md.
	return 0;
}

EFacilityItem AFacilityDoor::GetKeyItem() const
{
	// TODO: Restore the actor behavior described in instruction.md.
	return EFacilityItem::None;
}

FText AFacilityDoor::DescribeLock() const
{
	// TODO: Restore the actor behavior described in instruction.md.
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
