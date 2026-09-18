#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityDeviceBase.h"
#include "FacilityAlarmConsole.generated.h"

class UStaticMeshComponent;

/**
 * The alarm console at the Security desk. It runs on SECURITY, and using it while it has power brings
 * the alarm down one level: the GDD's silencing at a powered console. Cutting SECURITY takes that away
 * while the alarm itself stays armed, since the alarm is guard-side state and not on any circuit.
 *
 * Whether a use does anything is FFacilityRules::CanSilenceAlarm over this device's id, so the prompt,
 * the press and the route solver agree. The console keeps no state of its own beyond its routing; the
 * alarm level is the facility's.
 *
 * The neutral character will use this console too, to open the gate and to override the lockdown seal,
 * once they exist. The GDD gives the console a hack level of 2 but no effect for the hack, so the
 * handheld has nothing to work on here yet.
 *
 * Components: Root is where the actor is placed. Console is the desk unit; it needs collision so the
 * Interactor's trace can hit it. The mesh is assigned in the Blueprint.
 *
 * Whatever executes an interaction calls Silence().
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityAlarmConsole : public AFacilityDeviceBase
{
	GENERATED_BODY()

public:

	AFacilityAlarmConsole();

	/** Brings the alarm down one level through the facility state. False when the rules refused: no power, or the alarm is quiet already. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Alarm Console")
	bool Silence();

	/** True when the facility rules would bring the alarm down right now. */
	UFUNCTION(BlueprintPure, Category = "Facility|Alarm Console")
	bool CanSilence() const;

protected:

	virtual void RefreshDisplayData() override;

	//
	// Components
	//

	/** The desk unit. Needs collision so the Interactor's trace can hit it. The mesh is assigned in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Alarm Console")
	TObjectPtr<UStaticMeshComponent> Console;
};
