#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityActorBase.h"
#include "Facility/FacilityTypes.h"
#include "FacilityCrate.generated.h"

class APawn;
class UBoxComponent;
class UStaticMeshComponent;

/**
 * The dock crate in Storage. It starts wedged in the loading dock shutter, which cannot open around
 * it, and getting it out of the way is the first step of that exit: the pry bar levers it off, or the
 * cargo lift running shifts it; the neutral character as a second pair of hands comes later. Moved, it
 * sits at the head of the cargo lift shaft. From there a car called down carries it to Plant quietly,
 * and while the car is at the bottom the shaft is open and the crate can be pushed down it: it lands
 * on whatever stands in Plant below, kills it, and the noise raises the alarm one level.
 *
 * Where the crate is lives in the facility state under CrateId, FFacilityRules::GetCratePosition and
 * what follows it, so the prompt, the dock shutter and the route solver agree, and a reset puts it
 * back in the shutter. This actor reads the position back and slides the body between three poses:
 * where it was placed, the shaft head and the landing. Whether a press does anything is
 * FFacilityRules::CanMoveCrate or CanDropCrate.
 *
 * The kill is the drop's edge, the way a fan starting kills whoever is inside: the moment the state
 * says the crate was dropped, every pawn in the landing volume takes KillDamage through the engine's
 * damage events, whatever the body is still doing on its way down. A crate riding the car down lands
 * on nobody. A reset never kills.
 *
 * Components: Root is where the actor is placed, wedged in the shutter. ShaftHead is a scene component
 * to place at the head of the lift shaft, where the crate sits once moved; put it where the car's
 * floor is when the car is up. Landing is placed at the bottom of the shaft in Plant. Body is the
 * crate mesh, Movable with collision, assigned in the Blueprint. KillVolume sits under Landing,
 * overlapping pawns and nothing else; size it to the crate's footprint.
 *
 * Whatever executes an interaction calls Use().
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityCrate : public AFacilityActorBase
{
	GENERATED_BODY()

public:

	AFacilityCrate();

	/**
	 * What pressing interact on the crate does. Pushes it down the shaft when it sits at the shaft head
	 * over an open shaft, otherwise moves it off the shutter when something can. False when neither happened.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Crate")
	bool Use();

	/** Moves the crate off the shutter to the shaft head through the facility state. False when the rules refused. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Crate")
	bool Move();

	/** Pushes the crate down the open shaft through the facility state. False when the rules refused. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Crate")
	bool Drop();

	/** Where the crate is according to the facility state. The body may still be on its way there. */
	UFUNCTION(BlueprintPure, Category = "Facility|Crate")
	ECratePosition GetPosition() const;

	UFUNCTION(BlueprintPure, Category = "Facility|Crate")
	bool CanMove() const;

	UFUNCTION(BlueprintPure, Category = "Facility|Crate")
	bool CanDrop() const;

	/** True once the crate was pushed down the shaft rather than ridden down. */
	UFUNCTION(BlueprintPure, Category = "Facility|Crate")
	bool WasDropped() const;

	/** True while the body is between poses. */
	UFUNCTION(BlueprintPure, Category = "Facility|Crate")
	bool IsMoving() const { return bMoving; }

	UFUNCTION(BlueprintPure, Category = "Facility|Crate")
	FName GetCrateId() const { return CrateId; }

	UFUNCTION(BlueprintPure, Category = "Facility|Crate")
	FName GetLiftId() const { return LiftId; }

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnFacilityStateChanged() override;
	virtual void RefreshDisplayData() override;

	/**
	 * The crate's position changed: moved, dropped or ridden down, or a reset put it back. Also called
	 * once from BeginPlay with the starting position; bInstant is true then and on a reset, so a scrape
	 * or a crash belongs on the calls where it is false. bDropped says whether this is the drop. Fires
	 * before the drop kills whoever stands below.
	 */
	virtual void OnPositionChanged(ECratePosition NewPosition, bool bDropped, bool bInstant) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Crate", meta = (DisplayName = "On Position Changed"))
	void ReceivePositionChanged(ECratePosition NewPosition, bool bDropped, bool bInstant);

	/** The body finished a slide and rests at a pose. Not called for the snap at BeginPlay or on a reset. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Crate", meta = (DisplayName = "On Body Settled"))
	void ReceiveBodySettled(ECratePosition Position);

	//
	// Configuration
	//

	/** Key into the facility state's crates. Unique per crate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Crate")
	FName CrateId;

	/** DeviceId of the cargo lift whose shaft the crate goes down, and which moves it when the pry bar is not at hand. Seeded into the facility state at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Crate")
	FName LiftId;

	/** How fast the body slides when pushed aside or ridden down. Match the lift's TravelSpeed for the ride. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Crate", meta = (ClampMin = "1.0", ForceUnits = "cm/s"))
	float MoveSpeed = 200.f;

	/** How fast the body falls when dropped down the shaft. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Crate", meta = (ClampMin = "1.0", ForceUnits = "cm/s"))
	float DropSpeed = 1500.f;

	/** Damage applied to a pawn the dropped crate lands on. Meant to kill anything. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Crate", meta = (ClampMin = "0.0"))
	float KillDamage = 100000.f;

	//
	// Components. Root is the pose in the shutter; the other two are placed with the gizmo.
	//

	/** Where the crate sits once moved off the shutter: the head of the lift shaft, at the car's floor when the car is up. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Crate")
	TObjectPtr<USceneComponent> ShaftHead;

	/** Where the crate ends up: the bottom of the shaft, in Plant. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Crate")
	TObjectPtr<USceneComponent> Landing;

	/** The crate. Movable with collision, so it blocks the shutter and the Interactor's trace can hit it. The mesh is assigned in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Crate")
	TObjectPtr<UStaticMeshComponent> Body;

	/** What the dropped crate lands on. Under Landing, overlapping pawns and nothing else. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Crate")
	TObjectPtr<UBoxComponent> KillVolume;

private:

	/** Puts the body at a pose immediately, ending any slide. */
	void SnapTo(ECratePosition Position);

	/** Starts the body toward a pose at a speed. Nothing happens if it is there or already on its way. */
	void SlideTo(ECratePosition Position, float Speed);

	/** World location of a pose. */
	FVector GetPoseLocation(ECratePosition Position) const;

	/** Catches every pawn under the landing. */
	void KillPawnsBelow();

	/** Applies KillDamage to the pawn. Whether it dies, and what dying means, is the pawn's business. */
	void Kill(APawn* Victim);

	/** Pose the body is at, or heading for while bMoving. */
	ECratePosition TargetPosition = ECratePosition::JammingShutter;

	/** Speed of the slide under way. */
	float SlideSpeed = 0.f;

	bool bMoving = false;

	/** What the hooks were last told. They exist only to turn state changes into edges; the facility state is the answer. */
	ECratePosition LastReportedPosition = ECratePosition::JammingShutter;
	bool bLastReportedDropped = false;
};
