#include "Climbing/ClimbComponent.h"

#include "FacilityDevices/Ladder/FacilityLadder.h"

UClimbComponent::UClimbComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UClimbComponent::StartClimbing(AFacilityLadder* Ladder)
{
	(void)Ladder;
	return false;
}

void UClimbComponent::StopClimbing()
{
}

void UClimbComponent::AddClimbInput(const float Forward)
{
	(void)Forward;
}

void UClimbComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	PendingInput = 0.f;
	SetComponentTickEnabled(false);
}

FVector UClimbComponent::GetCapsuleLocation(const float InDistance) const
{
	(void)InDistance;
	return FVector::ZeroVector;
}

float UClimbComponent::ReadDistance() const
{
	return 0.f;
}

void UClimbComponent::StepOffTop()
{
}

void UClimbComponent::EndClimb()
{
	bClimbing = false;
	PendingInput = 0.f;
	SetComponentTickEnabled(false);
	CurrentLadder = nullptr;
}
