#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityActorBase.h"
#include "Facility/FacilityTypes.h"
#include "FacilityLiftCallPanel.generated.h"

class UStaticMeshComponent;

/**
 * A button on one floor that calls a lift's car to that floor. The car's own Interactable is only
 * reachable from the floor the car is on, so each floor of a shaft gets one of these by the doors.
 *
 * Not a facility device: it has no power of its own and is not in the facility state. It is a way
 * to reach two rules for the lift named by LiftId, CallLift and HackLift, and whether either works
 * comes from that lift through GetLiftObstacle and CanHackLift. Giving it a circuit of its own would
 * let the routing panel separate the button from its lift, which is wrong.
 *
 * Whatever executes an interaction calls Call(). The hack press arrives through OnHacked and calls Hack().
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityLiftCallPanel : public AFacilityActorBase
{
	GENERATED_BODY()

public:

	AFacilityLiftCallPanel();

	/**
	 * Calls the lift's car to this panel's stop through the facility state. False when the rules
	 * refused, which today means no power, or the car is already here. For the interaction logic to
	 * call when the player uses the panel.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Lift")
	bool Call();

	/** True when the facility rules would accept a call and the car is not already here. */
	UFUNCTION(BlueprintPure, Category = "Facility|Lift")
	bool CanCall() const;

	/** True when the facility state has the car at this panel's stop. The car mesh may still be on its way. */
	UFUNCTION(BlueprintPure, Category = "Facility|Lift")
	bool IsCarHere() const;

	/**
	 * Works the handheld on the lift through the facility state, from this floor. A hack the handheld
	 * reaches stops the car wherever it is; one it cannot reach fails and raises the alarm. False when
	 * the handheld had nothing to work on.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Lift")
	bool Hack();

	/** True when the handheld has something to work on in the lift, whether or not it would succeed. */
	UFUNCTION(BlueprintPure, Category = "Facility|Lift")
	bool CanHack() const;

	UFUNCTION(BlueprintPure, Category = "Facility|Lift")
	FName GetLiftId() const { return LiftId; }

	UFUNCTION(BlueprintPure, Category = "Facility|Lift")
	ELiftStop GetStop() const { return Stop; }

protected:

	virtual void RefreshDisplayData() override;
	virtual void OnHacked(AActor* Hacker) override;

	//
	// Configuration
	//

	/** DeviceId of the AFacilityLift this panel calls. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Lift")
	FName LiftId;

	/** The stop this panel stands at. Calling brings the car here. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Lift")
	ELiftStop Stop = ELiftStop::ELS_Bottom;

	//
	// Components
	//

	/** The button housing. Needs collision so the Interactor's trace can hit it. The mesh is assigned in the Blueprint or the instance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Lift")
	TObjectPtr<UStaticMeshComponent> Panel;
};
