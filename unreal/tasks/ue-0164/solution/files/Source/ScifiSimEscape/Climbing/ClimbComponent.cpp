#include "Climbing/ClimbComponent.h"

#include "Components/CapsuleComponent.h"
#include "FacilityDevices/Ladder/FacilityLadder.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"

UClimbComponent::UClimbComponent()
{
	// Ticks only during a climb.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UClimbComponent::StartClimbing(AFacilityLadder* Ladder)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (Character == nullptr || Ladder == nullptr || bClimbing)
	{
		return false;
	}

	PathBottom = Ladder->GetBottomLocation();
	const FVector PathTop = Ladder->GetTopLocation();
	PathAxis = (PathTop - PathBottom).GetSafeNormal(UE_KINDA_SMALL_NUMBER, FVector::UpVector);
	PathLength = FVector::Dist(PathBottom, PathTop);
	Facing = Ladder->GetFacing();
	StandOff = Ladder->GetStandOff();
	ExitDistance = Ladder->GetExitDistance();
	HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	CurrentLadder = Ladder;
	bClimbing = true;
	PendingInput = 0.f;

	// Join the path at the point nearest the feet, so stepping on at the top starts at the top.
	Distance = ReadDistance();

	// Nobody climbs crouched.
	if (Character->bIsCrouched)
	{
		Character->UnCrouch();
	}

	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->StopMovementImmediately();
	Movement->SetMovementMode(MOVE_Flying);

	// The rungs are right in front of the climber; they must not block the climb or the step off the top.
	Character->MoveIgnoreActorAdd(Ladder);

	Character->SetActorLocation(GetCapsuleLocation(Distance), false, nullptr, ETeleportType::TeleportPhysics);

	// Turn to face the rungs. The player can still look around from there.
	if (AController* Controller = Character->GetController())
	{
		FRotator Control = Controller->GetControlRotation();
		Control.Yaw = (-Facing).Rotation().Yaw;
		Controller->SetControlRotation(Control);
	}

	SetComponentTickEnabled(true);
	OnClimbingChanged.Broadcast(true);
	return true;
}

void UClimbComponent::StopClimbing()
{
	if (bClimbing == false)
	{
		return;
	}

	EndClimb();
}

void UClimbComponent::AddClimbInput(const float Forward)
{
	PendingInput += Forward;
}

void UClimbComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bClimbing == false)
	{
		SetComponentTickEnabled(false);
		return;
	}

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (Character == nullptr || CurrentLadder.IsValid() == false)
	{
		// The ladder went away under the climber.
		EndClimb();
		return;
	}

	const float Input = FMath::Clamp(PendingInput, -1.f, 1.f);
	PendingInput = 0.f;
	if (FMath::IsNearlyZero(Input))
	{
		return;
	}

	const float Wanted = Distance + Input * ClimbSpeed * DeltaTime;

	if (Wanted > PathLength)
	{
		StepOffTop();
		return;
	}

	if (Wanted < 0.f)
	{
		EndClimb();
		return;
	}

	// Swept, so a closed hatch or a ceiling stops the climb instead of being passed through. The
	// distance is then read back from wherever the sweep left the character.
	FHitResult Hit;
	Character->SetActorLocation(GetCapsuleLocation(Wanted), true, &Hit);
	Distance = ReadDistance();
}

FVector UClimbComponent::GetCapsuleLocation(const float InDistance) const
{
	return PathBottom + PathAxis * InDistance + Facing * StandOff + FVector::UpVector * HalfHeight;
}

float UClimbComponent::ReadDistance() const
{
	const AActor* Owner = GetOwner();
	const FVector Feet = Owner->GetActorLocation() - FVector::UpVector * HalfHeight;
	return FMath::Clamp(FVector::DotProduct(Feet - PathBottom, PathAxis), 0.f, PathLength);
}

void UClimbComponent::StepOffTop()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	const FVector TopOfPath = GetCapsuleLocation(PathLength);

	// Up first so the capsule clears the floor edge, then across the path onto the far side. Either
	// sweep being blocked means something is in the way, and the climber stays at the top.
	FHitResult Hit;
	const FVector Lifted = TopOfPath + FVector::UpVector * ExitLift;
	Character->SetActorLocation(Lifted, true, &Hit);
	if (Hit.bBlockingHit)
	{
		Character->SetActorLocation(TopOfPath, false, nullptr, ETeleportType::TeleportPhysics);
		Distance = PathLength;
		return;
	}

	const FVector Across = Lifted - Facing * (StandOff + ExitDistance);
	Character->SetActorLocation(Across, true, &Hit);
	if (Hit.bBlockingHit)
	{
		Character->SetActorLocation(TopOfPath, false, nullptr, ETeleportType::TeleportPhysics);
		Distance = PathLength;
		return;
	}

	EndClimb();
}

void UClimbComponent::EndClimb()
{
	bClimbing = false;
	PendingInput = 0.f;
	SetComponentTickEnabled(false);

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (AFacilityLadder* Ladder = CurrentLadder.Get())
		{
			Character->MoveIgnoreActorRemove(Ladder);
		}

		// Falling finds the floor by itself, whether the character is on one or has let go mid-way.
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	}

	CurrentLadder = nullptr;
	OnClimbingChanged.Broadcast(false);
}
