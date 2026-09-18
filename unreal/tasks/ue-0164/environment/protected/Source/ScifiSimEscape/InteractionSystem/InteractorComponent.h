// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractorComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInteractionLost);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInteractionDetected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFocusedDisplayDataChanged);


class UCameraComponent;
class UInteractableComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SCIFISIMESCAPE_API UInteractorComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UInteractorComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	
public:	
	
	/**
	 * @brief Initiates the recurring timer for Sphere Detection.
	 * 
	 * Gameplay Programmers / Designers: Call this to enable proximity-based interaction detection.
	 * Useful to toggle off during cutscenes or specific gameplay states where interaction is forbidden.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interaction|Detection")
	void StartSphereDetection();

	/**
	 * @brief Stops the recurring timer for Sphere Detection and clears current highlights.
	 * 
	 * Use this when the player dies, enters a vehicle, or when interaction should be paused to save performance.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interaction|Detection")
	void StopSphereDetection();

	/**
	 * @brief Executes a single iteration of the Sphere Detection logic immediately.
	 * 
	 * Scans the immediate area around the player for UInteractableComponents. 
	 * Can be called manually if timer-based detection is disabled and precise, event-driven checks are preferred.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interaction|Detection")
	void PerformSphereDetection();

	UPROPERTY(BlueprintAssignable, Category = "Interaction|Delegate")
	FOnInteractionDetected OnInteractableDetected;

	UPROPERTY(BlueprintAssignable, Category = "Interaction|Delegate")
	FOnInteractionLost OnInteractionLost;

	/**
	 * @brief Fires whenever what the HUD shows may have changed: focus moved to another interactable or
	 * to none, or the focused interactable changed its display data. Read the new value through
	 * GetFocusedInteractable.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Interaction|Delegate")
	FOnFocusedDisplayDataChanged OnFocusedDisplayDataChanged;

	/**
	 * @brief Presses interact on the focused interactable, if there is one.
	 *
	 * Bound to the interact input on the owning character. Detection is not re-run first, so the press
	 * goes to whatever the HUD is showing, and nothing is pressed while detection is stopped.
	 * @return True if an interactable was focused and received the press.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	bool Interact();

	/**
	 * @brief Presses hack on the focused interactable, if there is one: the owner works the handheld on
	 * whatever the HUD is showing. Same rules as Interact.
	 * @return True if an interactable was focused and received the press.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	bool Hack();

	/** @brief The interactable a press would go to right now, or null when nothing is focused. */
	UFUNCTION(BlueprintPure, Category = "Interaction")
	UInteractableComponent* GetFocusedInteractable() const;
	
private:

	void ClearDetectionState();

	/** @brief Forwards a display data change on the focused interactable to OnFocusedDisplayDataChanged. */
	UFUNCTION()
	void HandleFocusedDisplayDataChanged();

	/** Camera location when the owner has a camera component, otherwise the owner's eye view point. */
	FVector GetViewLocation() const;

	/** Camera forward vector when the owner has a camera component, otherwise the owner's view rotation. */
	FVector GetViewForward() const;

	/**
	* @brief Brings the highlights in line with the latest detection pass.
	*
	* Compares the set of currently detected interactables against previously highlighted ones,
	* highlighting any that came into range and unhighlighting any that have fallen out of it.
	* @param CurrentInteractables The interactables found in the latest detection pass.
	*/
	void ReconcileHighlightedInteractables(const TSet<TWeakObjectPtr<UInteractableComponent>>& CurrentInteractables);

	/**
	 * @brief Builds the two highlight overlays from HighlightMaterial, one in each color.
	 * Called once in BeginPlay, before the first detection pass. Without a HighlightMaterial nothing is highlighted.
	 */
	void CreateHighlightOverlays();

	/** @brief Draws the highlight on an interactable that came into range, and keeps its color current from then on. */
	void HighlightInteractable(UInteractableComponent* Interactable);

	/** @brief Takes the highlight off an interactable that left range, or off every one when detection stops. */
	void UnhighlightInteractable(UInteractableComponent* Interactable);

	/** @brief Draws the overlay whose color matches whether the interactable's interact press does something right now. */
	void RefreshHighlight(UInteractableComponent* Interactable) const;

	/** @brief A highlighted interactable changed its display data, so its highlight color may be stale. */
	UFUNCTION()
	void HandleHighlightedDisplayDataChanged();

	/**
	* @brief Checks if a weak pointer to an interactable component is valid and safe to use.
	* 
	* @param InteractableComponent The weak pointer to evaluate.
	* @return True if the component exists and is not pending kill.
	*/
	bool IsValidInteractableComponent(const TWeakObjectPtr<UInteractableComponent>& InteractableComponent) const;

	/**
	 * @brief Updates the currently focused interactable component. Highlights do not follow focus.
	 *
	 * Dispatches focus gained/lost events to the relevant interactable components.
	 * @param NewFocusedInteractable The newly selected best candidate.
	 */
	void SetFocusedInteractable(UInteractableComponent* NewFocusedInteractable);

	/**
	 * @brief Picks the interactable to focus from the highlighted set.
	 *
	 * Focus goes to the highlighted interactable whose actor the camera's forward trace lands on, or to
	 * none when the trace hits nothing or hits an actor that is not highlighted.
	 */
	UInteractableComponent* SelectBestCandidate() const;

	/**
	 * @brief Finds the actor the camera is pointing straight at.
	 *
	 * Traces from the view location along the view forward for SphereRadius on the visibility channel,
	 * ignoring the owner.
	 * @return The first actor the trace hits, or null when it hits nothing.
	 */
	AActor* FindLookedAtActor() const;

