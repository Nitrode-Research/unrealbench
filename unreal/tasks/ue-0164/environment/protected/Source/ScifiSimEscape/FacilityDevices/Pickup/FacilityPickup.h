#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityActorBase.h"
#include "Facility/FacilityTypes.h"
#include "FacilityPickup.generated.h"

class UStaticMeshComponent;

/**
 * An item lying in the world for the player to take: the pry bar on the Plant tool rack, the firearm
 * in the weapons locker, a keycard placed for testing where a guard would drop it. What lies here is
 * an EFacilityItem, named by the facility settings (DefaultGame.ini, Pickups) for this PickupId: the
 * item list is tuning, and this actor is only the place. A pickup the settings do not name holds nothing.
 *
 * Whether it has been taken is facility state, keyed by PickupId, and what the player carries is the
 * state's inventory. Using the pickup asks the subsystem to take it; this actor reads back whether it
 * is taken and hides itself, collision included, so the Interactor lets go of it. A reset puts every
 * pickup back where it lay and empties the player's hands. Nothing is spawned or destroyed, which is
 * what lets a death undo a pickup the same way it undoes a thrown breaker.
 *
 * Components: Root is where the actor is placed. Mesh is the item as it lies; it needs collision for
 * the Interactor to find it, and the mesh itself is assigned in the Blueprint or on the instance.
 *
 * Whatever executes an interaction calls Take().
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityPickup : public AFacilityActorBase
{
	GENERATED_BODY()

public:

	AFacilityPickup();

	/** Takes the item through the facility state. False when there was nothing to take. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Pickup")
	bool Take();

	/** True once taken, according to the facility state. */
	UFUNCTION(BlueprintPure, Category = "Facility|Pickup")
	bool IsTaken() const;

	/** What lies here, as the facility state has it from the settings. None outside a running game. */
	UFUNCTION(BlueprintPure, Category = "Facility|Pickup")
	EFacilityItem GetItem() const;

	UFUNCTION(BlueprintPure, Category = "Facility|Pickup")
	FName GetPickupId() const { return PickupId; }

	/**
	 * The item's name as prompts and dialogue use it: "pry bar", "level 2 keycard". Static so anything
	 * that talks about an item, the HUD or the neutral character's lines, says it the same way.
	 */
	UFUNCTION(BlueprintPure, Category = "Facility|Pickup")
	static FText DescribeItem(EFacilityItem Item);

protected:

	virtual void BeginPlay() override;
	virtual void OnFacilityStateChanged() override;
	virtual void RefreshDisplayData() override;

	/**
	 * The pickup was taken, or came back. Also called once from BeginPlay with the starting state, and
	 * on every reset; bInstant is true for those two, so a pickup sound belongs on the calls where it
	 * is false. The actor is already hidden or shown when this fires.
	 */
	virtual void OnTakenChanged(bool bTaken, bool bInstant) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Pickup", meta = (DisplayName = "On Taken Changed"))
	void ReceiveTakenChanged(bool bTaken, bool bInstant);

	//
	// Configuration
	//

	/** Key into the facility state's pickups, and into the settings' Pickups table for what lies here. Unique per pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Pickup")
	FName PickupId;

	//
	// Components
	//

	/** The item as it lies in the world. Needs collision so the Interactor can find it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Pickup")
	TObjectPtr<UStaticMeshComponent> Mesh;

private:

	/**
	 * Re-reads IsTaken, shows or hides the actor to match, and tells the hooks when the answer flipped.
	 * bInstant tells them regardless.
	 */
	void ReconcileTaken(bool bInstant);

	/**
	 * What the hooks were last told. Exists only to turn state changes into edges. It is not whether
	 * the pickup is taken; IsTaken is, and that reads the facility state.
	 */
	bool bLastReportedTaken = false;
};
