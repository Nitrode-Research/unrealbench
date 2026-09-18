#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityActorBase.h"
#include "FacilityGenerator.generated.h"

class UStaticMeshComponent;

/**
 * The backup generator in Plant. Started, it carries every cut circuit for a configured number of
 * steps, the one way around the panel's two-of-three rule; the breakers stay where they are, and what
 * they say comes back when the run ends. Starting it is loud: the alarm rises one level. While it runs,
 * the pry bar rigs it to overload: every breaker trips, the lockdown doors seal and the elevator parks
 * with the rest of the blackout, and the generator is finished for the attempt.
 *
 * Not a device: it makes power rather than drawing it, so it has no circuit and no id. Its state is the
 * facility's, FFacilityRules::IsGeneratorRunning and the rest, so this actor, the breaker switches and
 * the route solver agree about what the generator is doing, and a reset puts it back with everything
 * else.
 *
 * Components: Root is where the actor is placed. Housing is the generator body; it needs collision so
 * the Interactor's trace can hit it, and the mesh is assigned in the Blueprint.
 *
 * Whatever executes an interaction calls Use(): it starts the generator, or rigs it when it is running
 * and the player carries the pry bar.
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityGenerator : public AFacilityActorBase
{
	GENERATED_BODY()

public:

	AFacilityGenerator();

	/**
	 * What pressing interact on the generator does. Rigs it to overload when it is running and the player
	 * carries the pry bar, otherwise starts it. False when neither happened.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Generator")
	bool Use();

	/** Starts the generator through the facility state. False when the rules refused. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Generator")
	bool Start();

	/** Rigs the running generator to overload with the pry bar through the facility state. False when the rules refused. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Generator")
	bool Overload();

	UFUNCTION(BlueprintPure, Category = "Facility|Generator")
	bool IsRunning() const;

	UFUNCTION(BlueprintPure, Category = "Facility|Generator")
	int32 GetStepsLeft() const;

	UFUNCTION(BlueprintPure, Category = "Facility|Generator")
	bool IsOverloaded() const;

	UFUNCTION(BlueprintPure, Category = "Facility|Generator")
	bool CanStart() const;

	UFUNCTION(BlueprintPure, Category = "Facility|Generator")
	bool CanOverload() const;

protected:

	virtual void BeginPlay() override;
	virtual void OnFacilityStateChanged() override;
	virtual void RefreshDisplayData() override;

	/**
	 * The generator started or ran out. Also called once from BeginPlay with the starting state, and on
	 * every reset; bInstant is true for those two, so a start-up sound belongs on the calls where it is
	 * false. An overload stops it too, and fires OnOverloadedChanged after this.
	 */
	virtual void OnRunningChanged(bool bRunning, bool bInstant) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Generator", meta = (DisplayName = "On Running Changed"))
	void ReceiveRunningChanged(bool bRunning, bool bInstant);

	/**
	 * The generator was rigged to overload, or a reset put it back. Also called once from BeginPlay with
	 * the starting state; bInstant is true then and on a reset. For the blast and the wreck.
	 */
	virtual void OnOverloadedChanged(bool bOverloaded, bool bInstant) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Generator", meta = (DisplayName = "On Overloaded Changed"))
	void ReceiveOverloadedChanged(bool bOverloaded, bool bInstant);

	//
	// Components
	//

	/** The generator body. Needs collision so the Interactor's trace can hit it. The mesh is assigned in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Generator")
	TObjectPtr<UStaticMeshComponent> Housing;

private:

	/** Re-reads running and overloaded and tells the hooks when either differs from what they were last told. bInstant tells them regardless. */
	void Reconcile(bool bInstant);

	/** What the hooks were last told. They exist only to turn state changes into edges; the facility state is the answer. */
	bool bLastReportedRunning = false;
	bool bLastReportedOverloaded = false;
};
