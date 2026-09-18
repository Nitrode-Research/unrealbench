#include "FacilityDevices/Exit/FacilityExit.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Facility/FacilityRules.h"
#include "Facility/FacilityStateSubsystem.h"
#include "FacilityDevices/FacilityActorBase.h"
#include "GameFramework/Pawn.h"
#include "ScifiSimEscape.h"

AFacilityExit::AFacilityExit()
{
	// Nothing to tick. The volume reports entries and the facility reports changes.
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	Volume->SetupAttachment(Root);
	// About a doorway. Sized per opening in the Blueprint or the level.
	Volume->InitBoxExtent(FVector(50.f, 100.f, 120.f));
	Volume->ShapeColor = FColor::Green;
	// Overlaps pawns and nothing else, so it never blocks anything or catches a trace.
	Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Volume->SetCollisionObjectType(ECC_WorldDynamic);
	Volume->SetCollisionResponseToAllChannels(ECR_Ignore);
	Volume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Volume->SetGenerateOverlapEvents(true);
	Volume->SetCanEverAffectNavigation(false);
}

void AFacilityExit::BeginPlay()
{
	// Restore the published gameplay contract.
	Super::BeginPlay();
}

void AFacilityExit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Restore the published gameplay contract.
	Super::EndPlay(EndPlayReason);
}

void AFacilityExit::HandleVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Restore the published gameplay contract.
}

void AFacilityExit::HandleFacilityStateChanged()
{
	// Restore the published gameplay contract.
}

bool AFacilityExit::TryEscape()
{
	// Restore the published gameplay contract.
	return {};
}

FString AFacilityExit::DescribeWhyShut() const
{
	// Restore the published gameplay contract.
	return {};
}

UFacilityStateSubsystem* AFacilityExit::FindFacility() const
{
	// Restore the published gameplay contract.
	return {};
}
