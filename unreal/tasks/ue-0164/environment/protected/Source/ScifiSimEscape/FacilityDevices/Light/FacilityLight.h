#pragma once

#include "CoreMinimal.h"
#include "FacilityDevices/FacilityDeviceBase.h"
#include "FacilityLight.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;

/**
 * A light fixture. The lights of a zone are one device, the way the GDD wires them: every fixture on
 * Level 1 is placed under Lights_Level1 and every one in Storage under Lights_Storage, both on DOORS,
 * and every one in the basement under Lights_Plant on PLANT. The facility state then holds one routing
 * entry per zone, so a breaker or the routing panel puts a whole zone out at once, and stealth asks
 * whether a zone is lit rather than whether a lamp is.
 *
 * Whether the fixture is lit is FFacilityRules::AreLightsOn over its id, so this actor and whatever
 * else asks whether a zone is lit, a guard, the camera or the route solver, answer the same question.
 *
 * Components: Root is where the fixture is placed. Fixture is the lamp's mesh, without collision: a
 * lamp is nothing the player uses, so it must not block the player or catch the Interactor's trace.
 * The mesh is assigned in the Blueprint. Light is a point light. Every light component on the actor is
 * switched with the zone, so spot or rect lights a Blueprint child adds follow it too.
 *
 * To place a zone, give each of its fixtures the same DeviceId and the circuit its lights run on. The
 * default is DOORS, for Level 1 and Storage; fixtures in Plant get PLANT on the instance or in a
 * Blueprint. A zone keeps the circuit its first fixture was placed on, and a fixture placed on another
 * logs a warning.
 *
 * There is nothing to use on a light, and without collision the player cannot focus one.
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityLight : public AFacilityDeviceBase
{
	GENERATED_BODY()

public:

	AFacilityLight();

	/** True when the facility rules say the fixture's zone is lit. False outside a running game. */
	UFUNCTION(BlueprintPure, Category = "Facility|Light")
	bool IsLit() const;

protected:

	virtual void BeginPlay() override;
	virtual void OnFacilityStateChanged() override;
	virtual void RefreshDisplayData() override;

	/** Every fixture in a zone is placed under the zone's id, and together they are one device. */
	virtual bool SharesDeviceId() const override { return true; }

	/**
	 * The fixture came on or went out, for an emissive material swap or a hum. Also called once from
	 * BeginPlay with the starting state, and on every reset; bInstant is true for those two, so a
	 * flicker or a switching sound belongs on the calls where it is false. Every light component on the
	 * actor is already switched when this fires.
	 */
	virtual void OnLitChanged(bool bLit, bool bInstant) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility|Light", meta = (DisplayName = "On Lit Changed"))
	void ReceiveLitChanged(bool bLit, bool bInstant);

	//
	// Components
	//

	/** The lamp's mesh. No collision, so it never blocks the player or catches the Interactor's trace. The mesh is assigned in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Light")
	TObjectPtr<UStaticMeshComponent> Fixture;

	/** The fixture's light. Switched with the zone, like every other light component on the actor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Light")
	TObjectPtr<UPointLightComponent> Light;

private:

	/**
	 * Re-reads IsLit and reacts when it differs from what the fixture last reacted to: every light
	 * component on the actor is switched to match and the hooks fire. With bInstant it always reacts.
	 */
	void ReconcileLit(bool bInstant);

	/**
	 * Whether the fixture was lit when it last reacted. Exists only to turn state changes into edges. It
	 * is not whether the zone is lit; IsLit is, and that asks the facility rules.
	 */
	bool bLastReportedLit = false;
};
