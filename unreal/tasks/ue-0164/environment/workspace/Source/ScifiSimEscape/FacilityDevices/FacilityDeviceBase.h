#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityActorBase.h"
#include "Facility/FacilityTypes.h"
#include "FacilityDeviceBase.generated.h"

/**
 * Base class for everything in the facility that runs on power: lights, doors, the elevator, the
 * cargo lift, the fans, the camera, consoles. Everything the player can use that has no power, like
 * the hatch, the ladder or a call panel, derives from AFacilityActorBase instead, which is also
 * where this class gets its root, its Interactable and its focus, state and display hooks.
 *
 * A device owns nothing about its power. On BeginPlay it registers its id and default circuit with
 * UFacilityStateSubsystem. On every state change it asks the subsystem IsDevicePowered again and
 * tells the child through OnPowerChanged only when the answer flipped, plus once at BeginPlay with
 * the starting answer, so a light or a fan can put itself in the right state without polling.
 * Anything else a child reads from the facility (its circuit, lock state, alarm) arrives through
 * OnFacilityStateChanged, which fires on every change after power has been reconciled.
 *
 * Being unpowered does not make a device non-interactable. A dead door can still be pried and a
 * dead lift's shaft can still be climbed. What a device offers in each state is the child's call.
 */
UCLASS(Abstract, Blueprintable)
class SCIFISIMESCAPE_API AFacilityDeviceBase : public AFacilityActorBase
{
	GENERATED_BODY()

public:

	/** Id of this device in the facility state. Unique across the level, unless the device shares it like every light in a zone. */
	UFUNCTION(BlueprintPure, Category = "Facility|Device")
	FName GetDeviceId() const { return DeviceId; }

	/**
	 * Handheld level this device needs, as the facility state has it from the settings by id. 0 when
	 * the handheld has nothing to work on here, or outside a running game.
	 */
	UFUNCTION(BlueprintPure, Category = "Facility|Device")
	int32 GetHackLevel() const;

	/** Circuit the device draws from right now, as the facility state has it. None outside a running game. */
	UFUNCTION(BlueprintPure, Category = "Facility|Device")
	EFacilityCircuit GetCircuit() const;

	/** True when the device's circuit is live. Read from the facility state on every call, never cached. False outside a running game. */
	UFUNCTION(BlueprintPure, Category = "Facility|Device")
	bool IsPowered() const;

	/**
	 * Why a device on this circuit does nothing, for prompts: "DOORS is off", or "Not wired" for None.
	 * Static so things that are not devices, like the call panel of a lift, can describe the device they serve.
	 */
	UFUNCTION(BlueprintPure, Category = "Facility|Device")
	static FText DescribeNoPower(EFacilityCircuit Circuit);

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PreFacilityStateChanged() override;

	/**
	 * The device's power came on or went off.
	 *
	 * Also called once from AFacilityDeviceBase::BeginPlay with the starting state, after Blueprint
	 * BeginPlay has run, so a child initialises from it instead of repeating the logic. A child that
	 * needs setup before that first call does it in the constructor or before calling
	 * Super::BeginPlay(). Fires before OnFacilityStateChanged for the same change.
	 */
	virtual void OnPowerChanged(bool bPowered) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Device", meta = (DisplayName = "On Power Changed"))
	void ReceivePowerChanged(bool bPowered);

	/**
	 * Whether being wired to no circuit is a mistake worth a warning when play begins. It is for nearly
	 * everything: a fan or a lift on None can never run. A door says no when it is mechanical, like a locker.
	 */
	virtual bool NeedsCircuit() const { return true; }

	/**
	 * Whether several actors of this class may be placed under one id and share its state, like every
	 * light in a zone or the two vent fans. Devices whose state is their own, like a door or a lift,
	 * return false.
	 */
	virtual bool SharesDeviceId() const { return false; }

	//
	// Configuration
	//

	/**
	 * Key into the facility state. Unique per device, unless the class shares ids (SharesDeviceId): then
	 * every actor placed under one id is the same device, powered and rerouted together on the circuit
	 * the id was first placed on, like every light in Storage under Lights_Storage. Two actors on one id
	 * where either does not share still share one state, and log an error.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Device")
	FName DeviceId;

	/**
	 * Circuit the device is wired to when the level starts. Only a default: the circuit-routing panel
	 * can move the device afterwards, and the facility state is the authority from then on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Device")
	EFacilityCircuit DefaultCircuit = EFacilityCircuit::EFC_None;

	//
	// Not here: the handheld level the device needs. That is tuning, so it lives in the facility
	// settings (DefaultGame.ini, HackTargets) keyed by DeviceId, and the subsystem seeds it when the
	// device registers. GetHackLevel reads it back. What a hack does is the device's own rule: a door
	// opens, the elevator stops, the camera loops, the gate controller opens the gate.
	//

private:

	/** Re-reads power from the facility state and tells the child when it differs from what it was last told. */
	void ReconcilePower(bool bForceNotify);

	/**
	 * What the child was last told through OnPowerChanged. Exists only to turn state changes into
	 * edges. It is not the device's power; IsPowered() is, and that reads the facility state.
	 */
	bool bLastReportedPowered = false;
};
