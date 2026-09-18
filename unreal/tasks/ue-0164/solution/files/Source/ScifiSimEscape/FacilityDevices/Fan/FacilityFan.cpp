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
	// The hub is authored at rest. Read it before anything turns it.
	HubRestPose = Hub->GetRelativeRotation().Quaternion();

	KillVolume->OnComponentBeginOverlap.AddDynamic(this, &AFacilityFan::HandleKillVolumeBeginOverlap);

	Super::BeginPlay();

	// Start as the facility says, with the blades already at speed or at rest.
	ReconcileRunning(true);
	RefreshLight();
}

void AFacilityFan::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	KillVolume->OnComponentBeginOverlap.RemoveDynamic(this, &AFacilityFan::HandleKillVolumeBeginOverlap);

	Super::EndPlay(EndPlayReason);
}

void AFacilityFan::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float TargetSpeed = bLastReportedRunning ? MaxSpinSpeed : 0.f;
	const float RampTime = bLastReportedRunning ? SpinUpTime : SpinDownTime;

	// A constant rate over the whole range, so a fan restarted while still slowing down gets back to
	// full speed sooner.
	SpinSpeed = RampTime > 0.f
		? FMath::FInterpConstantTo(SpinSpeed, TargetSpeed, DeltaSeconds, MaxSpinSpeed / RampTime)
		: TargetSpeed;

	SpinAngle = FMath::Fmod(SpinAngle + SpinSpeed * DeltaSeconds, 360.f);

	// Right-hand side first: the turn is about the hub's own Z, then the authored pose.
	Hub->SetRelativeRotation(HubRestPose * FQuat(FVector::UpVector, FMath::DegreesToRadians(SpinAngle)));

	if (bLastReportedRunning == false && SpinSpeed == 0.f)
	{
		SetActorTickEnabled(false);
	}
}

void AFacilityFan::OnFacilityStateChanged()
{
	Super::OnFacilityStateChanged();

	// A reset starts a new attempt, so the blades are put at speed or at rest rather than spun there.
	const UFacilityStateSubsystem* Facility = FindFacility();
	ReconcileRunning(Facility != nullptr && Facility->IsResetting());
	RefreshLight();
}

void AFacilityFan::RefreshDisplayData()
{
	if (IsRunning())
	{
		Interactable->SetDisplayData(LOCTEXT("Running", "Fan running"), false);
		return;
	}

	// The rules only stop a fan for power today. The fallback keeps the prompt honest if that changes.
	Interactable->SetDisplayData(IsPowered() ? LOCTEXT("Stopped", "Fan stopped") : DescribeNoPower(GetCircuit()), false);
}

bool AFacilityFan::IsRunning() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->AreFansRunning(DeviceId);
}

void AFacilityFan::HandleKillVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Asked at the moment of entry, never remembered: the facility state alone decides whether entering kills.
	if (IsRunning())
	{
		Kill(Cast<APawn>(OtherActor));
	}
}

void AFacilityFan::ReconcileRunning(const bool bInstant)
{
	const bool bRunning = IsRunning();
	if (bInstant == false && bRunning == bLastReportedRunning)
	{
		return;
	}

	// Recorded before anything reacts. A kill can change the facility, a death resetting it, and the
	// nested state change has to compare against this value, not a stale one.
	bLastReportedRunning = bRunning;

	if (bInstant)
	{
		SpinSpeed = bRunning ? MaxSpinSpeed : 0.f;
	}

	// Blades at rest on a stopped fan have nothing to do. Anything else turns or slows down every frame.
	SetActorTickEnabled(bRunning || SpinSpeed != 0.f);

	OnRunningChanged(bRunning, bInstant);
	ReceiveRunningChanged(bRunning, bInstant);

	// Starting on someone is what makes the fans a weapon. A snap is not the fan starting but the level
	// beginning or a new attempt, and a reset is usually a death's doing: killing from inside it would
	// catch the same victim again.
	if (bRunning && bInstant == false)
	{
		KillPawnsInside();
	}
}

void AFacilityFan::KillPawnsInside()
{
	TArray<AActor*> Inside;
	KillVolume->GetOverlappingActors(Inside, APawn::StaticClass());

	for (AActor* Actor : Inside)
	{
		// A kill can change the facility, so the fan asks again before each one.
		if (IsRunning() == false)
		{
			return;
		}

		Kill(Cast<APawn>(Actor));
	}
}

void AFacilityFan::Kill(APawn* Victim)
{
	if (IsValid(Victim) == false || Victim->IsActorBeingDestroyed())
	{
		return;
	}

	UE_LOG(LogScifiSimEscape, Log, TEXT("%s (%s) caught %s while running."),
		*GetName(), *DeviceId.ToString(), *Victim->GetName());

	UGameplayStatics::ApplyDamage(Victim, KillDamage, nullptr, this, nullptr);
}

void AFacilityFan::RefreshLight()
{
	// Asked of the lights zone, so the fan follows its room's lights wherever they are routed. A fan with
	// no zone sees a dark room and keeps its own light on.
	const UFacilityStateSubsystem* Facility = FindFacility();
	const bool bRoomLit = Facility != nullptr && RoomLightsId.IsNone() == false && Facility->AreLightsOn(RoomLightsId);

	Light->SetVisibility(bRoomLit == false);
}

#undef LOCTEXT_NAMESPACE
