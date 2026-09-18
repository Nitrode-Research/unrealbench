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
	// Seed the pickup before the base begins play, so the first display refresh already sees it.
	// What lies here comes from the settings inside RegisterPickup.
	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		if (PickupId.IsNone())
		{
			UE_LOG(LogScifiSimEscape, Warning,
				TEXT("%s has no PickupId. It cannot register with the facility, so it will not appear in the world."),
				*GetName());
		}

		Facility->RegisterPickup(PickupId);
	}

	Super::BeginPlay();

	// Start as the facility says: a pickup emptied before this actor was re-created stays gone.
	ReconcileTaken(true);
}

void AFacilityPickup::OnFacilityStateChanged()
{
	Super::OnFacilityStateChanged();

	// A reset puts the pickup back as a new attempt, not as something that happened in the world.
	const UFacilityStateSubsystem* Facility = FindFacility();
	ReconcileTaken(Facility != nullptr && Facility->IsResetting());
}

void AFacilityPickup::RefreshDisplayData()
{
	if (IsTaken())
	{
		// Hidden and without collision, so nothing can focus it. Kept honest all the same.
		Interactable->SetDisplayData(LOCTEXT("Taken", "Taken"), false);
		return;
	}

	Interactable->SetDisplayData(FText::Format(LOCTEXT("Take", "Take {0}"), DescribeItem(GetItem())), true);
}

bool AFacilityPickup::Take()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->TakePickup(PickupId);
}

EFacilityItem AFacilityPickup::GetItem() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr ? Facility->GetPickupItem(PickupId) : EFacilityItem::None;
}

bool AFacilityPickup::IsTaken() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->IsPickupTaken(PickupId);
}

FText AFacilityPickup::DescribeItem(const EFacilityItem Item)
{
	switch (Item)
	{
	case EFacilityItem::PryBar:
		return LOCTEXT("PryBar", "pry bar");

	case EFacilityItem::ServerDrive:
		return LOCTEXT("ServerDrive", "server drive");

	case EFacilityItem::Keycard2:
		return LOCTEXT("Keycard2", "level 2 keycard");

	case EFacilityItem::ServiceKey:
		return LOCTEXT("ServiceKey", "service key");

	case EFacilityItem::ImprovisedWeapon:
		return LOCTEXT("ImprovisedWeapon", "improvised weapon");

	case EFacilityItem::Firearm:
		return LOCTEXT("Firearm", "firearm");

	case EFacilityItem::Keycard3:
		return LOCTEXT("Keycard3", "level 3 keycard");

	case EFacilityItem::LockerItem:
		return LOCTEXT("LockerItem", "locker item");

	default:
		return LOCTEXT("Nothing", "nothing");
	}
}

void AFacilityPickup::ReconcileTaken(const bool bInstant)
{
	const bool bTaken = IsTaken();
	if (bInstant == false && bTaken == bLastReportedTaken)
	{
		return;
	}

	// Recorded before the hooks run, so a change made in response compares against the new value.
	bLastReportedTaken = bTaken;

	// Gone from the world entirely while taken: invisible, and without collision so the Interactor's
	// overlap and trace stop finding it.
	SetActorHiddenInGame(bTaken);
	SetActorEnableCollision(bTaken == false);

	OnTakenChanged(bTaken, bInstant);
	ReceiveTakenChanged(bTaken, bInstant);
}

#undef LOCTEXT_NAMESPACE
