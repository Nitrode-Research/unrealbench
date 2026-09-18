#include "FacilityDevices/Crate/FacilityCrate.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Facility/FacilityStateSubsystem.h"
#include "GameFramework/Pawn.h"
#include "InteractionSystem/InteractableComponent.h"
#include "Kismet/GameplayStatics.h"
#include "ScifiSimEscape.h"

#define LOCTEXT_NAMESPACE "FacilityCrate"

AFacilityCrate::AFacilityCrate()
{
	// Ticks only while the body slides. SlideTo turns it on, settling turns it off.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	ShaftHead = CreateDefaultSubobject<USceneComponent>(TEXT("ShaftHead"));
	ShaftHead->SetupAttachment(Root);
	// Beside the shutter, so a freshly placed crate already reads as one. Moved per shaft in the level.
	ShaftHead->SetRelativeLocation(FVector(300.f, 0.f, 0.f));

	Landing = CreateDefaultSubobject<USceneComponent>(TEXT("Landing"));
	Landing->SetupAttachment(Root);
	// One storey down under the shaft head.
	Landing->SetRelativeLocation(FVector(300.f, 0.f, -400.f));

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->Mobility = EComponentMobility::Movable;

	KillVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("KillVolume"));
	KillVolume->SetupAttachment(Landing);
	// The crate's footprint, a little above the floor so a standing pawn is inside it.
	KillVolume->InitBoxExtent(FVector(60.f, 60.f, 100.f));
	KillVolume->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
	KillVolume->ShapeColor = FColor::Red;
	// Overlaps pawns and nothing else, so it never blocks anything or catches a trace.
	KillVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	KillVolume->SetCollisionObjectType(ECC_WorldDynamic);
	KillVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	KillVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	KillVolume->SetGenerateOverlapEvents(true);
	KillVolume->SetCanEverAffectNavigation(false);
}

void AFacilityCrate::BeginPlay()
{
	Super::BeginPlay();
}

void AFacilityCrate::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}

void AFacilityCrate::OnFacilityStateChanged()
{
	Super::OnFacilityStateChanged();
}

void AFacilityCrate::RefreshDisplayData()
{
}

bool AFacilityCrate::Use()
{
	return false;
}

bool AFacilityCrate::Move()
{
	return false;
}

bool AFacilityCrate::Drop()
{
	return false;
}

ECratePosition AFacilityCrate::GetPosition() const
{
	return ECratePosition::JammingShutter;
}

bool AFacilityCrate::CanMove() const
{
	return false;
}

bool AFacilityCrate::CanDrop() const
{
	return false;
}

bool AFacilityCrate::WasDropped() const
{
	return false;
}

void AFacilityCrate::SnapTo(const ECratePosition Position)
{
}

void AFacilityCrate::SlideTo(const ECratePosition Position, const float Speed)
{
}

FVector AFacilityCrate::GetPoseLocation(const ECratePosition Position) const
{
	return FVector::ZeroVector;
}

void AFacilityCrate::KillPawnsBelow()
{
}

void AFacilityCrate::Kill(APawn* Victim)
{
}

#undef LOCTEXT_NAMESPACE
