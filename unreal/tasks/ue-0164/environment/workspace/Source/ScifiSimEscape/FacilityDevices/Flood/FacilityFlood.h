#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FacilityFlood.generated.h"

class APawn;
class UBoxComponent;
class UFacilityStateSubsystem;
class UPrimitiveComponent;
class UStaticMeshComponent;

/**
 * The water on the Plant floor. One actor covering the floor: a body of water that grows up out of
 * the floor while the facility says Plant is flooded and sinks back when it drains, and a volume
 * under the water's surface that kills whoever the water reaches while any circuit carries power,
 * since the water is conductive. It also names the fans that drain the water, so the flood in the
 * facility state knows which device to ask.
 *
 * The rise is the player's grace. In the state the flood is lethal the moment it arrives with a live
 * circuit, which is what the route solver needs to know; in the world the surface takes RiseSeconds
 * to climb from the floor to FloodHeight, and the kill volume climbs with it, so a player standing at
 * the valve has that long to get off the floor, up the ladder, onto the lift or out of the tunnel.
 *
 * Whether the floor is flooded and whether the water kills are FFacilityRules::IsPlantFlooded and
 * IsFloodLethal, asked at the moment the water reaches a pawn, or a pawn walks into the water, and
 * again on the edge where standing water turns lethal with someone already in it: a breaker thrown on
 * with the floor already wet. That is how the fans catch whoever is inside when they start. The actor
 * keeps no copy of either answer. What being caught means belongs to the pawn: KillDamage goes through
 * the engine's damage events.
 *
 * Not a facility actor: there is nothing to look at or use, so it has no Interactable, and it listens
 * to the subsystem on its own like AFacilityExit.
 *
 * Components: Root is where the actor is placed, at floor level. Water is the body of water: assign a
 * box mesh and set its X and Y scale and placement to cover the floor; the actor scales it on Z from
 * the floor up to the surface and places it, so leave its Z scale and Z location alone. A flat plane
 * works too and is moved to the surface instead. Surface is the water level, moved by the actor only.
 * KillVolume hangs under Surface with its top at the water, overlapping pawns and nothing else: size
 * its extents to the floor and the depth, and leave its location to the actor. In the editor the water
 * shows at FloodHeight, so the level reads as flooded while placing it.
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityFlood : public AActor
{
	GENERATED_BODY()

public:

	AFacilityFlood();

	/** True while the facility says water stands on the floor. False outside a running game. */
	UFUNCTION(BlueprintPure, Category = "Facility|Flood")
	bool IsFlooded() const;

	/** True while the facility says standing in the water kills. False outside a running game. */
	UFUNCTION(BlueprintPure, Category = "Facility|Flood")
	bool IsLethal() const;

	/** Where the water surface is right now, in cm above the floor. Negative while it waits under the floor. Presentation only. */
	UFUNCTION(BlueprintPure, Category = "Facility|Flood")
	float GetSurfaceHeight() const;

	/** True while the surface is rising or sinking. */
	UFUNCTION(BlueprintPure, Category = "Facility|Flood")
	bool IsSurfaceMoving() const { return bMoving; }

protected:

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * The water arrived or drained, as the facility has it: the surface starts rising or sinking now.
	 * Also called once from BeginPlay with the starting state, and on every reset; bInstant is true for
	 * those two and the surface is put in place rather than moved, so a rising-water sound belongs on
	 * the calls where it is false.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Flood", meta = (DisplayName = "On Flooded Changed"))
	void ReceiveFloodedChanged(bool bFlooded, bool bInstant);

	/**
	 * The water turned lethal or safe: sparks on, sparks off. Also called once from BeginPlay and on
	 * every reset, with bInstant. Fires before a newly lethal flood kills whoever already stands in it.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Flood", meta = (DisplayName = "On Lethal Changed"))
	void ReceiveLethalChanged(bool bLethal, bool bInstant);

	/** The surface finished rising or sinking and rests at the flood height or under the floor. Not called for the snap at BeginPlay or on a reset. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Flood", meta = (DisplayName = "On Surface Settled"))
	void ReceiveSurfaceSettled(bool bFlooded);

	//
	// Configuration
	//

	/** Device id of the fans that drain the water, the vent fans (Fan_Vent). Seeded into the facility state at BeginPlay. None leaves the water standing until a reset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Flood")
	FName FansId;

	/** How high the water stands above the floor once risen, in cm. Knee deep by default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Flood", meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float FloodHeight = 40.f;

	/**
	 * How far under the floor the surface waits while drained, in cm. A margin, so the kill volume's top
	 * sits under everyone's feet until the water starts rising; the water itself shows only above the floor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Flood", meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float DrainedDepth = 5.f;

	/** Seconds the surface takes to rise from the floor to FloodHeight, and to sink back. The player's time to get off the floor. Zero snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Flood", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float RiseSeconds = 8.f;

	/** Damage applied to a pawn the water catches. Meant to kill anything. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Flood", meta = (ClampMin = "0.0"))
	float KillDamage = 100000.f;

	//
	// Components
	//

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Flood")
	TObjectPtr<USceneComponent> Root;

	/** The water level. The kill volume hangs from it. Moved by the actor only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Flood")
	TObjectPtr<USceneComponent> Surface;

	/**
	 * The body of water. Assign a box mesh and set its X and Y scale and placement to cover the floor;
	 * the actor scales it on Z from the floor to the surface and places it, and hides it under the floor.
	 * A flat plane is moved to the surface instead. No collision.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Flood")
	TObjectPtr<UStaticMeshComponent> Water;

	/** What kills. Hangs under Surface with its top at the water; size its extents to the floor and the depth, and leave its location to the actor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Flood")
	TObjectPtr<UBoxComponent> KillVolume;

private:

	UFUNCTION()
	void HandleKillVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleFacilityStateChanged();

	/**
	 * Re-reads IsFlooded and IsLethal and reacts to whichever differs from what it last reacted to: the
	 * surface heads up or down, the hooks fire, and standing water that just turned lethal kills whoever
	 * is in it. With bInstant it always reacts, puts the surface straight in place and kills nobody: a
	 * snap is the level beginning or a new attempt.
	 */
	void Reconcile(bool bInstant);

	/** Puts the surface at the flood height or under the floor immediately, ending any move. */
	void SnapTo(bool bFlooded);

	/** Starts the surface toward the flood height or under the floor. Nothing happens if it is there or already on its way. */
	void MoveTo(bool bFlooded);

	/**
	 * Fits the water mesh to the surface: a solid mesh is scaled on Z to span from the floor to the
	 * surface, whatever its pivot; a flat one is moved to the surface. Hidden while the surface is
	 * under the floor. X and Y are left as authored.
	 */
	void ApplyWaterPose();

	/** Hangs the kill volume under the surface so its top is the water. */
	void HangKillVolume();

	float GetSurfaceTargetZ(bool bFlooded) const;

	/** Catches every pawn inside the volume, for as long as the water is still lethal. */
	void KillPawnsInside();

	/** Applies KillDamage to the pawn. Whether it dies, and what dying means, is the pawn's business. */
	void Kill(APawn* Victim);

	UFacilityStateSubsystem* FindFacility() const;

	/** Where the surface is heading, or rests. */
	bool bTargetFlooded = false;

	bool bMoving = false;

	/** What the actor last reacted to. They exist only to turn state changes into edges; the facility state is the answer. */
	bool bLastReportedFlooded = false;
	bool bLastReportedLethal = false;
};
