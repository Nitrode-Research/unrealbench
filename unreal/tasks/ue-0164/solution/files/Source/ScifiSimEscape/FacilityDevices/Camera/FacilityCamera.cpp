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
	// Seed the camera before the base begins play, so the first display refresh already sees it.
	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		if (LightsId.IsNone())
		{
			UE_LOG(LogScifiSimEscape, Warning, TEXT("%s (%s) names no LightsId. A camera with no lights to see by sees nothing."),
				*GetName(), *DeviceId.ToString());
		}

		FCameraState Camera;
		Camera.LightsId = LightsId;
		Facility->RegisterCamera(DeviceId, Camera);
	}

	View->OnComponentBeginOverlap.AddDynamic(this, &AFacilityCamera::HandleViewBeginOverlap);

	// The housing is authored looking where the pan starts from. Read it before anything turns it, and
	// put the pan at its start before the base delivers power and starts the tick.
	HousingRestPose = Housing->GetRelativeRotation().Quaternion();
	ResetPan();

	Super::BeginPlay();

	// Start as the facility says, without reporting anyone already standing in view.
	ReconcileWatching(true);
}

void AFacilityCamera::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	View->OnComponentBeginOverlap.RemoveDynamic(this, &AFacilityCamera::HandleViewBeginOverlap);

	Super::EndPlay(EndPlayReason);
}

void AFacilityCamera::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Low = FMath::Min(StartYaw, MaxYaw);
	const float High = FMath::Max(StartYaw, MaxYaw);
	if (TurnSpeed <= 0.f || High - Low <= KINDA_SMALL_NUMBER)
	{
		// Nothing to sweep. Held where it is until the settings say otherwise.
		SetActorTickEnabled(false);
		return;
	}

	// Back and forth between the two ends, turning around at each.
	PanYaw += PanDirection * TurnSpeed * DeltaSeconds;
	if (PanYaw >= High)
	{
		PanYaw = High;
		PanDirection = -1.f;
	}
	else if (PanYaw <= Low)
	{
		PanYaw = Low;
		PanDirection = 1.f;
	}

	ApplyPan();
}

void AFacilityCamera::OnPowerChanged(const bool bPowered)
{
	Super::OnPowerChanged(bPowered);

	// The motor runs on the camera's circuit. Unpowered, the housing freezes where it is.
	SetActorTickEnabled(bPowered && TurnSpeed > 0.f);
}

void AFacilityCamera::OnFacilityStateChanged()
{
	Super::OnFacilityStateChanged();

	// A reset starts a new attempt, so the camera is put in its state rather than reacting to it, and
	// the housing goes back to where the pan starts.
	const UFacilityStateSubsystem* Facility = FindFacility();
	const bool bResetting = Facility != nullptr && Facility->IsResetting();
	if (bResetting)
	{
		ResetPan();
	}
	ReconcileWatching(bResetting);
}

void AFacilityCamera::RefreshDisplayData()
{
	const UFacilityStateSubsystem* Facility = FindFacility();

	// The interact line: there is nothing to use by hand, so it says what the camera is doing.
	if (IsWatching())
	{
		Interactable->SetDisplayData(LOCTEXT("Watching", "Camera watching"), false);
	}
	else if (IsLooped())
	{
		Interactable->SetDisplayData(LOCTEXT("Looped", "Camera looped"), false);
	}
	else if (Facility != nullptr && Facility->IsServerDrivePulled())
	{
		Interactable->SetDisplayData(LOCTEXT("Blinded", "Camera blinded"), false);
	}
	else if (IsPowered() == false)
	{
		Interactable->SetDisplayData(DescribeNoPower(GetCircuit()), false);
	}
	else
	{
		Interactable->SetDisplayData(LOCTEXT("Dark", "Camera in the dark"), false);
	}

	// The hack line.
	const int32 HackLevel = GetHackLevel();
	if (Facility == nullptr || HackLevel <= 0)
	{
		Interactable->SetHackDisplayData(FText::GetEmpty(), false);
	}
	else if (Facility->CanHackCamera(DeviceId))
	{
		// A hack beyond the handheld can still be attempted. It fails, and the noise raises the alarm.
		const bool bSucceeds = Facility->WouldHackSucceed(HackLevel);
		Interactable->SetHackDisplayData(
			bSucceeds ? LOCTEXT("Hack", "Hack camera (loops it)") : LOCTEXT("HackFails", "Hack camera (will fail, loud)"), true);
	}
	else if (IsPowered() == false)
	{
		Interactable->SetHackDisplayData(DescribeNoPower(GetCircuit()), false);
	}
	else
	{
		// Looped already. The interact line says so.
		Interactable->SetHackDisplayData(FText::GetEmpty(), false);
	}
}

void AFacilityCamera::OnHacked(AActor* Hacker)
{
	Super::OnHacked(Hacker);
	Hack();
}

bool AFacilityCamera::IsWatching() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->IsCameraWatching(DeviceId);
}

bool AFacilityCamera::IsLooped() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->IsCameraLooped(DeviceId);
}

bool AFacilityCamera::Hack()
{
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->HackCamera(DeviceId);
}

bool AFacilityCamera::CanHack() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanHackCamera(DeviceId);
}

void AFacilityCamera::HandleViewBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Asked at the moment of entry, never remembered: the facility state alone decides whether the camera sees.
	if (IsPlayerPawn(OtherActor) == false || IsWatching() == false)
	{
		return;
	}

	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		Facility->CameraSighting(DeviceId);
	}
}

void AFacilityCamera::ReconcileWatching(const bool bInstant)
{
	const bool bWatching = IsWatching();
	if (bInstant == false && bWatching == bLastReportedWatching)
	{
		return;
	}

	// Recorded before anything reacts. A sighting raises the alarm, and the nested state change has to
	// compare against this value, not a stale one.
	bLastReportedWatching = bWatching;

	OnWatchingChanged(bWatching, bInstant);
	ReceiveWatchingChanged(bWatching, bInstant);

	// A camera that starts seeing with the player already in view sees them there and then, as when the
	// lights come back on. A snap is the level beginning or a new attempt, and reports nobody.
	if (bWatching && bInstant == false)
	{
		ReportPlayerInside();
	}
}

void AFacilityCamera::ReportPlayerInside()
{
	TArray<AActor*> Inside;
	View->GetOverlappingActors(Inside, APawn::StaticClass());

	for (const AActor* Actor : Inside)
	{
		if (IsPlayerPawn(Actor))
		{
			if (UFacilityStateSubsystem* Facility = FindFacility())
			{
				Facility->CameraSighting(DeviceId);
			}
			return;
		}
	}
}

void AFacilityCamera::ResetPan()
{
	// At the start, heading for the far end, whichever way round the two are.
	PanYaw = StartYaw;
	PanDirection = MaxYaw >= StartYaw ? 1.f : -1.f;
	ApplyPan();
}

void AFacilityCamera::ApplyPan()
{
	// Right-hand side first: the authored pose, then the pan about the mount's up axis, so a housing
	// authored tilted down keeps its tilt as it turns.
	Housing->SetRelativeRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(PanYaw)) * HousingRestPose);
}

#undef LOCTEXT_NAMESPACE
