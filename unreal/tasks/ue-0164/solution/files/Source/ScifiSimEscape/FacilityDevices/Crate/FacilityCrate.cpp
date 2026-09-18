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
	// Seed the crate before the base begins play, so the first display refresh already sees it.
	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		if (CrateId.IsNone())
		{
			UE_LOG(LogScifiSimEscape, Warning, TEXT("%s has no CrateId. It cannot register with the facility and will never move."), *GetName());
		}
		if (LiftId.IsNone())
		{
			UE_LOG(LogScifiSimEscape, Warning, TEXT("%s (%s) names no LiftId. Only the pry bar will move it, and nothing will drop it."), *GetName(), *CrateId.ToString());
		}

		Facility->RegisterCrate(CrateId, LiftId);
	}

	Super::BeginPlay();

	// Read back rather than assuming the shutter: the state keeps a crate the player moved across the
	// actor being destroyed and re-created.
	LastReportedPosition = GetPosition();
	bLastReportedDropped = WasDropped();
	SnapTo(LastReportedPosition);
	OnPositionChanged(LastReportedPosition, false, true);
	ReceivePositionChanged(LastReportedPosition, false, true);
}

void AFacilityCrate::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bMoving == false)
	{
		SetActorTickEnabled(false);
		return;
	}

	const FVector Target = GetPoseLocation(TargetPosition);
	const FVector Next = FMath::VInterpConstantTo(Body->GetComponentLocation(), Target, DeltaSeconds, SlideSpeed);
	Body->SetWorldLocation(Next);

	if (Next.Equals(Target))
	{
		bMoving = false;
		SetActorTickEnabled(false);

		ReceiveBodySettled(TargetPosition);
	}
}

void AFacilityCrate::OnFacilityStateChanged()
{
	Super::OnFacilityStateChanged();

	const UFacilityStateSubsystem* Facility = FindFacility();
	const bool bResetting = Facility != nullptr && Facility->IsResetting();

	const ECratePosition Position = GetPosition();
	const bool bDropped = WasDropped();

	// Recorded before anything reacts. A kill can change the facility, a death resetting it, and the
	// nested state change has to compare against these values, not stale ones.
	const bool bPositionChanged = Position != LastReportedPosition;
	const bool bDroppedNow = bDropped && bLastReportedDropped == false;
	LastReportedPosition = Position;
	bLastReportedDropped = bDropped;

	// The state is the only thing that moves the body. A reset starts a new attempt, so the body is put
	// at its pose rather than slid there; a drop falls, everything else is pushed or ridden.
	if (bResetting)
	{
		SnapTo(Position);
	}
	else
	{
		SlideTo(Position, bDroppedNow ? DropSpeed : MoveSpeed);
	}

	if (bResetting || bPositionChanged)
	{
		OnPositionChanged(Position, bDroppedNow && bResetting == false, bResetting);
		ReceivePositionChanged(Position, bDroppedNow && bResetting == false, bResetting);
	}

	// It lands on whatever stands below, the moment the state says it fell. A reset is a new attempt
	// and kills nobody.
	if (bDroppedNow && bResetting == false)
	{
		KillPawnsBelow();
	}
}

void AFacilityCrate::RefreshDisplayData()
{
	switch (GetPosition())
	{
	case ECratePosition::JammingShutter:
		if (CanMove())
		{
			Interactable->SetDisplayData(LOCTEXT("Move", "Move crate off the shutter"), true);
		}
		else
		{
			Interactable->SetDisplayData(LOCTEXT("Jamming", "Crate jams the shutter (needs the pry bar or the lift running)"), false);
		}
		return;

	case ECratePosition::AtShaftHead:
		if (CanDrop())
		{
			// The landing is loud, so the prompt says so before the press.
			Interactable->SetDisplayData(LOCTEXT("Drop", "Push crate down the shaft (loud)"), true);
		}
		else
		{
			// The car is up, so the crate sits on it. Riding the car down takes it along.
			Interactable->SetDisplayData(LOCTEXT("OnCar", "Crate on the lift"), false);
		}
		return;

	default:
		Interactable->SetDisplayData(WasDropped() ? LOCTEXT("Dropped", "Crate, dropped") : LOCTEXT("InPlant", "Crate, out of the way"), false);
		return;
	}
}

bool AFacilityCrate::Use()
{
	// The one verb does what the crate's position allows: down the shaft from the shaft head, off the shutter otherwise.
	return CanDrop() ? Drop() : Move();
}

bool AFacilityCrate::Move()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->MoveCrate(CrateId);
}

bool AFacilityCrate::Drop()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->DropCrate(CrateId);
}

ECratePosition AFacilityCrate::GetPosition() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr ? Facility->GetCratePosition(CrateId) : ECratePosition::JammingShutter;
}

bool AFacilityCrate::CanMove() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanMoveCrate(CrateId);
}

bool AFacilityCrate::CanDrop() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanDropCrate(CrateId);
}

bool AFacilityCrate::WasDropped() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->WasCrateDropped(CrateId);
}

void AFacilityCrate::SnapTo(const ECratePosition Position)
{
	TargetPosition = Position;
	bMoving = false;
	SetActorTickEnabled(false);

	Body->SetWorldLocation(GetPoseLocation(Position));
}

void AFacilityCrate::SlideTo(const ECratePosition Position, const float Speed)
{
	if (Position == TargetPosition)
	{
		return;
	}

	TargetPosition = Position;
	SlideSpeed = Speed;
	bMoving = true;
	SetActorTickEnabled(true);
}

FVector AFacilityCrate::GetPoseLocation(const ECratePosition Position) const
{
	switch (Position)
	{
	case ECratePosition::AtShaftHead:
		return ShaftHead->GetComponentLocation();

	case ECratePosition::InPlant:
		return Landing->GetComponentLocation();

	default:
		return Root->GetComponentLocation();
	}
}

void AFacilityCrate::KillPawnsBelow()
{
	TArray<AActor*> Inside;
	KillVolume->GetOverlappingActors(Inside, APawn::StaticClass());

	for (AActor* Actor : Inside)
	{
		Kill(Cast<APawn>(Actor));
	}
}

void AFacilityCrate::Kill(APawn* Victim)
{
	if (IsValid(Victim) == false || Victim->IsActorBeingDestroyed())
	{
		return;
	}

	UE_LOG(LogScifiSimEscape, Log, TEXT("%s (%s) landed on %s."), *GetName(), *CrateId.ToString(), *Victim->GetName());

	UGameplayStatics::ApplyDamage(Victim, KillDamage, nullptr, this, nullptr);
}

#undef LOCTEXT_NAMESPACE
