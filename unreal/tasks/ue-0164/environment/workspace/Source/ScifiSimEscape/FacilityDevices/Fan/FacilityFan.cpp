#include "FacilityDevices/Fan/FacilityFan.h"

#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Facility/FacilityStateSubsystem.h"
#include "GameFramework/Pawn.h"
#include "InteractionSystem/InteractableComponent.h"
#include "Kismet/GameplayStatics.h"
#include "ScifiSimEscape.h"

#define LOCTEXT_NAMESPACE "FacilityFan"

AFacilityFan::AFacilityFan()
{
	// Ticks only while the blades turn. Starting turns it on, coming to rest turns it off.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// The vent fans run on PLANT.
	DefaultCircuit = EFacilityCircuit::EFC_Plant;

	Hub = CreateDefaultSubobject<USceneComponent>(TEXT("Hub"));
	Hub->SetupAttachment(Root);
	Hub->Mobility = EComponentMobility::Movable;

	Blades = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Blades"));
	Blades->SetupAttachment(Hub);
	Blades->Mobility = EComponentMobility::Movable;
	// The kill volume does the killing. Blades that collided would hold a player up on a stopped fan.
	Blades->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	KillVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("KillVolume"));
	KillVolume->SetupAttachment(Root);
	// About 120 units tall. Sized per shaft in the Blueprint or the level.
	KillVolume->InitBoxExtent(FVector(50.f, 50.f, 60.f));
	KillVolume->ShapeColor = FColor::Red;
	// Overlaps pawns and nothing else, so it never blocks a trace or the drop.
	KillVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	KillVolume->SetCollisionObjectType(ECC_WorldDynamic);
	KillVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	KillVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	KillVolume->SetGenerateOverlapEvents(true);
	KillVolume->SetCanEverAffectNavigation(false);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	// Switched on and off while playing.
	Light->Mobility = EComponentMobility::Movable;
}

void AFacilityFan::BeginPlay()
{
	// TODO: Restore the environmental hazard behavior.
	Super::BeginPlay();
}

void AFacilityFan::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// TODO: Restore the environmental hazard behavior.
	Super::EndPlay(EndPlayReason);
}

void AFacilityFan::Tick(const float DeltaSeconds)
{
	// TODO: Restore the environmental hazard behavior.
	Super::Tick(DeltaSeconds);
}

void AFacilityFan::OnFacilityStateChanged()
{
	// TODO: Restore the environmental hazard behavior.
	Super::OnFacilityStateChanged();
}

void AFacilityFan::RefreshDisplayData()
{
	// TODO: Restore the environmental hazard behavior.
}

bool AFacilityFan::IsRunning() const
{
	// TODO: Restore the environmental hazard behavior.
	return false;
}

void AFacilityFan::HandleKillVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// TODO: Restore the environmental hazard behavior.
}

void AFacilityFan::ReconcileRunning(const bool bInstant)
{
	// TODO: Restore the environmental hazard behavior.
}

void AFacilityFan::KillPawnsInside()
{
	// TODO: Restore the environmental hazard behavior.
}

void AFacilityFan::Kill(APawn* Victim)
{
	// TODO: Restore the environmental hazard behavior.
}

void AFacilityFan::RefreshLight()
{
	// TODO: Restore the environmental hazard behavior.
}

#undef LOCTEXT_NAMESPACE