protected:

	/**
		 * @brief The interactable the camera's forward trace currently lands on.
		 * This is the component that will receive the interaction event if the player presses the interact key.
		 */
	UPROPERTY(BlueprintReadOnly, Category = "Interaction|Runtime")
	TWeakObjectPtr<UInteractableComponent> FocusedInteractable;
	
	/**
	* @brief The active set of all interactables currently within detection range.
	* Used as the pool of candidates for focus.
	*/
	TSet<TWeakObjectPtr<UInteractableComponent>> DetectedInteractables;
	
	/**
	 * @brief The set of interactables that are currently visually highlighted.
	 * Kept synchronized to ensure no objects stay highlighted.
	 */
	TSet<TWeakObjectPtr<UInteractableComponent>> HighlightedInteractables;
	
	/**
	 * @brief Handle for the recurring Sphere Detection timer.
	 * Stored to allow pausing/stopping the timer without ticking.
	 */
	FTimerHandle SphereDetectionTimerHandle;

	/**
	 * @brief The owner's camera, resolved once in BeginPlay.
	 * Origin of the focus trace, so it follows the camera through a crouch rather than the capsule.
	 */
	UPROPERTY()
	TObjectPtr<UCameraComponent> CachedCamera;

	/** @brief HighlightMaterial in CanInteractColor, shared by every highlighted interactable that can be used. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CanInteractOverlay;

	/** @brief HighlightMaterial in CannotInteractColor, shared by every highlighted interactable that cannot be used. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CannotInteractOverlay;

	/**
	* @brief How frequently (in seconds) the Sphere Detection routine is executed. 
	* Performance implication: Lower values are more responsive but cost more CPU. Default (0.1s) is usually optimal.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Sphere Detection", meta = (ClampMin = "0.1"))
	float SphereDetectionInterval = 0.1f;

	/**
	 * @brief The radius of the collision sphere used to find nearby interactables.
	 * Represents the maximum range a player can realistically notice an interactable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Sphere Detection", meta = (ClampMin = "1.0"))
	float SphereRadius = 450.f;

	//
	/// Highlight
	//

	/**
	 * @brief Overlay drawn on every interactable inside the detection sphere, not only the focused one.
	 * A translucent material with a vector parameter named HighlightColorParameter. Read once in BeginPlay;
	 * nothing is highlighted while it is empty.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Highlight")
	TObjectPtr<UMaterialInterface> HighlightMaterial;

	/** @brief The vector parameter on HighlightMaterial that takes the highlight color. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Highlight")
	FName HighlightColorParameter = TEXT("Color");

	/** @brief Highlight color of an interactable whose interact press does something right now. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Highlight")
	FLinearColor CanInteractColor = FLinearColor(0.f, 1.f, 1.f);

	/** @brief Highlight color of an interactable whose interact press does nothing right now. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Highlight")
	FLinearColor CannotInteractColor = FLinearColor(1.f, 0.f, 0.f);

	//
	/// Debug
	//
	
	/**
	 * @brief Toggles debug visualization for traces and detection volumes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Debug")
	bool bDrawDebug = false;

	/**
	 * @brief How long the debug shapes should remain visible on screen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Debug", meta = (EditCondition = "bDrawDebug", ClampMin = "0.0"))
	float DebugDrawDuration = 0.12f;
};
