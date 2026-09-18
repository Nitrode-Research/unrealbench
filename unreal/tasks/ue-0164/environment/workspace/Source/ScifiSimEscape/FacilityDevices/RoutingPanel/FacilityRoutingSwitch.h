#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityDeviceBase.h"
#include "Facility/FacilityTypes.h"
#include "FacilityRoutingSwitch.generated.h"

class UStaticMeshComponent;

/**
 * One switch on the circuit-routing panel in the Lab. The panel is a handheld target, at level 1 in
 * the GDD, and it does three things: puts the cargo lift on SECURITY so it runs without the fans, puts
 * the fans on SECURITY so PLANT can be live without them, and runs the coolant pump remotely, which
 * floods Plant without the valve. Each is a switch of this class with its job in Option, placed on
 * the panel the way the breaker panel is an APanelSwitch per circuit. Every switch shares the panel's
 * DeviceId, so the panel has one hack level, from the settings by that id, and one circuit.
 *
 * The panel carries one patch at a time: routing a second device sends the first back to the circuit
 * it was placed on. A routing switch is a toggle. While its device is elsewhere it moves it onto its
 * circuit; while the panel has its device on that circuit it sends it home. The pump switch floods
 * Plant once; standing water leaves it nothing to do.
 *
 * Whether the handheld has something to work on here is FFacilityRules::CanRouteDevice or
 * CanRunCoolantPump over the panel's id and the switch's job, so the prompt, the press and the route
 * solver agree. There is nothing to use by hand. Looking at the switch says where its device is, or
 * whether the floor is flooded, and the hack press arrives through OnHacked and calls Hack().
 *
 * Cosmetics belong in the Blueprint child. Implement On Switch Changed to move the lever: on while
 * the device sits on the switch's circuit, or while Plant is flooded for the pump. The mesh needs
 * collision so the Interactor's trace can hit it, and that collision has to sit in front of the
 * panel's, because the Interactor's line-of-sight trace does not ignore the panel.
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityRoutingSwitch : public AFacilityDeviceBase
{
	GENERATED_BODY()

public:

	AFacilityRoutingSwitch();

	/**
	 * Works the handheld on the switch through the facility state. A hack the handheld reaches does the
	 * switch's job silently; one it cannot reach fails and raises the alarm. False when the handheld had
	 * nothing to work on.
	 */
	UFUNCTION(BlueprintCallable, Category = "Facility|Routing Switch")
	bool Hack();

	/** True when the handheld has something to work on here, whether or not it would succeed. */
	UFUNCTION(BlueprintPure, Category = "Facility|Routing Switch")
	bool CanHack() const;

	/** True while the switch is thrown: its device sits on its circuit, or Plant is flooded for the pump. Follows the facility state, not presses. */
	UFUNCTION(BlueprintPure, Category = "Facility|Routing Switch")
	bool IsOn() const;

	/** The switch's job. */
	UFUNCTION(BlueprintPure, Category = "Facility|Routing Switch")
	FRoutingOption GetOption() const { return Option; }

protected:

	virtual void BeginPlay() override;
	virtual void OnFacilityStateChanged() override;
	virtual void RefreshDisplayData() override;
	virtual void OnHacked(AActor* Hacker) override;

	/** Every switch of one panel is placed under the panel's id. */
	virtual bool SharesDeviceId() const override { return true; }

	/**
	 * The switch went on or off. Move the lever here.
	 *
	 * Also called once when the switch begins play, with bInstant set, so the lever can snap to its
	 * starting position instead of animating there, and on every reset. Every other call is a real
	 * change, including ones this switch did not cause, such as another switch displacing its device.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Routing Switch", meta = (DisplayName = "On Switch Changed"))
	void ReceiveSwitchChanged(bool bOn, bool bInstant);

	//
	// Configuration
	//

	/** What this switch does: which device it routes onto which circuit, or that it runs the pump. Set on each placed switch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Routing Switch")
	FRoutingOption Option;

	/** How prompts name the device, like "cargo lift" or "vent fans". The device id when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Routing Switch")
	FText DeviceLabel;

	//
	// Components
	//

	/** What the player sees and looks at. Needs collision, see the class comment. The mesh is assigned in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Routing Switch")
	TObjectPtr<UStaticMeshComponent> Mesh;

private:

	/** The device's name for prompts: DeviceLabel, or the id. */
	FText GetDeviceLabel() const;

	/** Re-reads IsOn and tells Blueprint when it differs from what it was last told. bInstant tells it regardless. */
	void ReconcileSwitch(bool bInstant);

	/** What Blueprint was last told. Exists only to turn state changes into edges; IsOn reads the facility state. */
	bool bLastReportedOn = false;
};
