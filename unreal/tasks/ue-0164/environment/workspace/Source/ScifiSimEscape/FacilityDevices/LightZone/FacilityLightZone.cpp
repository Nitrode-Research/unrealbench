#include "FacilityDevices/LightZone/FacilityLightZone.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Facility/FacilityStateSubsystem.h"
#include "ScifiSimEscape.h"

AFacilityLightZone::AFacilityLightZone()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	Volume->SetupAttachment(Root);
}

void AFacilityLightZone::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogScifiSimEscape, Log, TEXT("Light-zone implementation is pending."));
}

bool AFacilityLightZone::IsLit() const
{
	return false;
}
