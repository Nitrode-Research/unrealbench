#include "FacilityDevices/Lift/FacilityLift.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Facility/FacilityRules.h"
#include "Facility/FacilityStateSubsystem.h"
#include "InteractionSystem/InteractableComponent.h"

#define LOCTEXT_NAMESPACE "FacilityLift"

AFacilityLift::AFacilityLift()
{
	// Ticks only while the car is travelling. TravelTo turns it on, arriving turns it off.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	BottomStop = CreateDefaultSubobject<USceneComponent>(TEXT("BottomStop"));
	BottomStop->SetupAttachment(Root);

	TopStop = CreateDefaultSubobject<USceneComponent>(TEXT("TopStop"));
	TopStop->SetupAttachment(Root);
	// One storey up, so a freshly placed lift already reads as one. Moved per shaft in the level.
	TopStop->SetRelativeLocation(FVector(0.f, 0.f, 400.f));

	Car = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Car"));
	Car->SetupAttachment(Root);
	// A character only rides a base that is Movable. Set explicitly rather than trusting the default.
	Car->Mobility = EComponentMobility::Movable;
}

// UE-0144: restore the lift behavior described in instruction.md.
void AFacilityLift::OnConstruction(const FTransform& Transform) { Super::OnConstruction(Transform); }
void AFacilityLift::BeginPlay() { Super::BeginPlay(); }
void AFacilityLift::Tick(const float DeltaSeconds) { Super::Tick(DeltaSeconds); }
void AFacilityLift::OnFacilityStateChanged() { Super::OnFacilityStateChanged(); }
bool AFacilityLift::Toggle() { return false; }
ELiftStop AFacilityLift::GetStop() const { return InitialStop; }
bool AFacilityLift::CanUse() const { return false; }
ELiftObstacle AFacilityLift::GetObstacle() const { return ELiftObstacle::NoPower; }
bool AFacilityLift::Hack() { return false; }
bool AFacilityLift::CanHack() const { return false; }
void AFacilityLift::OnHacked(AActor* Hacker) { Super::OnHacked(Hacker); }
void AFacilityLift::SnapCarTo(const ELiftStop Stop) {}
void AFacilityLift::TravelTo(const ELiftStop Stop) {}
USceneComponent* AFacilityLift::GetStopComponent(const ELiftStop Stop) const { return BottomStop; }
void AFacilityLift::RefreshDisplayData() {}
void AFacilityLift::RefreshHackDisplay() {}

#undef LOCTEXT_NAMESPACE
