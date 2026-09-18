#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ClimbComponent.generated.h"

class AFacilityLadder;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnClimbingChanged, bool, bClimbing);

/**
 * Lets a character climb a straight path between two points: the ladder under the Security hatch
 * today, a dead lift shaft with the pry bar later. The path belongs to the thing being climbed; the
 * movement lives here so both use the same climb.
 *
 * While climbing, the character is held on the path at the ladder's stand-off from it, and forward
 * input moves it up and down at ClimbSpeed while every other movement input is ignored. Gravity is
 * off: the movement component sits in Flying and this component sets the location itself, with a
 * sweep, so a closed hatch stops the climb and opening it lets it continue. Climbing past the top
 * steps the character off onto the far side of the path; climbing below the bottom lets go. Jump
 * lets go anywhere.
 *
 * The owning character routes its forward input through AddClimbInput while IsClimbing.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SCIFISIMESCAPE_API UClimbComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UClimbComponent();

	/** Fires when a climb starts or ends, once the character is already in or out of it. */
	UPROPERTY(BlueprintAssignable, Category = "Climbing")
	FOnClimbingChanged OnClimbingChanged;

	/**
	 * Puts the character on the ladder at the point of the path nearest its feet, so stepping on
	 * from the floor above starts at the top. False when the owner is not a character, the ladder is
	 * null, or a climb is already running.
	 */
	UFUNCTION(BlueprintCallable, Category = "Climbing")
	bool StartClimbing(AFacilityLadder* Ladder);

	/** Lets go where the character is. Falling takes over. */
	UFUNCTION(BlueprintCallable, Category = "Climbing")
	void StopClimbing();

	UFUNCTION(BlueprintPure, Category = "Climbing")
	bool IsClimbing() const { return bClimbing; }

	/** What is being climbed, or null. */
	UFUNCTION(BlueprintPure, Category = "Climbing")
	AFacilityLadder* GetLadder() const { return CurrentLadder.Get(); }

	/** Forward input for this frame, -1 to 1. Positive climbs up. Consumed by the next tick. */
	UFUNCTION(BlueprintCallable, Category = "Climbing")
	void AddClimbInput(float Forward);

protected:

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** How fast the character moves along the path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climbing", meta = (ClampMin = "1.0", ForceUnits = "cm/s"))
	float ClimbSpeed = 150.f;

private:

	/** Where the capsule centre sits for a distance along the path, held off the path on the climber's side. */
	FVector GetCapsuleLocation(float Distance) const;

	/** How far along the path the character's feet are right now, from where it actually is. */
	float ReadDistance() const;

	/** Moves the character off the top of the path onto the far side and ends the climb. Stays on the path if something is in the way. */
	void StepOffTop();

	void EndClimb();

	/** How high the character is lifted before stepping over the top edge, so the capsule clears the floor. */
	static constexpr float ExitLift = 5.f;

	TWeakObjectPtr<AFacilityLadder> CurrentLadder;

	bool bClimbing = false;

	//
	// Path geometry, read from the ladder when the climb starts.
	//

	FVector PathBottom = FVector::ZeroVector;
	FVector PathAxis = FVector::UpVector;
	float PathLength = 0.f;

	/** From the path toward the climber. */
	FVector Facing = FVector::ForwardVector;
	float StandOff = 0.f;
	float ExitDistance = 0.f;

	float HalfHeight = 0.f;

	/** How far along the path the feet are, 0 at the bottom. */
	float Distance = 0.f;

	float PendingInput = 0.f;
};
