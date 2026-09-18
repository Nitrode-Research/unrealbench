#include "FacilityDevices/Light/FacilityLight.h"

#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Facility/FacilityStateSubsystem.h"
#include "InteractionSystem/InteractableComponent.h"
#include "ScifiSimEscape.h"

#define LOCTEXT_NAMESPACE "FacilityLight"

AFacilityLight::AFacilityLight()
{
	// Nothing to tick. A fixture only changes when the facility state does.
	PrimaryActorTick.bCanEverTick = false;

	// Level 1 and Storage lights run on DOORS. Fixtures in Plant are put on PLANT on the instance or in a Blueprint.
	DefaultCircuit = EFacilityCircuit::EFC_Doors;

	Fixture = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Fixture"));
	Fixture->SetupAttachment(Root);
	// A lamp is nothing the player uses. With collision it would block the player and catch the Interactor's trace.
	Fixture->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	// Switched on and off while playing.
	Light->Mobility = EComponentMobility::Movable;
}

void AFacilityLight::BeginPlay()
{
	Super::BeginPlay();

	// Start as the facility says, lit or dark without a transition.
	ReconcileLit(true);
}

void AFacilityLight::OnFacilityStateChanged()
{
	Super::OnFacilityStateChanged();

	// A reset starts a new attempt, so the fixture is set to the state rather than switched to it.
	const UFacilityStateSubsystem* Facility = FindFacility();
	ReconcileLit(Facility != nullptr && Facility->IsResetting());
}

void AFacilityLight::RefreshDisplayData()
{
	// Without collision nothing can focus a fixture. Kept honest all the same.
	if (IsLit())
	{
		Interactable->SetDisplayData(LOCTEXT("On", "Light on"), false);
		return;
	}

	// The rules only put lights out for power today. The fallback keeps the prompt honest if that changes.
	Interactable->SetDisplayData(IsPowered() ? LOCTEXT("Off", "Light off") : DescribeNoPower(GetCircuit()), false);
}

bool AFacilityLight::IsLit() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->AreLightsOn(DeviceId);
}

void AFacilityLight::ReconcileLit(const bool bInstant)
{
	const bool bLit = IsLit();
	if (bInstant == false && bLit == bLastReportedLit)
	{
		return;
	}

	// Recorded before the hooks run, so a change made in response compares against the new value.
	bLastReportedLit = bLit;

	// Every light on the actor, so spot or rect lights a Blueprint child adds follow the zone too.
	TInlineComponentArray<ULightComponent*> LightComponents;
	GetComponents(LightComponents);
	for (ULightComponent* LightComponent : LightComponents)
	{
		LightComponent->SetVisibility(bLit);
	}

	// Verbose only: one breaker flip reaches every fixture in a zone, and the facility logs the change once already.
	UE_LOG(LogScifiSimEscape, Verbose, TEXT("%s (%s) is %s%s."),
		*GetName(), *DeviceId.ToString(), bLit ? TEXT("lit") : TEXT("dark"), bInstant ? TEXT(", set instantly") : TEXT(""));

	OnLitChanged(bLit, bInstant);
	ReceiveLitChanged(bLit, bInstant);
}

#undef LOCTEXT_NAMESPACE
