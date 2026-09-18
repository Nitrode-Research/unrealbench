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

void AFacilityLift::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Show the car where it will start, so the level reads right without playing it.
	SnapCarTo(InitialStop);
}

void AFacilityLift::BeginPlay()
{
	// Seed where the car starts before the base registers the device and delivers the first state,
	// so anything reading the stop during Super::BeginPlay sees the placed value.
	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		FLiftState Lift;
		Lift.Stop = InitialStop;
		Lift.Kind = Kind;
		Facility->RegisterLift(DeviceId, Lift);
	}

	Super::BeginPlay();

	// Read the stop back rather than trusting InitialStop: the state keeps where a lift was called
	// to across the actor being destroyed and re-created.
	SnapCarTo(GetStop());
}

void AFacilityLift::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bMoving == false)
	{
		SetActorTickEnabled(false);
		return;
	}

	const FVector Target = GetStopComponent(TargetStop)->GetComponentLocation();
	const FVector NewLocation = FMath::VInterpConstantTo(Car->GetComponentLocation(), Target, DeltaSeconds, TravelSpeed);
	Car->SetWorldLocation(NewLocation);

	if (NewLocation.Equals(Target))
	{
		bMoving = false;
		SetActorTickEnabled(false);

		OnCarArrived(TargetStop);
		ReceiveCarArrived(TargetStop);
	}
}

void AFacilityLift::OnFacilityStateChanged()
{
	Super::OnFacilityStateChanged();

	// The state is the only thing that moves the car. Whatever changed, head for the stop it names;
	// most changes leave the car where it is. A reset starts a new attempt, so the car is put there
	// rather than driven.
	const UFacilityStateSubsystem* Facility = FindFacility();
	if (Facility != nullptr && Facility->IsResetting())
	{
		SnapCarTo(GetStop());
	}
	else
	{
		TravelTo(GetStop());
	}
}

bool AFacilityLift::Toggle()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	if (Facility == nullptr)
	{
		return false;
	}

	return Facility->CallLift(DeviceId, FFacilityRules::OtherLiftStop(Facility->GetLiftStop(DeviceId)));
}

ELiftStop AFacilityLift::GetStop() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr ? Facility->GetLiftStop(DeviceId) : InitialStop;
}

bool AFacilityLift::CanUse() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanUseLift(DeviceId);
}

ELiftObstacle AFacilityLift::GetObstacle() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr ? Facility->GetLiftObstacle(DeviceId) : ELiftObstacle::NoPower;
}

bool AFacilityLift::Hack()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->HackLift(DeviceId);
}

bool AFacilityLift::CanHack() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanHackLift(DeviceId);
}

void AFacilityLift::OnHacked(AActor* Hacker)
{
	Super::OnHacked(Hacker);
	Hack();
}

void AFacilityLift::SnapCarTo(const ELiftStop Stop)
{
	TargetStop = Stop;
	bMoving = false;
	SetActorTickEnabled(false);

	Car->SetWorldLocation(GetStopComponent(Stop)->GetComponentLocation());
}

void AFacilityLift::TravelTo(const ELiftStop Stop)
{
	if (Stop == TargetStop)
	{
		return;
	}

	TargetStop = Stop;
	bMoving = true;
	SetActorTickEnabled(true);
}

USceneComponent* AFacilityLift::GetStopComponent(const ELiftStop Stop) const
{
	return Stop == ELiftStop::ELS_Top ? TopStop : BottomStop;
}

void AFacilityLift::RefreshDisplayData()
{
	switch (GetObstacle())
	{
	case ELiftObstacle::Hacked:
		Interactable->SetDisplayData(LOCTEXT("Hacked", "Stopped by the handheld"), false);
		break;

	case ELiftObstacle::NoPower:
		Interactable->SetDisplayData(DescribeNoPower(GetCircuit()), false);
		break;

	case ELiftObstacle::Lockdown:
		Interactable->SetDisplayData(LOCTEXT("Lockdown", "Lockdown"), false);
		break;

	default:
	{
		const bool bAtBottom = GetStop() == ELiftStop::ELS_Bottom;
		Interactable->SetDisplayData(bAtBottom ? LOCTEXT("RideUp", "Ride up") : LOCTEXT("RideDown", "Ride down"), true);
		break;
	}
	}

	RefreshHackDisplay();
}

void AFacilityLift::RefreshHackDisplay()
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	const int32 HackLevel = GetHackLevel();
	if (Facility == nullptr || HackLevel <= 0)
	{
		// Nothing the handheld has business with. The line stays empty.
		Interactable->SetHackDisplayData(FText::GetEmpty(), false);
		return;
	}

	if (Facility->CanHackLift(DeviceId))
	{
		// A hack beyond the handheld can still be attempted. It fails, and the noise raises the alarm.
		const bool bSucceeds = Facility->WouldHackSucceed(HackLevel);
		Interactable->SetHackDisplayData(
			bSucceeds ? LOCTEXT("Hack", "Hack lift (stops it)") : LOCTEXT("HackFails", "Hack lift (will fail, loud)"), true);
		return;
	}

	// A hackable lift the handheld cannot reach right now. Power is the one reason worth a line of its
	// own: a lift it already stopped says so on the interact line.
	if (IsPowered() == false)
	{
		Interactable->SetHackDisplayData(DescribeNoPower(GetCircuit()), false);
		return;
	}

	Interactable->SetHackDisplayData(FText::GetEmpty(), false);
}

#undef LOCTEXT_NAMESPACE
