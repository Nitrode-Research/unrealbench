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
	Super::OnConstruction(Transform);

	// Show the water at its flood height, so the level reads as flooded while the actor is placed and
	// sized. Play puts it where the facility says.
	HangKillVolume();
	Surface->SetRelativeLocation(FVector(0.f, 0.f, FloodHeight));
	ApplyWaterPose();
}

void AFacilityFlood::BeginPlay()
{
	Super::BeginPlay();

	HangKillVolume();
	KillVolume->OnComponentBeginOverlap.AddDynamic(this, &AFacilityFlood::HandleKillVolumeBeginOverlap);

	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		if (FansId.IsNone())
		{
			UE_LOG(LogScifiSimEscape, Warning, TEXT("%s names no FansId. Nothing will ever drain the flood."), *GetName());
		}

		// Seeded before anything asks, so the first step already knows which fans drain the water.
		Facility->RegisterFlood(FansId);
		Facility->OnStateChanged.AddDynamic(this, &AFacilityFlood::HandleFacilityStateChanged);
	}

	if (Water->GetStaticMesh() == nullptr)
	{
		UE_LOG(LogScifiSimEscape, Warning, TEXT("%s has no mesh on Water. The flood will kill without being seen."), *GetName());
	}

	// Start as the facility says, with the surface already in place and without killing anyone.
	Reconcile(true);
}

void AFacilityFlood::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		Facility->OnStateChanged.RemoveDynamic(this, &AFacilityFlood::HandleFacilityStateChanged);
	}

	KillVolume->OnComponentBeginOverlap.RemoveDynamic(this, &AFacilityFlood::HandleKillVolumeBeginOverlap);

	Super::EndPlay(EndPlayReason);
}

void AFacilityFlood::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bMoving == false)
	{
		SetActorTickEnabled(false);
		return;
	}

	// A constant rate, so the visible rise from the floor to the flood height takes RiseSeconds.
	const float TargetZ = GetSurfaceTargetZ(bTargetFlooded);
	const float Speed = FloodHeight / FMath::Max(RiseSeconds, KINDA_SMALL_NUMBER);
	const float NextZ = FMath::FInterpConstantTo(Surface->GetRelativeLocation().Z, TargetZ, DeltaSeconds, Speed);

	// Moving the surface moves the kill volume through whoever stands on the floor, which is the moment
	// the water reaches them. The body of water follows.
	Surface->SetRelativeLocation(FVector(0.f, 0.f, NextZ));
	ApplyWaterPose();

	if (FMath::IsNearlyEqual(NextZ, TargetZ))
	{
		bMoving = false;
		SetActorTickEnabled(false);

		ReceiveSurfaceSettled(bTargetFlooded);
	}
}

bool AFacilityFlood::IsFlooded() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->IsPlantFlooded();
}

bool AFacilityFlood::IsLethal() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->IsFloodLethal();
}

float AFacilityFlood::GetSurfaceHeight() const
{
	return Surface->GetRelativeLocation().Z;
}

void AFacilityFlood::HandleKillVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Whether the water rose to the pawn or the pawn walked into the water, this is asked at that
	// moment and never remembered: the facility state alone decides whether the water kills.
	if (IsLethal())
	{
		Kill(Cast<APawn>(OtherActor));
	}
}

void AFacilityFlood::HandleFacilityStateChanged()
{
	// A reset starts a new attempt, so the water is put in its state rather than reacting to it.
	const UFacilityStateSubsystem* Facility = FindFacility();
	Reconcile(Facility != nullptr && Facility->IsResetting());
}

