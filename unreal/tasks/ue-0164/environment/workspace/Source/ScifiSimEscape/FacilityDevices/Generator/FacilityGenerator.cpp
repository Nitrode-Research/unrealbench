#include "FacilityDevices/Generator/FacilityGenerator.h"

#include "Components/StaticMeshComponent.h"
#include "Facility/FacilityStateSubsystem.h"
#include "InteractionSystem/InteractableComponent.h"

#define LOCTEXT_NAMESPACE "FacilityGenerator"

AFacilityGenerator::AFacilityGenerator()
{
	// Nothing to tick. The generator only changes when the facility state does, and the facility's clock does the counting.
	PrimaryActorTick.bCanEverTick = false;

	Housing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Housing"));
	Housing->SetupAttachment(Root);
}

void AFacilityGenerator::BeginPlay()
{
	// TODO: Restore task behavior.
	Super::BeginPlay();
}

void AFacilityGenerator::OnFacilityStateChanged()
{
	// TODO: Restore task behavior.
	Super::OnFacilityStateChanged();
}

void AFacilityGenerator::RefreshDisplayData()
{
	// TODO: Restore task behavior.
}

bool AFacilityGenerator::Use()
{
	// TODO: Restore task behavior.
	return false;
}

bool AFacilityGenerator::Start()
{
	// TODO: Restore task behavior.
	return false;
}

bool AFacilityGenerator::Overload()
{
	// TODO: Restore task behavior.
	return false;
}

bool AFacilityGenerator::IsRunning() const
{
	// TODO: Restore task behavior.
	return false;
}

int32 AFacilityGenerator::GetStepsLeft() const
{
	// TODO: Restore task behavior.
	return 0;
}

bool AFacilityGenerator::IsOverloaded() const
{
	// TODO: Restore task behavior.
	return false;
}

bool AFacilityGenerator::CanStart() const
{
	// TODO: Restore task behavior.
	return false;
}

bool AFacilityGenerator::CanOverload() const
{
	// TODO: Restore task behavior.
	return false;
}

void AFacilityGenerator::Reconcile(const bool bInstant)
{
	// TODO: Restore task behavior.
}

#undef LOCTEXT_NAMESPACE
