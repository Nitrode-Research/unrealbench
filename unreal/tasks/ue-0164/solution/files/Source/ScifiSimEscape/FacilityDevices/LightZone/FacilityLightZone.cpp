#include "FacilityDevices/LightZone/FacilityLightZone.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Facility/FacilityStateSubsystem.h"
#include "ScifiSimEscape.h"

AFacilityLightZone::AFacilityLightZone()
{
	// Nothing to tick. The volume is asked, never asks.
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	Volume->SetupAttachment(Root);
	// About a room. Sized per room in the level.
	Volume->InitBoxExtent(FVector(500.f, 500.f, 200.f));
	Volume->ShapeColor = FColor::Yellow;
	// Overlaps pawns and nothing else, so it never blocks anything or catches a trace.
	Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Volume->SetCollisionObjectType(ECC_WorldDynamic);
	Volume->SetCollisionResponseToAllChannels(ECR_Ignore);
	Volume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Volume->SetGenerateOverlapEvents(true);
	Volume->SetCanEverAffectNavigation(false);
}

void AFacilityLightZone::BeginPlay()
{
	Super::BeginPlay();

	if (LightsId.IsNone())
	{
		UE_LOG(LogScifiSimEscape, Warning, TEXT("%s names no LightsId. Everyone in it counts as in the dark."), *GetName());
	}
}

bool AFacilityLightZone::IsLit() const
{
	const UFacilityStateSubsystem* Facility = UWorld::GetSubsystem<UFacilityStateSubsystem>(GetWorld());
	return Facility != nullptr && Facility->AreLightsOn(LightsId);
}
