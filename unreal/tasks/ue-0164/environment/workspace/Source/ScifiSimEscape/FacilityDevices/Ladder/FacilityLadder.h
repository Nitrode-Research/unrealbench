#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityActorBase.h"
#include "FacilityLadder.generated.h"

class UClimbComponent;
class UStaticMeshComponent;

/**
 * The ladder from Plant up to the Security hatch. Mechanical, never locked, and not in the facility
 * state at all: it is a path the player climbs. The climbing itself lives in the character's
 * UClimbComponent, so a dead lift shaft can offer the same climb later with a different path.
 *
 * Root is the bottom of the path, with X pointing toward the climber, away from the rungs. Top is
 * the top of the path. Both are where the climber's feet are, so put Root on the floor below and
 * Top level with the floor above. Rails is the mesh; it needs collision so the Interactor can see
 * the ladder, and the climber ignores it while on the ladder.
 *
 * Whatever executes an interaction calls Use() with the actor that pressed.
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityLadder : public AFacilityActorBase
{
	GENERATED_BODY()

public:

	AFacilityLadder();

	/**
	 * Puts the user on the ladder, or lets them go if they are already climbing it. False when the
	 * user cannot climb, which today means it has no UClimbComponent, or is climbing something else.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Ladder")
	bool Use(AActor* User);

	/** Bottom of the path: where the feet are on the lowest rung. */
	UFUNCTION(BlueprintPure, Category = "Facility|Ladder")
	FVector GetBottomLocation() const;

	/** Top of the path: where the feet are when stepping off. */
	UFUNCTION(BlueprintPure, Category = "Facility|Ladder")
	FVector GetTopLocation() const;

	/** Direction from the rungs toward the climber. Keep it horizontal. */
	UFUNCTION(BlueprintPure, Category = "Facility|Ladder")
	FVector GetFacing() const;

	UFUNCTION(BlueprintPure, Category = "Facility|Ladder")
	float GetStandOff() const { return StandOff; }

	UFUNCTION(BlueprintPure, Category = "Facility|Ladder")
	float GetExitDistance() const { return ExitDistance; }

	/** Whoever is climbing right now, or null. */
	UFUNCTION(BlueprintPure, Category = "Facility|Ladder")
	UClimbComponent* GetClimber() const { return Climber.Get(); }

protected:

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void RefreshDisplayData() override;

	//
	// Configuration
	//

	/** How far in front of the rungs the climber's feet are held. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Ladder", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float StandOff = 45.f;

	/** How far past the rungs, on the far side, the climber is put down when stepping off the top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Ladder", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float ExitDistance = 60.f;

	//
	// Components
	//

	/** Top of the path. Drag it up to the floor above. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Ladder")
	TObjectPtr<USceneComponent> Top;

	/** The rungs and rails. Needs collision so the Interactor can see the ladder. The mesh is assigned in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Ladder")
	TObjectPtr<UStaticMeshComponent> Rails;

private:

	UFUNCTION()
	void HandleClimbingChanged(bool bClimbing);

	void SetClimber(UClimbComponent* NewClimber);

	TWeakObjectPtr<UClimbComponent> Climber;
};
