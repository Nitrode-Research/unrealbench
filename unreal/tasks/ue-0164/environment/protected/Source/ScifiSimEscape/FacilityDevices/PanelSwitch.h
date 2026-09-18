#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Facility/FacilityTypes.h"
#include "PanelSwitch.generated.h"

class UFacilityStateSubsystem;
class UInteractableComponent;
class UStaticMeshComponent;

/**
 * One switch on the breaker panel in Plant. Pressing interact on it asks the facility subsystem to
 * flip its circuit's breaker, and that is all a press does.
 *
 * The switch never decides whether it is on. bIsOn mirrors whether its circuit is live, read from the
 * facility state when the switch begins play and again on every state change. That keeps the switch
 * right when something else moved its breaker: turning on a third circuit trips the whole panel and
 * throws the other switches, and a death reset puts every switch back where the attempt started.
 *
 * Cosmetics belong in a Blueprint child. Implement On Switch Changed to move the lever. The highlight
 * needs no wiring: the player's Interactor draws it. The mesh needs collision for the player's
 * Interactor to find the switch, and that collision has to sit in front of the panel's, because the
 * Interactor's line-of-sight trace does not ignore the panel.
 */
UCLASS()
class SCIFISIMESCAPE_API APanelSwitch : public AActor
{
	GENERATED_BODY()

public:

	APanelSwitch();

	/** The circuit whose breaker this switch flips. */
	UFUNCTION(BlueprintPure, Category = "Facility|Breaker")
	EFacilityCircuit GetCircuit() const { return Circuit; }

	/** True while the switch's circuit is live. Follows the facility state, not presses. */
	UFUNCTION(BlueprintPure, Category = "Facility|Breaker")
	bool IsOn() const { return bIsOn; }

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * The switch went on or off. Move the lever here.
	 *
	 * Also called once when the switch begins play, with bInstant set, so the lever can snap to its
	 * starting position instead of animating there. Every other call is a real change, including ones
	 * this switch did not cause, such as a trip or a reset.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Breaker", meta = (DisplayName = "On Switch Changed"))
	void ReceiveSwitchChanged(bool bOn, bool bInstant);

	/**
	 * The circuit whose breaker this switch flips. Set on each placed switch. A switch left on None logs a
	 * warning when play begins and flips nothing.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Facility|Breaker")
	EFacilityCircuit Circuit = EFacilityCircuit::EFC_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Breaker")
	TObjectPtr<USceneComponent> Root;

	/** What the player sees and looks at. Needs collision, see the class comment. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Breaker")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Breaker")
	TObjectPtr<UInteractableComponent> Interactable;

private:

	UFUNCTION()
	void HandleInteracted(AActor* InteractingActor);

	UFUNCTION()
	void HandleFacilityStateChanged();

	/**
	 * Re-reads the circuit from the facility state, updates the prompt, and tells Blueprint when the
	 * switch went on or off. bInstant tells Blueprint regardless, for the starting state.
	 */
	void RefreshFromFacility(bool bInstant);

	UFacilityStateSubsystem* FindFacility() const;

	/** Mirrors whether Circuit is live. Written only by RefreshFromFacility, never by a press. */
	UPROPERTY(VisibleInstanceOnly, Category = "Facility|Breaker")
	bool bIsOn = false;
};
