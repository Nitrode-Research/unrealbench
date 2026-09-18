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
	// Restore the published gameplay contract.
	Super::BeginPlay();
}

void AFacilityLight::OnFacilityStateChanged()
{
	// Restore the published gameplay contract.
}

void AFacilityLight::RefreshDisplayData()
{
	// Restore the published gameplay contract.
}

bool AFacilityLight::IsLit() const
{
	// Restore the published gameplay contract.
	return {};
}

void AFacilityLight::ReconcileLit(const bool bInstant)
{
	// Restore the published gameplay contract.
}

#undef LOCTEXT_NAMESPACE
