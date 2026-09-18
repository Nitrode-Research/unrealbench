#include "FacilityDevices/Hatch/FacilityHatch.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Facility/FacilityStateSubsystem.h"
#include "InteractionSystem/InteractableComponent.h"

#define LOCTEXT_NAMESPACE "FacilityHatch"

AFacilityHatch::AFacilityHatch()
{
	// Ticks only while the lid swings. SwingTo turns it on, settling turns it off.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(Root);

	Lid = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lid"));
	Lid->SetupAttachment(Hinge);
	Lid->Mobility = EComponentMobility::Movable;
}

void AFacilityHatch::BeginPlay()
{
	// Seed the door before the base begins play, so the first display refresh already sees it.
	// A standard door with no lock in the settings, which is what makes it mechanical and always usable.
	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		Facility->RegisterDoor(DoorId, EDoorKind::Standard, bInitiallyOpen);
	}

	// The hinge is authored closed. Read it before anything moves it.
	ClosedPose = Hinge->GetRelativeRotation().Quaternion();

	Super::BeginPlay();

	// Read back rather than trusting bInitiallyOpen: the state keeps a door the player opened across
	// the actor being destroyed and re-created.
	SnapTo(IsOpen());
}

void AFacilityHatch::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bMoving == false)
	{
		SetActorTickEnabled(false);
		return;
	}

	const FQuat Target = GetPose(bTargetOpen);
	const FQuat Current = Hinge->GetRelativeRotation().Quaternion();
	const FQuat Next = FMath::QInterpConstantTo(Current, Target, DeltaSeconds, FMath::DegreesToRadians(SwingSpeed));
	Hinge->SetRelativeRotation(Next);

	if (Next.Equals(Target))
	{
		bMoving = false;
		SetActorTickEnabled(false);

		OnLidSettled(bTargetOpen);
		ReceiveLidSettled(bTargetOpen);
	}
}

void AFacilityHatch::OnFacilityStateChanged()
{
	Super::OnFacilityStateChanged();

	// The state is the only thing that moves the lid. A reset starts a new attempt, so the lid is put
	// in its pose rather than swung there.
	const UFacilityStateSubsystem* Facility = FindFacility();
	if (Facility != nullptr && Facility->IsResetting())
	{
		SnapTo(IsOpen());
	}
	else
	{
		SwingTo(IsOpen());
	}
}

void AFacilityHatch::RefreshDisplayData()
{
	if (IsOpen())
	{
		Interactable->SetDisplayData(LOCTEXT("Close", "Close hatch"), true);
		return;
	}

	if (CanOpen() == false)
	{
		Interactable->SetDisplayData(LOCTEXT("Locked", "Locked"), false);
		return;
	}

	Interactable->SetDisplayData(LOCTEXT("Open", "Open hatch"), true);
}

bool AFacilityHatch::Toggle()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->ToggleDoor(DoorId);
}

bool AFacilityHatch::IsOpen() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr ? Facility->IsDoorOpen(DoorId) : bInitiallyOpen;
}

bool AFacilityHatch::CanOpen() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanOpenDoor(DoorId);
}

void AFacilityHatch::SnapTo(const bool bOpen)
{
	bTargetOpen = bOpen;
	bMoving = false;
	SetActorTickEnabled(false);

	Hinge->SetRelativeRotation(GetPose(bOpen));
}

void AFacilityHatch::SwingTo(const bool bOpen)
{
	if (bOpen == bTargetOpen)
	{
		return;
	}

	bTargetOpen = bOpen;
	bMoving = true;
	SetActorTickEnabled(true);
}

FQuat AFacilityHatch::GetPose(const bool bOpen) const
{
	// Right-hand side first: the delta is applied in the hinge's own frame, then the authored pose.
	return bOpen ? ClosedPose * OpenDelta.Quaternion() : ClosedPose;
}

#undef LOCTEXT_NAMESPACE
