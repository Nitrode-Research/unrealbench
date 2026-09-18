#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityActorBase.h"
#include "FacilityCoolantValve.generated.h"

class UStaticMeshComponent;

/**
 * The coolant valve in Plant. Seized: opening it takes the pry bar, and opening it floods the Plant
 * floor. While it stays open the vent fans cannot drain the water; closed by hand, the water already
 * down stays until a step finds the fans running. The flood itself is the facility's, see
 * FFacilityRules::IsPlantFlooded and what follows it, so the valve, the flood volume, the breakers
 * the water shorts and the route solver agree about it.
 *
 * Not a device: it draws no power. The pump the routing panel runs remotely is that panel's rule,
 * not this valve's, and puts the same water down. Whether a press does anything is
 * FFacilityRules::CanOpenCoolantValve or CanCloseCoolantValve, so the prompt and the press agree.
 *
 * Components: Root is where the actor is placed. Wheel is the handwheel; it needs collision so the
 * Interactor's trace can hit it, and the mesh is assigned in the Blueprint. Turn it in On Open Changed.
 *
 * Whatever executes an interaction calls Use().
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityCoolantValve : public AFacilityActorBase
{
	GENERATED_BODY()

public:

	AFacilityCoolantValve();

	/**
	 * What pressing interact on the valve does. Forces it open with the pry bar while it is closed, which
	 * floods Plant, and closes it by hand while it is open. False when neither happened.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Coolant Valve")
	bool Use();

	/** Forces the valve open with the pry bar through the facility state. False when the rules refused. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Coolant Valve")
	bool Open();

	/** Closes the valve through the facility state. False when it is closed already. */
	UFUNCTION(BlueprintCallable, Category = "Facility|Coolant Valve")
	bool Close();

	/** Open according to the facility state. */
	UFUNCTION(BlueprintPure, Category = "Facility|Coolant Valve")
	bool IsOpen() const;

	/** True when the pry bar would open it right now. */
	UFUNCTION(BlueprintPure, Category = "Facility|Coolant Valve")
	bool CanOpen() const;

protected:

	virtual void BeginPlay() override;
	virtual void OnFacilityStateChanged() override;
	virtual void RefreshDisplayData() override;

	/**
	 * The valve opened or closed. Also called once from BeginPlay with the starting state, and on every
	 * reset; bInstant is true for those two, so the wheel snaps rather than turns and no sound plays.
	 */
	virtual void OnOpenChanged(bool bOpen, bool bInstant) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Coolant Valve", meta = (DisplayName = "On Open Changed"))
	void ReceiveOpenChanged(bool bOpen, bool bInstant);

	//
	// Components
	//

	/** The handwheel. Needs collision so the Interactor's trace can hit it. The mesh is assigned in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Coolant Valve")
	TObjectPtr<UStaticMeshComponent> Wheel;

private:

	/** Re-reads IsOpen and tells the hooks when it differs from what they were last told. bInstant tells them regardless. */
	void ReconcileOpen(bool bInstant);

	/** What the hooks were last told. Exists only to turn state changes into edges; IsOpen reads the facility state. */
	bool bLastReportedOpen = false;
};
