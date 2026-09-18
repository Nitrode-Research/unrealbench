#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityDeviceBase.h"
#include "FacilityFan.generated.h"

class APawn;
class UBoxComponent;
class UPointLightComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;

/**
 * A vent fan in Plant. It runs on PLANT unless the circuit-routing panel moves it, and it kills while
 * it turns: a pawn that enters the kill volume while the fan runs is caught, and so is whoever is
 * already inside when it starts, which is what makes starting the fans with someone in the vent a
 * weapon.
 *
 * Whether the fan turns is FFacilityRules::AreFansRunning over this device's id, so this actor and the
 * route solver answer the same question. The kill keeps no copy of that answer: the kill volume
 * overlaps pawns all the time, and the rules are asked at the moment a pawn is caught. What being
 * caught means belongs to the pawn. The fan applies KillDamage through the engine's damage events and
 * the pawn decides what dying is.
 *
 * The vent itself is plain level geometry. The fan goes at the bottom of the shaft, and a second one
 * goes in the service tunnel under the same DeviceId: the vent fans are one device, so the two run,
 * stop and kill together.
 *
 * Components: Root is where the actor is placed. Hub is what the blades turn about, around its own Z,
 * so point its Z along the shaft. Blades is the mesh, attached to the hub and without collision, so a
 * stopped fan never blocks the drop. KillVolume is the box that kills; keep it short and at the
 * blades, so a player who drops in falls toward them before dying. Light is the fan's own light, on
 * while the lights zone named by RoomLightsId is off, so the blades never turn unseen in a dark room.
 *
 * There is nothing to use on a fan by hand. Looking at one says whether it is running.
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityFan : public AFacilityDeviceBase
{
	GENERATED_BODY()

public:

	AFacilityFan();

	/** True when the facility rules say the fan is turning, which is when it kills. False outside a running game. */
	UFUNCTION(BlueprintPure, Category = "Facility|Fan")
	bool IsRunning() const;

	/** How fast the blades turn right now, in degrees per second. Presentation only: the kill follows IsRunning. */
	UFUNCTION(BlueprintPure, Category = "Facility|Fan")
	float GetSpinSpeed() const { return SpinSpeed; }

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnFacilityStateChanged() override;
	virtual void RefreshDisplayData() override;

	/** The vent fans are one device: the fan in the vent shaft and the one in the service tunnel share the id. */
	virtual bool SharesDeviceId() const override { return true; }

	/**
	 * The fan started or stopped. Also called once from BeginPlay with the starting state, and on every
	 * reset. bInstant is true for those two, where the blades are put at speed or at rest instead of
	 * spinning up or down, so a spin-up or spin-down sound belongs on the calls where it is false. Fires
	 * before a starting fan kills whoever is inside.
	 */
	virtual void OnRunningChanged(bool bRunning, bool bInstant) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Fan", meta = (DisplayName = "On Running Changed"))
	void ReceiveRunningChanged(bool bRunning, bool bInstant);

	//
	// Configuration
	//

	/** How fast the blades turn at full speed, in degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Fan", meta = (ClampMin = "0.0"))
	float MaxSpinSpeed = 720.f;

	/** Seconds from rest to full speed. Zero starts at full speed. The fan kills from the moment it starts, whatever the blades look like. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Fan", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float SpinUpTime = 1.5f;

	/** Seconds from full speed to rest. Zero stops dead. The fan is harmless from the moment it stops, whatever the blades look like. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Fan", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float SpinDownTime = 3.f;

	/** Damage applied to a pawn the fan catches. Meant to kill anything. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Fan", meta = (ClampMin = "0.0"))
	float KillDamage = 100000.f;

	/**
	 * Id of the lights zone around the fan, like Lights_Plant for the basement. The fan's own light is on
	 * while AreLightsOn says that zone is dark, and None counts as dark. It follows the zone rather than
	 * a circuit, so rerouting the room lights never leaves the blades turning in the dark.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Fan")
	FName RoomLightsId;

	//
	// Components
	//

	/** What the blades turn about, around its own Z. Point Z along the shaft. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Fan")
	TObjectPtr<USceneComponent> Hub;

	/** The blades, attached to the hub. No collision, so a stopped fan never blocks the drop. The mesh is assigned in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Fan")
	TObjectPtr<UStaticMeshComponent> Blades;

	/**
	 * What kills. Overlaps pawns and ignores everything else, all the time. Keep it short, at the blades
	 * and inside the shaft, so a player who drops in falls toward the blades before dying and nobody
	 * standing in the room below can reach it.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Fan")
	TObjectPtr<UBoxComponent> KillVolume;

	/** The fan's own light. On while the lights zone named by RoomLightsId is off. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Fan")
	TObjectPtr<UPointLightComponent> Light;

private:

	UFUNCTION()
	void HandleKillVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/**
	 * Re-reads IsRunning and reacts when it differs from what the fan last reacted to: the blades head
	 * for the new speed, the hooks fire, and a fan that just started kills whoever is inside. With
	 * bInstant it always reacts, puts the blades straight at speed or at rest, and kills nobody.
	 */
	void ReconcileRunning(bool bInstant);

	/** Catches every pawn inside the kill volume, for as long as the fan is still running. */
	void KillPawnsInside();

	/** Applies KillDamage to the pawn. Whether it dies, and what dying means, is the pawn's business. */
	void Kill(APawn* Victim);

	/** Turns the fan's own light on while the room lights are off, and off while they are on. */
	void RefreshLight();

	/** The hub's authored rotation, read once at BeginPlay. The blades turn from this about the hub's own Z. */
	FQuat HubRestPose = FQuat::Identity;

	/** How far the blades have turned from the rest pose, in degrees, kept within one turn. */
	float SpinAngle = 0.f;

	/** How fast the blades turn right now, in degrees per second. */
	float SpinSpeed = 0.f;

	/**
	 * Whether the fan was running when it last reacted, and so where the blades are heading. Exists to
	 * turn state changes into edges. It is not whether the fan kills: that is asked of the rules through
	 * IsRunning every time.
	 */
	bool bLastReportedRunning = false;
};
