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
	Super::BeginPlay();

	// Start as the facility says, without a start-up or a blast.
	Reconcile(true);
}

void AFacilityGenerator::OnFacilityStateChanged()
{
	Super::OnFacilityStateChanged();

	// A reset starts a new attempt, so the generator is put in its state rather than reacting to it.
	const UFacilityStateSubsystem* Facility = FindFacility();
	Reconcile(Facility != nullptr && Facility->IsResetting());
}

void AFacilityGenerator::RefreshDisplayData()
{
	if (IsOverloaded())
	{
		Interactable->SetDisplayData(LOCTEXT("Overloaded", "Overloaded"), false);
		return;
	}

	if (IsRunning())
	{
		// The pry bar rigs a running generator. Without it, the count is what there is to see.
		if (CanOverload())
		{
			Interactable->SetDisplayData(LOCTEXT("Overload", "Rig to overload (blackout)"), true);
		}
		else
		{
			Interactable->SetDisplayData(
				FText::Format(LOCTEXT("Running", "Running, {0} steps left"), FText::AsNumber(GetStepsLeft())), false);
		}
		return;
	}

	// Starting is loud, so the prompt says so before the press.
	Interactable->SetDisplayData(LOCTEXT("Start", "Start generator (loud)"), CanStart());
}

bool AFacilityGenerator::Use()
{
	// The pry bar only when it is the thing to do: a stopped generator is started, a running one is rigged.
	return CanOverload() ? Overload() : Start();
}

bool AFacilityGenerator::Start()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->StartGenerator();
}

bool AFacilityGenerator::Overload()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->OverloadGenerator();
}

bool AFacilityGenerator::IsRunning() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->IsGeneratorRunning();
}

int32 AFacilityGenerator::GetStepsLeft() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr ? Facility->GetGeneratorStepsLeft() : 0;
}

bool AFacilityGenerator::IsOverloaded() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->IsGeneratorOverloaded();
}

bool AFacilityGenerator::CanStart() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanStartGenerator();
}

bool AFacilityGenerator::CanOverload() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanOverloadGenerator();
}

void AFacilityGenerator::Reconcile(const bool bInstant)
{
	const bool bRunning = IsRunning();
	const bool bOverloaded = IsOverloaded();

	// Recorded before the hooks run, so a change made in response compares against the new values.
	const bool bRunningChanged = bRunning != bLastReportedRunning;
	const bool bOverloadedChanged = bOverloaded != bLastReportedOverloaded;
	bLastReportedRunning = bRunning;
	bLastReportedOverloaded = bOverloaded;

	// Running first: an overload stops the generator as it wrecks it, and the stop belongs before the blast.
	if (bInstant || bRunningChanged)
	{
		OnRunningChanged(bRunning, bInstant);
		ReceiveRunningChanged(bRunning, bInstant);
	}

	if (bInstant || bOverloadedChanged)
	{
		OnOverloadedChanged(bOverloaded, bInstant);
		ReceiveOverloadedChanged(bOverloaded, bInstant);
	}
}

#undef LOCTEXT_NAMESPACE
