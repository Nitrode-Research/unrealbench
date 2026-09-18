#include "FacilityDevices/Flood/FacilityFlood.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Facility/FacilityStateSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "ScifiSimEscape.h"

AFacilityFlood::AFacilityFlood()
{
	// Ticks only while the surface rises or sinks. MoveTo turns it on, settling turns it off.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Surface = CreateDefaultSubobject<USceneComponent>(TEXT("Surface"));
	Surface->SetupAttachment(Root);
	Surface->Mobility = EComponentMobility::Movable;

	Water = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Water"));
	// Under the root, at floor level: the actor grows it up from there to wherever the surface is.
	Water->SetupAttachment(Root);
	Water->Mobility = EComponentMobility::Movable;
	// Water is nothing the player uses or stands on.
	Water->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	KillVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("KillVolume"));
	KillVolume->SetupAttachment(Surface);
	KillVolume->Mobility = EComponentMobility::Movable;
	// A room's floor, knee deep. Sized to the Plant floor in the Blueprint or the level; the actor hangs it under the surface.
	KillVolume->InitBoxExtent(FVector(500.f, 500.f, 40.f));
	KillVolume->ShapeColor = FColor::Blue;
	// Overlaps pawns and nothing else, so it never blocks anything or catches a trace. A moving box
	// generates overlaps on whoever it reaches, which is how the rising water catches a standing player.
	KillVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	KillVolume->SetCollisionObjectType(ECC_WorldDynamic);
	KillVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	KillVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	KillVolume->SetGenerateOverlapEvents(true);
	KillVolume->SetCanEverAffectNavigation(false);
}

void AFacilityFlood::OnConstruction(const FTransform& Transform)
{
	// TODO: Restore the environmental hazard behavior.
	Super::OnConstruction(Transform);
}

void AFacilityFlood::BeginPlay()
{
	// TODO: Restore the environmental hazard behavior.
	Super::BeginPlay();
}

void AFacilityFlood::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// TODO: Restore the environmental hazard behavior.
	Super::EndPlay(EndPlayReason);
}

void AFacilityFlood::Tick(const float DeltaSeconds)
{
	// TODO: Restore the environmental hazard behavior.
	Super::Tick(DeltaSeconds);
}

bool AFacilityFlood::IsFlooded() const
{
	// TODO: Restore the environmental hazard behavior.
	return false;
}

bool AFacilityFlood::IsLethal() const
{
	// TODO: Restore the environmental hazard behavior.
	return false;
}

float AFacilityFlood::GetSurfaceHeight() const
{
	return Surface->GetRelativeLocation().Z;
}

void AFacilityFlood::HandleKillVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// TODO: Restore the environmental hazard behavior.
}

void AFacilityFlood::HandleFacilityStateChanged()
{
	// TODO: Restore the environmental hazard behavior.
}

void AFacilityFlood::Reconcile(const bool bInstant)
{
	// TODO: Restore the environmental hazard behavior.
}

void AFacilityFlood::SnapTo(const bool bFlooded)
{
	// TODO: Restore the environmental hazard behavior.
}

void AFacilityFlood::MoveTo(const bool bFlooded)
{
	// TODO: Restore the environmental hazard behavior.
}

void AFacilityFlood::ApplyWaterPose()
{
	// TODO: Restore the environmental hazard behavior.
}

void AFacilityFlood::HangKillVolume()
{
	// TODO: Restore the environmental hazard behavior.
}

float AFacilityFlood::GetSurfaceTargetZ(const bool bFlooded) const
{
	// TODO: Restore the environmental hazard behavior.
	return 0.f;
}

void AFacilityFlood::KillPawnsInside()
{
	// TODO: Restore the environmental hazard behavior.
}

void AFacilityFlood::Kill(APawn* Victim)
{
	// TODO: Restore the environmental hazard behavior.
}

UFacilityStateSubsystem* AFacilityFlood::FindFacility() const
{
	return UWorld::GetSubsystem<UFacilityStateSubsystem>(GetWorld());
}
