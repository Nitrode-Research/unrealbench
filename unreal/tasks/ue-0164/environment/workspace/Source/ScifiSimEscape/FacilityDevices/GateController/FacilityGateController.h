#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityDeviceBase.h"
#include "FacilityGateController.generated.h"

class UStaticMeshComponent;

/**
 * The main gate control in Security. The gate itself is not a handheld target; this is. It runs on
 * DOORS like the gate, and hacking it at the level the facility settings give its id, 2 in the GDD,
 * opens the gate the way a level 3 card does. The
 * controller stands in for the card and nothing else: a gate that is sealed by lockdown or has no
 * power stays shut, and an open gate needs nothing from it.
 *
 * Whether the handheld has something to work on here is FFacilityRules::CanHackGateController over
 * this device's id, so the prompt, the press and the route solver agree. The gate is found by its kind,
 * so the controller needs no link to it.
 *
 * The neutral character will use this control too, to open the gate and to override the lockdown seal,
 * once they exist.
 *
 * Components: Root is where the actor is placed. Panel is the control unit; it needs collision so the
 * Interactor's trace can hit it, and the mesh is assigned in the Blueprint.
 *
 * There is nothing to use on the controller by hand. Looking at it says what the gate is doing. The
 * hack press arrives through OnHacked and calls Hack().
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityGateController : public AFacilityDeviceBase
{
	GENERATED_BODY()

public:

	AFacilityGateController();

	/**
	 * Works the handheld on the controller through the facility state. A hack the handheld reaches opens
	 * the gate silently; one it cannot reach fails and raises the alarm. False when the handheld had
	 * nothing to work on.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Gate Controller")
	bool Hack();

	/** True when the handheld has something to work on here, whether or not it would succeed. */
	UFUNCTION(BlueprintPure, Category = "Facility|Gate Controller")
	bool CanHack() const;

protected:

	virtual void RefreshDisplayData() override;
	virtual void OnHacked(AActor* Hacker) override;

	//
	// Components
	//

	/** The control unit. Needs collision so the Interactor's trace can hit it. The mesh is assigned in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Gate Controller")
	TObjectPtr<UStaticMeshComponent> Panel;
};
