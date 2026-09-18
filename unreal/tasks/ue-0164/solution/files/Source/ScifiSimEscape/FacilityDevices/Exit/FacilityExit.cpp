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
	Super::BeginPlay();

	if (Exit == EFacilityExit::None)
	{
		UE_LOG(LogScifiSimEscape, Warning, TEXT("%s has no Exit set. Walking through it wins nothing."), *GetName());
	}

	Volume->OnComponentBeginOverlap.AddDynamic(this, &AFacilityExit::HandleVolumeBeginOverlap);

	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		// Seeded before anything asks, so the first escape check already knows what guards this exit.
		FExitState ExitState;
		ExitState.DoorId = DoorId;
		ExitState.FansId = FansId;
		Facility->RegisterExit(Exit, ExitState);

		if (Exit == EFacilityExit::ServiceTunnel && DoorId.IsNone())
		{
			UE_LOG(LogScifiSimEscape, Warning, TEXT("%s is the service tunnel but names no DoorId. The tunnel will never let the player through."), *GetName());
		}
		if (Exit == EFacilityExit::ServiceTunnel && FansId.IsNone())
		{
			UE_LOG(LogScifiSimEscape, Warning, TEXT("%s is the service tunnel but names no FansId. Running fans will not keep the player in."), *GetName());
		}

		Facility->OnStateChanged.AddDynamic(this, &AFacilityExit::HandleFacilityStateChanged);
	}
}

void AFacilityExit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UFacilityStateSubsystem* Facility = FindFacility())
	{
		Facility->OnStateChanged.RemoveDynamic(this, &AFacilityExit::HandleFacilityStateChanged);
	}

	Volume->OnComponentBeginOverlap.RemoveDynamic(this, &AFacilityExit::HandleVolumeBeginOverlap);

	Super::EndPlay(EndPlayReason);
}

void AFacilityExit::HandleVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (AFacilityActorBase::IsPlayerPawn(OtherActor) == false)
	{
		return;
	}

	// Logged on entry only. The re-check on every state change stays quiet, or standing on a closed
	// threshold would say so on every breaker flip.
	if (TryEscape() == false)
	{
		UE_LOG(LogScifiSimEscape, Log, TEXT("%s: player is on the %s threshold, but %s."),
			*GetName(), *UEnum::GetDisplayValueAsText(Exit).ToString(), *DescribeWhyShut());
	}
}

void AFacilityExit::HandleFacilityStateChanged()
{
	// The exit may have just opened with the player already standing on the threshold.
	TArray<AActor*> Inside;
	Volume->GetOverlappingActors(Inside, APawn::StaticClass());

	for (const AActor* Actor : Inside)
	{
		if (AFacilityActorBase::IsPlayerPawn(Actor))
		{
			TryEscape();
			return;
		}
	}
}

bool AFacilityExit::TryEscape()
{
	// Asked first so a closed exit is not an Escape refusal in the log. The rules alone decide.
	UFacilityStateSubsystem* Facility = FindFacility();
	return Facility != nullptr && Facility->CanEscape(Exit) && Facility->Escape(Exit);
}

FString AFacilityExit::DescribeWhyShut() const
{
	const UFacilityStateSubsystem* Facility = FindFacility();
	if (Facility == nullptr)
	{
		return TEXT("there is no facility state in this world");
	}

	if (Facility->HasEscaped())
	{
		return TEXT("the player has already escaped");
	}

	switch (Exit)
	{
	case EFacilityExit::MainGate:
	{
		const FName GateId = FFacilityRules::FindDoorOfKind(Facility->GetState(), EDoorKind::Gate);
		if (GateId.IsNone())
		{
			return TEXT("no door of kind Main gate has registered, so nothing can open");
		}

		return FString::Printf(TEXT("the gate door '%s' is closed"), *GateId.ToString());
	}

	case EFacilityExit::ServiceTunnel:
	{
		const FExitState* Tunnel = FFacilityRules::FindExit(Facility->GetState(), Exit);
		if (Tunnel == nullptr || Tunnel->DoorId.IsNone())
		{
			return TEXT("no tunnel door is named for it, so nothing can open");
		}

		if (Facility->IsDoorOpen(Tunnel->DoorId) == false)
		{
			return FString::Printf(TEXT("the tunnel door '%s' is closed"), *Tunnel->DoorId.ToString());
		}

		return FString::Printf(TEXT("the fans '%s' are running"), *Tunnel->FansId.ToString());
	}

	default:
		return TEXT("that exit is not in the rules yet");
	}
}

UFacilityStateSubsystem* AFacilityExit::FindFacility() const
{
	return UWorld::GetSubsystem<UFacilityStateSubsystem>(GetWorld());
}
