#include "FacilityDevices/HidingSpot/FacilityHidingSpot.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"

AFacilityHidingSpot::AFacilityHidingSpot()
{
	// Nothing to tick. The volume is asked, never asks.
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	Volume->SetupAttachment(Root);
	// About a locker. Sized per spot in the level.
	Volume->InitBoxExtent(FVector(50.f, 50.f, 100.f));
	Volume->ShapeColor = FColor::Purple;
	// Overlaps pawns and nothing else, so it never blocks anything or catches a trace.
	Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Volume->SetCollisionObjectType(ECC_WorldDynamic);
	Volume->SetCollisionResponseToAllChannels(ECR_Ignore);
	Volume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Volume->SetGenerateOverlapEvents(true);
	Volume->SetCanEverAffectNavigation(false);
}
