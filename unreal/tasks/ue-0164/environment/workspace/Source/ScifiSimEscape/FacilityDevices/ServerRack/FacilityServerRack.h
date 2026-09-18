#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityDeviceBase.h"
#include "FacilityServerRack.generated.h"

class UStaticMeshComponent;

/**
 * The server rack in the Lab, on SECURITY. Its drive is a pickup placed in it; pulling that blinds the
 * camera and raises the handheld, and is the pickup's business. What is here is the fire: rigged to
 * overload with the pry bar while it has power, the rack burns for the rest of the attempt, the Lab
 * fills with smoke guards will not enter, the alarm goes to Alerted, and the sprinklers dump water
 * into Plant through the vent, which the facility counts as flood.
 *
 * Whether a press does anything is FFacilityRules::CanOverloadServerRack over this device's id, and
 * whether it burns is IsServerRackOverloaded, so the prompt, the press, the smoke the guards read and
 * the route solver agree. The GDD gives the rack hack level 2 but no effect for the hack, so the
 * handheld line stays empty whatever the settings say.
 *
 * Components: Root is where the actor is placed. Cabinet is the rack; it needs collision so the
 * Interactor's trace can hit it, and the mesh is assigned in the Blueprint. Fire and smoke go in On
 * Overloaded Changed.
 *
 * Whatever executes an interaction calls Use().
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityServerRack : public AFacilityDeviceBase
{
	GENERATED_BODY()

public:

	AFacilityServerRack();

	/** What pressing interact on the rack does: rigs it to overload with the pry bar. False when the rules refused. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Server Rack")
	bool Use();

	/** Rigs the rack to overload through the facility state. False when the rules refused. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Server Rack")
	bool Overload();

	/** Burning, according to the facility state. */
	UFUNCTION(BlueprintPure, Category = "Facility|Server Rack")
	bool IsOverloaded() const;

	/** True when the pry bar would rig it right now. */
	UFUNCTION(BlueprintPure, Category = "Facility|Server Rack")
	bool CanOverload() const;

protected:

	virtual void BeginPlay() override;
	virtual void OnFacilityStateChanged() override;
	virtual void RefreshDisplayData() override;

	/**
	 * The rack caught fire, or a reset put it out. Also called once from BeginPlay with the starting
	 * state; bInstant is true then and on a reset, so the blast and the smoke belong on the calls where
	 * it is false.
	 */
	virtual void OnOverloadedChanged(bool bOverloaded, bool bInstant) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Server Rack", meta = (DisplayName = "On Overloaded Changed"))
	void ReceiveOverloadedChanged(bool bOverloaded, bool bInstant);

	//
	// Components
	//

	/** The rack. Needs collision so the Interactor's trace can hit it. The mesh is assigned in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Server Rack")
	TObjectPtr<UStaticMeshComponent> Cabinet;

private:

	/** Re-reads IsOverloaded and tells the hooks when it differs from what they were last told. bInstant tells them regardless. */
	void ReconcileOverloaded(bool bInstant);

	/** What the hooks were last told. Exists only to turn state changes into edges; IsOverloaded reads the facility state. */
	bool bLastReportedOverloaded = false;
};
