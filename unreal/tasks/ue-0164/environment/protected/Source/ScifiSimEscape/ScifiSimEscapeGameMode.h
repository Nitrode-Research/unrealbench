// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Facility/FacilityTypes.h"
#include "ScifiSimEscapeGameMode.generated.h"

/**
 * Simple GameMode for a first person game, and where an attempt ends.
 *
 * The win is facility state: FFacilityState::EscapedThrough, set once by an AFacilityExit when the
 * player passes through an open exit. The game mode listens for it the way every actor listens for
 * state. When it flips, the player's move and look input are switched off and OnEscaped runs, which
 * is where the Blueprint shows what winning looks like. A reset clears it and hands input back.
 */
UCLASS(abstract)
class AScifiSimEscapeGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AScifiSimEscapeGameMode();

	/** True once the player has passed through an exit, as the facility state has it. */
	UFUNCTION(BlueprintPure, Category = "Facility")
	bool HasEscaped() const;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * The player passed through an exit. Move and look input are already off. Show the win here: a
	 * widget, the cursor, a way to restart. Not called for a reset; that only hands input back.
	 */
	virtual void OnEscaped(EFacilityExit Exit) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Facility", meta = (DisplayName = "On Escaped"))
	void ReceiveEscaped(EFacilityExit Exit);

private:

	UFUNCTION()
	void HandleFacilityStateChanged();

	/** Switches every player's move and look input off or back on. */
	void SetPlayersFrozen(bool bFrozen);

	/**
	 * What the hooks were last told. Exists only to turn state changes into edges. It is not the win;
	 * the facility state is, and HasEscaped reads that.
	 */
	bool bLastReportedEscaped = false;
};