void AFacilityFlood::Reconcile(const bool bInstant)
{
	const bool bFlooded = IsFlooded();
	const bool bLethal = IsLethal();

	// Recorded before anything reacts. A kill can change the facility, a death resetting it, and the
	// nested state change has to compare against these values, not stale ones.
	const bool bFloodedChanged = bFlooded != bLastReportedFlooded;
	const bool bLethalChanged = bLethal != bLastReportedLethal;
	bLastReportedFlooded = bFlooded;
	bLastReportedLethal = bLethal;

	if (bInstant)
	{
		SnapTo(bFlooded);
	}
	else
	{
		MoveTo(bFlooded);
	}

	if (bInstant || bFloodedChanged)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("%s: the Plant floor is %s%s."),
			*GetName(), bFlooded ? TEXT("flooding") : TEXT("draining"), bInstant ? TEXT(", set instantly") : TEXT(""));

		ReceiveFloodedChanged(bFlooded, bInstant);
	}

	if (bInstant || bLethalChanged)
	{
		ReceiveLethalChanged(bLethal, bInstant);
	}

	// Standing water turning lethal on someone already in it kills them there and then, the way a fan
	// starting does. Water still under the floor reaches nobody yet; the rise catches them. A snap is
	// the level beginning or a new attempt, usually a death's doing, and killing from inside it would
	// catch the same victim again.
	if (bLethal && bLethalChanged && bInstant == false)
	{
		KillPawnsInside();
	}
}

void AFacilityFlood::SnapTo(const bool bFlooded)
{
	bTargetFlooded = bFlooded;
	bMoving = false;
	SetActorTickEnabled(false);

	Surface->SetRelativeLocation(FVector(0.f, 0.f, GetSurfaceTargetZ(bFlooded)));
	ApplyWaterPose();
}

void AFacilityFlood::MoveTo(const bool bFlooded)
{
	if (bFlooded == bTargetFlooded)
	{
		return;
	}

	if (RiseSeconds <= 0.f)
	{
		SnapTo(bFlooded);
		return;
	}

	bTargetFlooded = bFlooded;
	bMoving = true;
	SetActorTickEnabled(true);
}

void AFacilityFlood::ApplyWaterPose()
{
	const float Height = Surface->GetRelativeLocation().Z;
	const UStaticMesh* Mesh = Water->GetStaticMesh();

	// Nothing above the floor, nothing to see.
	if (Mesh == nullptr || Height <= 0.f)
	{
		Water->SetVisibility(false);
		return;
	}
	Water->SetVisibility(true);

	// Only Z is the actor's. X and Y, the scale that covers the floor and where it sits, stay as authored.
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const float MeshHeight = 2.f * Bounds.BoxExtent.Z;
	FVector Scale = Water->GetRelativeScale3D();
	FVector Location = Water->GetRelativeLocation();

	if (MeshHeight <= 1.f)
	{
		// A flat plane has no body to grow. It is the surface, and moves with it.
		Location.Z = Height;
	}
	else
	{
		// A solid mesh is the body of water: scaled on Z so it spans from the floor to the surface, and
		// placed so its bottom is on the floor whatever its pivot.
		Scale.Z = Height / MeshHeight;
		Location.Z = -Scale.Z * (Bounds.Origin.Z - Bounds.BoxExtent.Z);
		Water->SetRelativeScale3D(Scale);
	}

	Water->SetRelativeLocation(Location);
}

void AFacilityFlood::HangKillVolume()
{
	// Its top is the surface, so the water reaches a pawn exactly when the volume does.
	const float HalfHeight = KillVolume->GetUnscaledBoxExtent().Z;
	KillVolume->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight));
}

float AFacilityFlood::GetSurfaceTargetZ(const bool bFlooded) const
{
	return bFlooded ? FloodHeight : -DrainedDepth;
}

void AFacilityFlood::KillPawnsInside()
{
	TArray<AActor*> Inside;
	KillVolume->GetOverlappingActors(Inside, APawn::StaticClass());

	for (AActor* Actor : Inside)
	{
		// A kill can change the facility, so the water asks again before each one.
		if (IsLethal() == false)
		{
			return;
		}

		Kill(Cast<APawn>(Actor));
	}
}

void AFacilityFlood::Kill(APawn* Victim)
{
	if (IsValid(Victim) == false || Victim->IsActorBeingDestroyed())
	{
		return;
	}

	UE_LOG(LogScifiSimEscape, Log, TEXT("%s: the live water reached %s at %.0f cm."), *GetName(), *Victim->GetName(), GetSurfaceHeight());

	UGameplayStatics::ApplyDamage(Victim, KillDamage, nullptr, this, nullptr);
}

UFacilityStateSubsystem* AFacilityFlood::FindFacility() const
{
	return UWorld::GetSubsystem<UFacilityStateSubsystem>(GetWorld());
}
