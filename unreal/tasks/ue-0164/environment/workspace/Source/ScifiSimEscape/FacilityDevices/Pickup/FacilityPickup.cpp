#include "FacilityDevices/Pickup/FacilityPickup.h"

#include "Components/StaticMeshComponent.h"
#include "Facility/FacilityStateSubsystem.h"
#include "InteractionSystem/InteractableComponent.h"
#include "ScifiSimEscape.h"

#define LOCTEXT_NAMESPACE "FacilityPickup"

AFacilityPickup::AFacilityPickup()
{
	// Nothing to tick. A pickup only changes when the facility state does.
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
}

void AFacilityPickup::BeginPlay()
{
	// TODO: Restore the actor behavior described in instruction.md.
	Super::BeginPlay();
}

void AFacilityPickup::OnFacilityStateChanged()
{
	// TODO: Restore the actor behavior described in instruction.md.
	Super::OnFacilityStateChanged();
}

void AFacilityPickup::RefreshDisplayData()
{
	// TODO: Restore the actor behavior described in instruction.md.
}

bool AFacilityPickup::Take()
{
	// TODO: Restore the actor behavior described in instruction.md.
	return false;
}

EFacilityItem AFacilityPickup::GetItem() const
{
	// TODO: Restore the actor behavior described in instruction.md.
	return EFacilityItem::None;
}

bool AFacilityPickup::IsTaken() const
{
	// TODO: Restore the actor behavior described in instruction.md.
	return false;
}

FText AFacilityPickup::DescribeItem(const EFacilityItem Item)
{
	// TODO: Restore the actor behavior described in instruction.md.
	return FText::GetEmpty();
}

void AFacilityPickup::ReconcileTaken(const bool bInstant)
{
	// TODO: Restore the actor behavior described in instruction.md.
}

#undef LOCTEXT_NAMESPACE
