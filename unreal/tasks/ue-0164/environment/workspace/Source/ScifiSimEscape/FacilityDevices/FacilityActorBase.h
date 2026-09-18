#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FacilityActorBase.generated.h"

class UFacilityStateSubsystem;
class UInteractableComponent;

/**
 * Base for everything in the facility the player can look at and use: doors, lifts, panels, the
 * hatch, the ladder, valves, crates. It owns the plumbing every one of them needs and nothing about
 * power. AFacilityDeviceBase adds power on top for the things that draw from a breaker.
 *
 * What a child gets: a scene root, a UInteractableComponent so the player's Interactor can find and
 * focus the actor, focus, hack and facility-state hooks as C++ virtuals with matching Blueprint events,
 * a display refresh hook that keeps the prompt current, and FindFacility.
 *
 * The interact press reaches a child through the Interactable's On Interacted, wired in its Blueprint
 * to whatever the press does. The hack press is bound here instead: a child the handheld works on
 * overrides OnHacked, and nothing needs wiring.
 *
 * Detection is the Interactor's: a sphere overlap followed by a Visibility trace to the part of the
 * actor's collision bounds the player is looking at, so a child needs at least one primitive with
 * collision before the player can look at it. The base only provides the root; what is rendered and
 * what collides is the child's.
 */
UCLASS(Abstract, Blueprintable)
class SCIFISIMESCAPE_API AFacilityActorBase : public AActor
{
	GENERATED_BODY()

public:

	AFacilityActorBase();

	UFUNCTION(BlueprintPure, Category = "Facility")
	UInteractableComponent* GetInteractable() const { return Interactable; }

	/**
	 * True for a pawn a player controller is driving. Asked of the controller, not the pawn's player
	 * state, so it answers the moment a player possesses the pawn. Guards will walk into volumes too
	 * and must not count as the player, at an exit or in front of a camera.
	 */
	static bool IsPlayerPawn(const AActor* Actor);

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	//
	// Hooks for children. The C++ virtual runs first, then the Blueprint event.
	//

	/**
	 * Runs on every facility state change before the hooks below. For a base class that has to bring
	 * itself up to date before its children look: AFacilityDeviceBase reconciles power here so that
	 * OnFacilityStateChanged already sees the new answer. Children use OnFacilityStateChanged.
	 */
	virtual void PreFacilityStateChanged() {}

	/**
	 * Something in the facility state changed. Fires on every change, whether or not it concerns
	 * this actor. A reset to the initial state after a player death arrives here too, with the
	 * subsystem's IsResetting set for the duration, so a child that animates toward the state can
	 * snap instead.
	 */
	virtual void OnFacilityStateChanged() {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility", meta = (DisplayName = "On Facility State Changed"))
	void ReceiveFacilityStateChanged();

	/** The player's Interactor started or stopped looking at this actor. */
	virtual void OnFocusChanged(bool bFocused) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility", meta = (DisplayName = "On Focus Changed"))
	void ReceiveFocusChanged(bool bFocused);

	/**
	 * The player pressed hack while looking at this actor. Fires whether or not the handheld has anything
	 * to work on here; the child asks the rules. The base does nothing, which is right for most things.
	 */
	virtual void OnHacked(AActor* Hacker) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility", meta = (DisplayName = "On Hacked"))
	void ReceiveHacked(AActor* Hacker);

	/**
	 * Pushes what the Interactable shows the player: the verb a press performs, or why nothing happens.
	 * Called at the end of BeginPlay, after every facility state change once the hooks above have run,
	 * and when the player focuses the actor, which also covers reading another actor that had not
	 * begun play yet when this one did. A child that can be used overrides it and calls SetDisplayData
	 * on the Interactable; that only broadcasts when the text actually changed, so pushing every time
	 * is cheap. A child also calls it itself when something outside the facility state changed what
	 * it shows, like a ladder gaining a climber.
	 */
	virtual void RefreshDisplayData() {}

	/** The facility this actor lives in. Null outside a running game. */
	UFacilityStateSubsystem* FindFacility() const;

	//
	// Components
	//

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility")
	TObjectPtr<UInteractableComponent> Interactable;

private:

	UFUNCTION()
	void HandleFacilityStateChanged();

	UFUNCTION()
	void HandleFocusChanged(bool bFocused);

	UFUNCTION()
	void HandleHacked(AActor* Hacker);
};
