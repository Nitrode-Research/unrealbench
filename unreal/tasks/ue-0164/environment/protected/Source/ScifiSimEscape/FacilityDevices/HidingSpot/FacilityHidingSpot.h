#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FacilityHidingSpot.generated.h"

class UBoxComponent;

/**
 * A place the player is not found: under the Security desk, inside a locker, behind the crates in
 * Storage. A volume. A player inside it is hidden from guards and their sightings whatever the lights
 * are doing, and with bRequiresCrouch only while crouched, for a spot with low cover. UStealthComponent
 * on the player keeps track of the spots it overlaps and answers IsHidden from them.
 *
 * Keep spots out of the camera's view box: the camera does not ask the stealth component, so a spot
 * in its view hides from nothing.
 *
 * Not a facility actor: nothing to look at or use, and nothing in the facility state.
 *
 * Components: Root is where the actor is placed. Volume is the box, overlapping pawns and nothing
 * else; size it to the cover.
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityHidingSpot : public AActor
{
	GENERATED_BODY()

public:

	AFacilityHidingSpot();

	/** True when the spot only hides a crouched player. */
	UFUNCTION(BlueprintPure, Category = "Facility|Hiding Spot")
	bool RequiresCrouch() const { return bRequiresCrouch; }

protected:

	//
	// Configuration
	//

	/** Whether the spot hides only a crouched player, as under a desk. Off, standing inside is enough, as in a locker. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Hiding Spot")
	bool bRequiresCrouch = false;

	//
	// Components
	//

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Hiding Spot")
	TObjectPtr<USceneComponent> Root;

	/** The cover. Overlaps pawns and nothing else. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Hiding Spot")
	TObjectPtr<UBoxComponent> Volume;
};
