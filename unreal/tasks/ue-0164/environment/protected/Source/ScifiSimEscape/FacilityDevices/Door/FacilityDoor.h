#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityDeviceBase.h"
#include "Facility/FacilityTypes.h"
#include "FacilityDoor.generated.h"

class UStaticMeshComponent;

/**
 * A door that slides open. The two lockdown doors, the main gate, the two lockers and the service
 * tunnel door are this one class placed with different settings. A locker is a door on no circuit,
 * which makes it mechanical, and so is the tunnel door, locked with the service key instead of a card.
 * It is a device because most doors run on DOORS, and being on a circuit is what makes a door need
 * power.
 *
 * Everything about the door that matters to play is in the facility state under the device id: open
 * or closed, whether the pry bar forced it, the kind this actor seeds when it registers, and the lock
 * the facility settings give the id (DefaultGame.ini, DoorLocks), since a lock is tuning and not placement.
 * Using the door asks the subsystem to open, close or force it, the rules decide, and this actor reads
 * the answer back and moves the panel. So the door, its prompt, the circuits that seal it and the
 * route solver never disagree, and a reset puts it back with everything else. Whether the door moves
 * is FFacilityRules::GetDoorObstacle, and the prompt shows the reason it gives, or offers the pry bar
 * when FFacilityRules::CanForceDoor says it gets past that reason. The handheld gets its own line: a
 * door the settings give a hack level offers a hack while only its lock holds it, FFacilityRules::CanHackDoor, and
 * says when the hack is beyond the handheld and would only make noise.
 *
 * Components: Root is where the actor is placed. Frame is the casing around the opening; it has
 * collision and nothing else. Panel is the leaf, authored closed; opening moves it by OpenOffset in
 * the door's own frame, straight up by default. Both meshes are assigned in the Blueprint. A closing
 * panel moves through whatever stands in the opening, as the hatch lid does.
 *
 * Whatever executes an interaction calls Use(). The hack press arrives through OnHacked and calls Hack().
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityDoor : public AFacilityDeviceBase
{
	GENERATED_BODY()

public:

	AFacilityDoor();

	/**
	 * What pressing interact on the door does. Opens or closes it when nothing holds it, and pries it open
	 * with the pry bar when a seal, a dead circuit or the lock holds it and the player carries one, which
	 * is loud. False when neither happened.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Door")
	bool Use();

	/** Opens a closed door or closes an open one through the facility state, never by force. False when the rules refused. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Door")
	bool Toggle();

	/** True when the pry bar would force the door open right now, which is what the prompt offers then. */
	UFUNCTION(BlueprintPure, Category = "Facility|Door")
	bool CanForce() const;

	/**
	 * Works the handheld on the door through the facility state. A hack the handheld reaches opens the
	 * door silently; one it cannot reach fails and raises the alarm. False when the handheld had nothing
	 * to work on.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Door")
	bool Hack();

	/** True when the handheld has something to work on here, whether or not it would succeed. */
	UFUNCTION(BlueprintPure, Category = "Facility|Door")
	bool CanHack() const;

	/** Open according to the facility state. The panel may still be moving. */
	UFUNCTION(BlueprintPure, Category = "Facility|Door")
	bool IsOpen() const;

	/** Why the door will not move right now, None when it will. What the prompt shows. */
	UFUNCTION(BlueprintPure, Category = "Facility|Door")
	EDoorObstacle GetObstacle() const;

	/** True while the panel is between poses. */
	UFUNCTION(BlueprintPure, Category = "Facility|Door")
	bool IsMoving() const { return bMoving; }

	/** Keycard level the lock takes, from the facility settings. 0 for none, or outside a running game. */
	UFUNCTION(BlueprintPure, Category = "Facility|Door")
	int32 GetLockLevel() const;

	/** Item the lock takes, from the facility settings. None for none, or outside a running game. */
	UFUNCTION(BlueprintPure, Category = "Facility|Door")
	EFacilityItem GetKeyItem() const;

	UFUNCTION(BlueprintPure, Category = "Facility|Door")
	EDoorKind GetKind() const { return Kind; }

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnFacilityStateChanged() override;
	virtual void RefreshDisplayData() override;
	virtual void OnHacked(AActor* Hacker) override;
	virtual bool NeedsCircuit() const override;

	/** The panel finished moving and rests open or closed. Not called for the snap at BeginPlay or on a reset. */
	virtual void OnPanelSettled(bool bOpen) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Door", meta = (DisplayName = "On Panel Settled"))
	void ReceivePanelSettled(bool bOpen);

	//
	// Configuration
	//

	/** What the prompt calls it: "Open door", "Open locker", "Open gate". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Door")
	FText DisplayName;

	/** What the door does when its circuit dies or lockdown is called. Seeded into the facility state at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Door")
	EDoorKind Kind = EDoorKind::Standard;

	//
	// Not here: the lock. What the door takes, a keycard level, a key item or both, is tuning, so it
	// lives in the facility settings (DefaultGame.ini, DoorLocks) keyed by DeviceId, and the subsystem
	// reads it when the door registers. A door the settings do not name has no lock.
	//

	/** Whether the door is open when the level starts. Seeded into the facility state at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Door")
	bool bInitiallyOpen = false;

	/** How far the panel moves from the closed pose to the open one, in the door's own frame. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Door")
	FVector OpenOffset = FVector(0.f, 0.f, 200.f);

	/** How fast the panel moves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Door", meta = (ClampMin = "1.0", ForceUnits = "cm/s"))
	float SlideSpeed = 200.f;

	//
	// Components
	//

	/** The casing around the opening. Collision only; it never moves and never does anything. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Door")
	TObjectPtr<UStaticMeshComponent> Frame;

	/** The leaf, authored closed. Needs collision: it is what keeps the player out. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Door")
	TObjectPtr<UStaticMeshComponent> Panel;

private:

	/** Puts the panel in a pose immediately, ending any move. */
	void SnapTo(bool bOpen);

	/** Starts the panel toward a pose. Nothing happens if it is there or already on its way. */
	void SlideTo(bool bOpen);

	FVector GetPose(bool bOpen) const;

	/** What the lock wants, for the prompt: "Needs a level 2 keycard", "Needs the service key". */
	FText DescribeLock() const;

	/** The interact line: what a press does to the door, or why nothing happens. */
	void RefreshInteractDisplay();

	/** The hack line: what the handheld does to the door, or nothing for a door it has no business with. */
	void RefreshHackDisplay();

	/** The panel's authored location, read once at BeginPlay. Open is this moved by OpenOffset. */
	FVector ClosedPose = FVector::ZeroVector;

	/** Pose the panel is in, or heading for while bMoving. */
	bool bTargetOpen = false;

	bool bMoving = false;
};
