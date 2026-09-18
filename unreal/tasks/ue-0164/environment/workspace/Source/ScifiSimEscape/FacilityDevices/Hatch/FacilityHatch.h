#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityActorBase.h"
#include "FacilityHatch.generated.h"

class UStaticMeshComponent;

/**
 * The floor hatch in Security over the ladder down to Plant. Mechanical: it has no lock and no
 * power, so it is the one door in the facility that is always usable, and nothing about the
 * breakers, the alarm or lockdown ever touches it.
 *
 * Whether it is open is facility state, keyed by DoorId in the doors map, so a reset closes it again
 * and guards will find it as the player left it. Using it asks the subsystem to toggle the door;
 * this actor reads the answer back and swings the lid. It is the first door: the keycard doors that
 * follow use the same map with lock, seal and force added to the rules, and this class stays as it
 * is because CanOpenDoor already answers for it.
 *
 * Components: Root is where the actor is placed. Hinge is the scene component the lid swings about,
 * so put it on the edge the lid is hinged on. Lid is the mesh, offset from the hinge and authored in
 * the closed pose; opening turns the hinge by OpenDelta in its own frame.
 *
 * Whatever executes an interaction calls Toggle().
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityHatch : public AFacilityActorBase
{
	GENERATED_BODY()

public:

	AFacilityHatch();

	/** Opens a closed hatch or closes an open one through the facility state. False when the rules refused. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Hatch")
	bool Toggle();

	/** Open according to the facility state. The lid may still be swinging. */
	UFUNCTION(BlueprintPure, Category = "Facility|Hatch")
	bool IsOpen() const;

	/** True when the facility rules would let the hatch open right now. Always, for a mechanical hatch. */
	UFUNCTION(BlueprintPure, Category = "Facility|Hatch")
	bool CanOpen() const;

	/** True while the lid is between poses. */
	UFUNCTION(BlueprintPure, Category = "Facility|Hatch")
	bool IsMoving() const { return bMoving; }

	UFUNCTION(BlueprintPure, Category = "Facility|Hatch")
	FName GetDoorId() const { return DoorId; }

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnFacilityStateChanged() override;
	virtual void RefreshDisplayData() override;

	/** The lid finished swinging and rests open or closed. Not called for the snap at BeginPlay or on a reset. */
	virtual void OnLidSettled(bool bOpen) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Hatch", meta = (DisplayName = "On Lid Settled"))
	void ReceiveLidSettled(bool bOpen);

	//
	// Configuration
	//

	/** Key into the facility state's doors. Unique per door. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Hatch")
	FName DoorId;

	/** Whether the hatch is open when the level starts. Seeded into the facility state at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Hatch")
	bool bInitiallyOpen = false;

	/** How far the hinge turns from the closed pose to the open one, in the hinge's own frame. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Hatch")
	FRotator OpenDelta = FRotator(100.f, 0.f, 0.f);

	/** How fast the lid swings, in degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Hatch", meta = (ClampMin = "1.0"))
	float SwingSpeed = 180.f;

	//
	// Components
	//

	/** What the lid swings about. Place it on the hinged edge. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Hatch")
	TObjectPtr<USceneComponent> Hinge;

	/** The lid, offset from the hinge and authored closed. Needs collision: it is what keeps the player out of the hole. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Hatch")
	TObjectPtr<UStaticMeshComponent> Lid;

private:

	/** Puts the hinge in a pose immediately, ending any swing. */
	void SnapTo(bool bOpen);

	/** Starts the hinge swinging to a pose. Nothing happens if it is there or already on its way. */
	void SwingTo(bool bOpen);

	FQuat GetPose(bool bOpen) const;

	/** The hinge's authored rotation, read once at BeginPlay. Open is this turned by OpenDelta. */
	FQuat ClosedPose = FQuat::Identity;

	/** Pose the hinge is in, or heading for while bMoving. */
	bool bTargetOpen = false;

	bool bMoving = false;
};
