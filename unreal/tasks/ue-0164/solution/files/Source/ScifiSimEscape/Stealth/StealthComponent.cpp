#include "Stealth/StealthComponent.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "FacilityDevices/HidingSpot/FacilityHidingSpot.h"
#include "FacilityDevices/LightZone/FacilityLightZone.h"
#include "GameFramework/Character.h"
#include "ScifiSimEscape.h"

#define LOCTEXT_NAMESPACE "StealthComponent"

UStealthComponent::UStealthComponent()
{
	// Pace changes every frame, and the summary has to follow it.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UStealthComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	Owner->OnActorBeginOverlap.AddDynamic(this, &UStealthComponent::HandleOwnerBeginOverlap);
	Owner->OnActorEndOverlap.AddDynamic(this, &UStealthComponent::HandleOwnerEndOverlap);

	// Whatever the owner already stands in when play begins, or when the component is added mid-play.
	TArray<AActor*> Overlapping;
	Owner->GetOverlappingActors(Overlapping);
	for (AActor* Other : Overlapping)
	{
		Track(Other, true);
	}

	// A level with no zones at all leaves the player in the dark everywhere, which is rarely what was meant.
	if (TActorIterator<AFacilityLightZone> It(GetWorld()); !It)
	{
		UE_LOG(LogScifiSimEscape, Warning, TEXT("%s: the level has no AFacilityLightZone, so the player counts as in the dark everywhere. Place one per lit room."),
			*Owner->GetName());
	}

	LastReportedState = GetStealthState();
	UE_LOG(LogScifiSimEscape, Log, TEXT("%s starts %s (zone '%s')."),
		*Owner->GetName(), *DescribeStealthState(LastReportedState).ToString(), *GetLightZoneId().ToString());
}

void UStealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AActor* Owner = GetOwner())
	{
		Owner->OnActorEndOverlap.RemoveDynamic(this, &UStealthComponent::HandleOwnerEndOverlap);
		Owner->OnActorBeginOverlap.RemoveDynamic(this, &UStealthComponent::HandleOwnerBeginOverlap);
	}

	Super::EndPlay(EndPlayReason);
}

void UStealthComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Pace and the lights both move without an event of their own, so the summary is re-read every frame.
	Reconcile();
}

EStealthState UStealthComponent::GetStealthState() const
{
	if (IsHidden())
	{
		return EStealthState::Hidden;
	}

	if (IsInDarkness())
	{
		return EStealthState::Dark;
	}

	return IsSneaking() ? EStealthState::Sneaking : EStealthState::Exposed;
}

bool UStealthComponent::IsDetectable() const
{
	return GetStealthState() == EStealthState::Exposed;
}

bool UStealthComponent::IsHidden() const
{
	for (const TWeakObjectPtr<AFacilityHidingSpot>& Spot : HidingSpots)
	{
		if (Spot.IsValid() && (Spot->RequiresCrouch() == false || IsCrouched()))
		{
			return true;
		}
	}
	return false;
}

bool UStealthComponent::IsInDarkness() const
{
	// A lit zone anywhere underfoot lights the player; a doorway between a lit room and a dark one is lit.
	for (const TWeakObjectPtr<AFacilityLightZone>& Zone : Zones)
	{
		if (Zone.IsValid() && Zone->IsLit())
		{
			return false;
		}
	}
	return true;
}

bool UStealthComponent::IsSneaking() const
{
	return IsCrouched() || GetSpeed() <= SneakSpeed;
}

bool UStealthComponent::IsCrouched() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character != nullptr && Character->bIsCrouched;
}

float UStealthComponent::GetSpeed() const
{
	const AActor* Owner = GetOwner();
	return Owner != nullptr ? Owner->GetVelocity().Size2D() : 0.f;
}

FName UStealthComponent::GetLightZoneId() const
{
	FName First = NAME_None;
	for (const TWeakObjectPtr<AFacilityLightZone>& Zone : Zones)
	{
		if (Zone.IsValid() == false)
		{
			continue;
		}

		if (Zone->IsLit())
		{
			return Zone->GetLightsId();
		}

		if (First.IsNone())
		{
			First = Zone->GetLightsId();
		}
	}
	return First;
}

FText UStealthComponent::DescribeStealthState(const EStealthState State)
{
	switch (State)
	{
	case EStealthState::Hidden:
		return LOCTEXT("Hidden", "Hidden");

	case EStealthState::Dark:
		return LOCTEXT("Dark", "In the dark");

	case EStealthState::Sneaking:
		return LOCTEXT("Sneaking", "Sneaking");

	default:
		return LOCTEXT("Exposed", "Exposed");
	}
}

void UStealthComponent::HandleOwnerBeginOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	Track(OtherActor, true);
	Reconcile();
}

void UStealthComponent::HandleOwnerEndOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	Track(OtherActor, false);
	Reconcile();
}

void UStealthComponent::Track(AActor* Other, const bool bInside)
{
	if (AFacilityLightZone* Zone = Cast<AFacilityLightZone>(Other))
	{
		if (bInside)
		{
			Zones.AddUnique(Zone);
		}
		else
		{
			Zones.Remove(Zone);
		}
		return;
	}

	if (AFacilityHidingSpot* Spot = Cast<AFacilityHidingSpot>(Other))
	{
		if (bInside)
		{
			HidingSpots.AddUnique(Spot);
		}
		else
		{
			HidingSpots.Remove(Spot);
		}
	}
}

void UStealthComponent::Reconcile()
{
	const EStealthState State = GetStealthState();
	if (State == LastReportedState)
	{
		return;
	}

	// Recorded before the broadcast, so a listener that asks again compares against the new value.
	const EStealthState OldState = LastReportedState;
	LastReportedState = State;

	// Every start and stop flips this, so kept out of the default log level.
	UE_LOG(LogScifiSimEscape, Verbose, TEXT("%s is %s (was %s; zone '%s', %s, %.0f cm/s)."),
		*GetOwner()->GetName(), *DescribeStealthState(State).ToString(), *DescribeStealthState(OldState).ToString(),
		*GetLightZoneId().ToString(), IsCrouched() ? TEXT("crouched") : TEXT("upright"), GetSpeed());

	OnStealthStateChanged.Broadcast(OldState, State);
}

#undef LOCTEXT_NAMESPACE
