#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityDeviceBase.h"
#include "FacilityLift.generated.h"

class UStaticMeshComponent;

/**
 * A car that travels a shaft between two stops. The elevator (Corridor to Storage, on DOORS) and
 * the cargo lift (Storage to Plant, on PLANT) are this one class placed twice. They differ in id,
 * circuit, where the stops are and Kind, and in rules that live in FFacilityRules keyed by id and
 * kind, not in code here: the elevator parks on Level 1 while its circuit is dead or lockdown is
 * called, and the handheld can stop it. Subclass only when one of them needs code the other does not.
 *
 * Which stop the car is at is facility state, not something this actor knows on its own. Using the
 * car asks the subsystem to send it to the other stop, the subsystem applies the rules and
 * broadcasts, and this actor reads the stop back and drives the car there at TravelSpeed. So the
 * route solver, a call panel on either floor and this actor always agree about where the car is,
 * and a reset puts it back along with everything else.
 *
 * The rules gate new calls only, see FFacilityRules::GetLiftObstacle. A car that is already
 * travelling finishes the trip, because the state has it at the far stop from the moment the call is
 * accepted, and a car stuck halfway would disagree with it. The cargo lift moving the crate and a
 * dead shaft being climbable are rules over the state too and belong in FFacilityRules.
 *
 * The base's Interactable finds this actor through the car mesh, so a player on the other floor
 * cannot reach it. Each floor gets an AFacilityLiftCallPanel that calls the car over instead.
 * Whatever executes an interaction on the car calls Toggle(). The hack press arrives through OnHacked
 * and calls Hack().
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityLift : public AFacilityDeviceBase
{
	GENERATED_BODY()

public:

	AFacilityLift();

	/**
	 * Sends the car to the other stop through the facility state. False when the rules refused the
	 * call, which today means no power. For the interaction logic to call when the player uses the car.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Lift")
	bool Toggle();

	/** Stop the car is at according to the facility state. The car mesh may still be on its way there. */
	UFUNCTION(BlueprintPure, Category = "Facility|Lift")
	ELiftStop GetStop() const;

	/** True when the facility rules would accept a call right now. */
	UFUNCTION(BlueprintPure, Category = "Facility|Lift")
	bool CanUse() const;

	/** Why the car will not answer a call right now, None when it will. What the prompt shows. */
	UFUNCTION(BlueprintPure, Category = "Facility|Lift")
	ELiftObstacle GetObstacle() const;

	/**
	 * Works the handheld on the lift through the facility state. A hack the handheld reaches stops the
	 * car silently for the rest of the attempt; one it cannot reach fails and raises the alarm. False
	 * when the handheld had nothing to work on.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Lift")
	bool Hack();

	/** True when the handheld has something to work on here, whether or not it would succeed. */
	UFUNCTION(BlueprintPure, Category = "Facility|Lift")
	bool CanHack() const;

	UFUNCTION(BlueprintPure, Category = "Facility|Lift")
	ELiftKind GetKind() const { return Kind; }

	/** True while the car mesh is between stops. */
	UFUNCTION(BlueprintPure, Category = "Facility|Lift")
	bool IsMoving() const { return bMoving; }

	UFUNCTION(BlueprintPure, Category = "Facility|Lift")
	UStaticMeshComponent* GetCar() const { return Car; }

protected:

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnFacilityStateChanged() override;
	virtual void RefreshDisplayData() override;
	virtual void OnHacked(AActor* Hacker) override;

	/** The car mesh finished a trip and is parked at Stop. Not called for the snap at BeginPlay or on a reset. */
	virtual void OnCarArrived(ELiftStop Stop) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Lift", meta = (DisplayName = "On Car Arrived"))
	void ReceiveCarArrived(ELiftStop Stop);

	//
	// Configuration
	//

	/** Where the car is parked when the level starts. Seeded into the facility state at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Lift")
	ELiftStop InitialStop = ELiftStop::ELS_Bottom;

	/**
	 * Which lift this is. The elevator parks on Level 1, its bottom stop, while its circuit is dead or
	 * lockdown is called; the cargo lift only needs power. Seeded into the facility state at BeginPlay.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Lift")
	ELiftKind Kind = ELiftKind::CargoLift;

	/** Speed of the car between stops. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Lift", meta = (ClampMin = "1.0", ForceUnits = "cm/s"))
	float TravelSpeed = 200.f;

	//
	// Components. The stops are placed with the gizmo; the car's origin sits on a stop when parked.
	//

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Lift")
	TObjectPtr<USceneComponent> BottomStop;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Lift")
	TObjectPtr<USceneComponent> TopStop;

	/**
	 * The platform the player rides. Movable with collision, so the character is carried along and
	 * the Interactor's trace can hit it. The mesh itself is assigned in the Blueprint or the instance.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Lift")
	TObjectPtr<UStaticMeshComponent> Car;

private:

	/** Parks the car mesh at a stop immediately, ending any trip. */
	void SnapCarTo(ELiftStop Stop);

	/** Starts the car mesh towards a stop. Nothing happens if it is parked there or already on its way. */
	void TravelTo(ELiftStop Stop);

	USceneComponent* GetStopComponent(ELiftStop Stop) const;

	/** The hack line: what the handheld does to the lift, or nothing for a lift it has no business with. */
	void RefreshHackDisplay();

	/** Stop the car mesh is parked at, or heading for while bMoving. */
	ELiftStop TargetStop = ELiftStop::ELS_Bottom;

	bool bMoving = false;
};
