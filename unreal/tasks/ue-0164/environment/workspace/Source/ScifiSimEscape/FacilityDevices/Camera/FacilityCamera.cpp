#include "FacilityDevices/Camera/FacilityCamera.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Facility/FacilityStateSubsystem.h"
#include "GameFramework/Pawn.h"
#include "InteractionSystem/InteractableComponent.h"
#include "ScifiSimEscape.h"

#define LOCTEXT_NAMESPACE "FacilityCamera"

AFacilityCamera::AFacilityCamera()
{
	// Ticks only while the housing pans, which is while the camera has power. OnPowerChanged turns it on and off.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// SECURITY powers the camera. The level the handheld loops it at is the settings', 1 in the GDD.
	DefaultCircuit = EFacilityCircuit::EFC_Security;

	Housing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Housing"));
	Housing->SetupAttachment(Root);
	// It pans.
	Housing->Mobility = EComponentMobility::Movable;

	View = CreateDefaultSubobject<UBoxComponent>(TEXT("View"));
	// Under the housing, so it sweeps with it, reaching forward from the lens along the housing's X.
	// About ten metres long and three wide. Sized to how far the camera sees in the Blueprint.
	View->SetupAttachment(Housing);
	View->SetRelativeLocation(FVector(500.f, 0.f, 0.f));
	View->InitBoxExtent(FVector(500.f, 150.f, 150.f));
	View->ShapeColor = FColor::Red;
	// Overlaps pawns and nothing else, so it never blocks anything or catches a trace.
	View->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	View->SetCollisionObjectType(ECC_WorldDynamic);
	View->SetCollisionResponseToAllChannels(ECR_Ignore);
	View->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	View->SetGenerateOverlapEvents(true);
	View->SetCanEverAffectNavigation(false);
}

void AFacilityCamera::BeginPlay()
{
	// TODO: Restore security device behavior.
	Super::BeginPlay();
}

void AFacilityCamera::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// TODO: Restore security device behavior.
	Super::EndPlay(EndPlayReason);
}

void AFacilityCamera::Tick(const float DeltaSeconds)
{
	// TODO: Restore security device behavior.
	Super::Tick(DeltaSeconds);
}

void AFacilityCamera::OnPowerChanged(const bool bPowered)
{
	// TODO: Restore security device behavior.
	Super::OnPowerChanged(bPowered);
}

void AFacilityCamera::OnFacilityStateChanged()
{
	// TODO: Restore security device behavior.
	Super::OnFacilityStateChanged();
}

void AFacilityCamera::RefreshDisplayData()
{
	// TODO: Restore security device behavior.
}

void AFacilityCamera::OnHacked(AActor* Hacker)
{
	// TODO: Restore security device behavior.
	Super::OnHacked(Hacker);
}

bool AFacilityCamera::IsWatching() const
{
	// TODO: Restore security device behavior.
	return false;
}

bool AFacilityCamera::IsLooped() const
{
	// TODO: Restore security device behavior.
	return false;
}

bool AFacilityCamera::Hack()
{
	// TODO: Restore security device behavior.
	return false;
}

bool AFacilityCamera::CanHack() const
{
	// TODO: Restore security device behavior.
	return false;
}

void AFacilityCamera::HandleViewBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// TODO: Restore security device behavior.
}

void AFacilityCamera::ReconcileWatching(const bool bInstant)
{
	// TODO: Restore security device behavior.
}

void AFacilityCamera::ReportPlayerInside()
{
	// TODO: Restore security device behavior.
}

void AFacilityCamera::ResetPan()
{
	// TODO: Restore security device behavior.
}

void AFacilityCamera::ApplyPan()
{
	// TODO: Restore security device behavior.
}

#undef LOCTEXT_NAMESPACE
