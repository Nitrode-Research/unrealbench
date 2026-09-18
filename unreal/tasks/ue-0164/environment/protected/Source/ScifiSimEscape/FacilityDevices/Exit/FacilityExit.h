#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Facility/FacilityTypes.h"
#include "FacilityExit.generated.h"

class UBoxComponent;
class UFacilityStateSubsystem;
class UPrimitiveComponent;

/**
 * The threshold of one of the three ways out. A volume placed across the opening, just outside the
 * main gate, the dock shutter or the tunnel door: the player passing through it is the win.
 *
 * Passing through, not opening. The neutral character can open the gate from the console while the
 * player is still in the Lab, and opening it under an alert is loud, so lockdown can seal it again
 * before the player gets there. What counts is the player being on the far side, and this volume
 * is where that is decided.
 *
 * The volume alone wins nothing. The exit has to be open by the rules, FFacilityRules::CanEscape,
 * which for the gate means the door of kind Main gate is open, and for the tunnel means the door named
 * in DoorId is open and the fans named in FansId are still. This actor seeds those two names into the
 * facility state when it begins play, so the rules never need the actor. A player who reaches the
 * threshold while the exit is shut gets a line in the log saying what held them, so a volume that
 * never wins is not a mystery.
 *
 * Not a facility actor: there is nothing to look at or use, so it has no Interactable. It asks the
 * subsystem to escape when a player pawn enters it, and again on any state change while a player
 * pawn is inside, so standing on the threshold as the exit opens counts too. What the win does to
 * the game is the game mode's business, see AScifiSimEscapeGameMode.
 *
 * Components: Root is where the actor is placed. Volume is the box; it overlaps pawns and nothing
 * else, and is sized per opening in the Blueprint or the level.
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityExit : public AActor
{
	GENERATED_BODY()

public:

	AFacilityExit();

	UFUNCTION(BlueprintPure, Category = "Facility|Exit")
	EFacilityExit GetExit() const { return Exit; }

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	//
	// Configuration
	//

	/** Which of the three ways out this is the threshold of. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Exit")
	EFacilityExit Exit = EFacilityExit::None;

	/**
	 * Id of the door the player leaves through: the tunnel door for the service tunnel. The exit lets
	 * the player through only while that door is open. The gate ignores it, since the gate is the door
	 * of kind Main gate. Seeded into the facility state at BeginPlay.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Exit")
	FName DoorId;

	/**
	 * Device id of the fans that make the way out lethal while they turn: the vent fans for the service
	 * tunnel. The exit lets the player through only while they are still. None when no fans guard it.
	 * Seeded into the facility state at BeginPlay.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Exit")
	FName FansId;

	//
	// Components
	//

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Exit")
	TObjectPtr<USceneComponent> Root;

	/** The threshold. Overlaps pawns and nothing else. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Exit")
	TObjectPtr<UBoxComponent> Volume;

private:

	UFUNCTION()
	void HandleVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleFacilityStateChanged();

	/** Asks the subsystem to escape if the exit is open right now. True if the player got out. */
	bool TryEscape();

	/** What holds the exit shut right now, for the log. */
	FString DescribeWhyShut() const;

	UFacilityStateSubsystem* FindFacility() const;
};
