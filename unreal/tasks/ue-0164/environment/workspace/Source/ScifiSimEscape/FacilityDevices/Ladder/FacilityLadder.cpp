#include "FacilityDevices/Ladder/FacilityLadder.h"

#include "Climbing/ClimbComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"

AFacilityLadder::AFacilityLadder()
{
	Top = CreateDefaultSubobject<USceneComponent>(TEXT("Top"));
	Top->SetupAttachment(Root);
	Top->SetRelativeLocation(FVector(0.f, 0.f, 400.f));

	Rails = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Rails"));
	Rails->SetupAttachment(Root);
}

void AFacilityLadder::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

bool AFacilityLadder::Use(AActor* User)
{
	(void)User;
	return false;
}

FVector AFacilityLadder::GetBottomLocation() const
{
	return FVector::ZeroVector;
}

FVector AFacilityLadder::GetTopLocation() const
{
	return FVector::ZeroVector;
}

FVector AFacilityLadder::GetFacing() const
{
	return FVector::ForwardVector;
}

void AFacilityLadder::RefreshDisplayData()
{
}

void AFacilityLadder::HandleClimbingChanged(const bool bClimbing)
{
	(void)bClimbing;
}

void AFacilityLadder::SetClimber(UClimbComponent* NewClimber)
{
	(void)NewClimber;
}
