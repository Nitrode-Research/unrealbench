#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityDeviceBase.h"
#include "FacilityCamera.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;

/**
 * The camera in the Corridor. It runs on SECURITY and sees by the Corridor's lights: while it has
 * power and the lights zone named by LightsId is on, a player inside its view is a sighting, and a
 * sighting raises the alarm one level. The handheld loops it at the level the facility settings give
 * its id, 1 in the GDD; pulling the server drive blinds it, and cutting SECURITY or the lights kills
 * it. Shooting it waits for weapons.
 *
 * Whether the camera sees is FFacilityRules::IsCameraWatching over this device's id, so the prompt,
 * the sighting and the route solver agree. The view is a volume, not a trace: a box under the housing
 * reaching forward from the lens, so it sweeps as the camera pans. The GDD has the camera see the
 * whole Corridor; the pan covers it over time rather than all at once, so a player can slip past while
 * it looks the other way, and a stopped elevator's shaft is out of view because the box never reaches it.
 *
 * The housing pans like a real camera: from StartYaw toward MaxYaw at TurnSpeed and back again, about
 * the mount's up axis, for as long as the camera has power. Point the actor's Z up and tilt the housing
 * in the Blueprint; the pan is applied on top of that. Unpowered, the housing freezes where it is, and
 * a reset puts it back at StartYaw. Looped, blinded or in the dark it keeps panning: those kill the
 * feed, not the motor.
 *
 * A sighting is an event: the view sweeping onto the player or the player walking into it while the
 * camera watches, or the camera starting to watch with the player already inside, as when the lights
 * come back on. Standing in view raises nothing more; leaving and coming back does.
 *
 * Components: Root is where the actor is placed. Housing is the camera body; it needs collision so the
 * Interactor's trace can hit it for the hack, and the mesh is assigned in the Blueprint. View is the
 * box that sees, under the housing; it overlaps pawns and nothing else, and is sized to how far and
 * how wide the camera sees in the Blueprint.
 *
 * There is nothing to use on a camera by hand. Looking at one says whether it sees. The hack press
 * arrives through OnHacked and calls Hack().
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityCamera : public AFacilityDeviceBase
{
	GENERATED_BODY()

public:

	AFacilityCamera();

	/** True while the facility rules say the camera sees. False outside a running game. */
	UFUNCTION(BlueprintPure, Category = "Facility|Camera")
	bool IsWatching() const;

	/** True once the handheld has looped the camera, according to the facility state. */
	UFUNCTION(BlueprintPure, Category = "Facility|Camera")
	bool IsLooped() const;

	/**
	 * Works the handheld on the camera through the facility state. A hack the handheld reaches loops the
	 * camera silently for the rest of the attempt; one it cannot reach fails and raises the alarm. False
	 * when the handheld had nothing to work on.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Camera")
	bool Hack();

	/** True when the handheld has something to work on here, whether or not it would succeed. */
	UFUNCTION(BlueprintPure, Category = "Facility|Camera")
	bool CanHack() const;

	/** Where the housing is pointed right now, in degrees of yaw from its authored rotation. Presentation only. */
	UFUNCTION(BlueprintPure, Category = "Facility|Camera")
	float GetPanYaw() const { return PanYaw; }

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPowerChanged(bool bPowered) override;
	virtual void OnFacilityStateChanged() override;
	virtual void RefreshDisplayData() override;
	virtual void OnHacked(AActor* Hacker) override;

	/**
	 * The camera started or stopped seeing. Also called once from BeginPlay with the starting state, and
	 * on every reset; bInstant is true for those two. For the lens light, red while it sees and dark
	 * otherwise. Fires before a camera that just started seeing reports whoever is inside its view.
	 */
	virtual void OnWatchingChanged(bool bWatching, bool bInstant) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Camera", meta = (DisplayName = "On Watching Changed"))
	void ReceiveWatchingChanged(bool bWatching, bool bInstant);

	//
	// Configuration
	//

	/**
	 * Lights zone the camera sees by, like Level1_Light for the Corridor. A camera in the dark sees
	 * nothing, which is what cutting DOORS does to it. Seeded into the facility state at BeginPlay. None
	 * leaves the camera blind and logs a warning.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Camera")
	FName LightsId;

	/** Where the pan starts, in degrees of yaw from the housing's authored rotation. Where a reset puts it back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Camera|Pan", meta = (ClampMin = "-180.0", ClampMax = "180.0", ForceUnits = "deg"))
	float StartYaw = 0.f;

	/** Where the pan turns back, in degrees of yaw from the housing's authored rotation. The same as StartYaw holds the camera still. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Camera|Pan", meta = (ClampMin = "-180.0", ClampMax = "180.0", ForceUnits = "deg"))
	float MaxYaw = 80.f;

	/** How fast the housing pans, in degrees per second. Zero holds it still. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Camera|Pan", meta = (ClampMin = "0.0"))
	float TurnSpeed = 20.f;

	//
	// Components
	//

	/** The camera body. Needs collision so the Interactor's trace can hit it. The mesh is assigned in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Camera")
	TObjectPtr<UStaticMeshComponent> Housing;

	/** What the camera sees. Overlaps pawns and ignores everything else. Sized to the room it watches. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Camera")
	TObjectPtr<UBoxComponent> View;

private:

	UFUNCTION()
	void HandleViewBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/**
	 * Re-reads IsWatching and reacts when it differs from what the camera last reacted to: the hooks
	 * fire, and a camera that just started seeing reports the player if they are already in view. With
	 * bInstant it always reacts and reports nobody: a snap is the level beginning or a new attempt.
	 */
	void ReconcileWatching(bool bInstant);

	/** Reports a sighting of the player inside the view, if the camera is watching. */
	void ReportPlayerInside();

	/** Puts the pan at StartYaw, heading for MaxYaw. For the start of play and a reset. */
	void ResetPan();

	/** Turns the housing to PanYaw about the mount's up axis, on top of its authored rotation. */
	void ApplyPan();

	/**
	 * Whether the camera was watching when it last reacted. Exists only to turn state changes into edges.
	 * It is not whether the camera sees: that is asked of the rules through IsWatching every time.
	 */
	bool bLastReportedWatching = false;

	/** The housing's authored rotation, read once at BeginPlay. The pan turns from this. */
	FQuat HousingRestPose = FQuat::Identity;

	/** Where the housing is pointed, in degrees of yaw from the rest pose. */
	float PanYaw = 0.f;

	/** Which way the pan is going: 1 toward higher yaw, -1 toward lower. Flips at each end. */
	float PanDirection = 1.f;
};
