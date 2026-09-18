#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FacilityLightZone.generated.h"

class UBoxComponent;

/**
 * The space one lights zone lights: a volume over a room, naming the zone's device id, Level1_Light
 * for the Corridor or Plant_Light for the basement. The fixtures say whether the zone is lit,
 * FFacilityRules::AreLightsOn over that id; this volume says who stands in it. UStealthComponent on
 * the player keeps track of the zones it overlaps, and a player in no lit zone is in the dark.
 *
 * Each room lit by a zone gets a volume, or one volume across several rooms of the same zone. Where
 * two overlap, in a doorway, a lit one wins. Space no volume covers, a shaft or the vent, counts as dark.
 *
 * Not a facility actor: nothing to look at or use, and it never asks the facility itself.
 *
 * Components: Root is where the actor is placed. Volume is the box, overlapping pawns and nothing
 * else; size it to the room.
 */
UCLASS()
class SCIFISIMESCAPE_API AFacilityLightZone : public AActor
{
	GENERATED_BODY()

public:

	AFacilityLightZone();

	/** Device id of the lights zone this volume belongs to. */
	UFUNCTION(BlueprintPure, Category = "Facility|Light Zone")
	FName GetLightsId() const { return LightsId; }

	/** True while the facility says the zone's lights are on. False outside a running game. */
	UFUNCTION(BlueprintPure, Category = "Facility|Light Zone")
	bool IsLit() const;

protected:

	virtual void BeginPlay() override;

	//
	// Configuration
	//

	/** Device id shared by every fixture of the zone, like Level1_Light or Plant_Light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility|Light Zone")
	FName LightsId;

	//
	// Components
	//

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Light Zone")
	TObjectPtr<USceneComponent> Root;

	/** The lit space. Overlaps pawns and nothing else. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Facility|Light Zone")
	TObjectPtr<UBoxComponent> Volume;
};
