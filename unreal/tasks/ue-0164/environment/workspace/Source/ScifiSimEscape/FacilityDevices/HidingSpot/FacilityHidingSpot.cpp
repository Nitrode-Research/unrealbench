#include "FacilityDevices/HidingSpot/FacilityHidingSpot.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"

AFacilityHidingSpot::AFacilityHidingSpot()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	Volume->SetupAttachment(Root);
}
