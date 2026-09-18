#include "FacilityDevices/Ladder/FacilityLadder.h"

#include "Climbing/ClimbComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "InteractionSystem/InteractableComponent.h"

#define LOCTEXT_NAMESPACE "FacilityLadder"

AFacilityLadder::AFacilityLadder()
{
	Top = CreateDefaultSubobject<USceneComponent>(TEXT("Top"));
	Top->SetupAttachment(Root);
	// One storey up, so a freshly placed ladder already reads as one. Moved per ladder in the level.
	Top->SetRelativeLocation(FVector(0.f, 0.f, 400.f));

	Rails = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Rails"));
	Rails->SetupAttachment(Root);
}

void AFacilityLadder::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Nobody is left hanging on a ladder that is going away.
	if (UClimbComponent* Current = Climber.Get())
	{
		Current->StopClimbing();
	}
	SetClimber(nullptr);

	Super::EndPlay(EndPlayReason);
}

bool AFacilityLadder::Use(AActor* User)
{
	UClimbComponent* Climb = User != nullptr ? User->FindComponentByClass<UClimbComponent>() : nullptr;
	if (Climb == nullptr)
	{
		return false;
	}

	if (Climb->IsClimbing())
	{
		// Only a climber on this ladder can let go of it here.
		if (Climb->GetLadder() != this)
		{
			return false;
		}

		Climb->StopClimbing();
		return true;
	}

	if (Climb->StartClimbing(this) == false)
	{
		return false;
	}

	SetClimber(Climb);
	return true;
}

FVector AFacilityLadder::GetBottomLocation() const
{
	return Root->GetComponentLocation();
}

FVector AFacilityLadder::GetTopLocation() const
{
	return Top->GetComponentLocation();
}

FVector AFacilityLadder::GetFacing() const
{
	return Root->GetForwardVector();
}

void AFacilityLadder::RefreshDisplayData()
{
	const bool bOccupied = Climber.IsValid();
	Interactable->SetDisplayData(bOccupied ? LOCTEXT("LetGo", "Let go") : LOCTEXT("Climb", "Climb"), true);
}

void AFacilityLadder::HandleClimbingChanged(const bool bClimbing)
{
	if (bClimbing == false)
	{
		SetClimber(nullptr);
	}
}

void AFacilityLadder::SetClimber(UClimbComponent* NewClimber)
{
	if (UClimbComponent* Old = Climber.Get())
	{
		Old->OnClimbingChanged.RemoveDynamic(this, &AFacilityLadder::HandleClimbingChanged);
	}

	Climber = NewClimber;

	if (NewClimber != nullptr)
	{
		NewClimber->OnClimbingChanged.AddDynamic(this, &AFacilityLadder::HandleClimbingChanged);
	}

	// Who is on the ladder is not facility state, so the prompt is refreshed by hand here.
	RefreshDisplayData();
}

#undef LOCTEXT_NAMESPACE
