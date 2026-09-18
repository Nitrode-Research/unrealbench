// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RTSHUD.generated.h"

/** C++ Canvas projection of Milestone 1 selection, order, health, and attack state. */
UCLASS()
class RTS_API ARTSHUD final : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void DrawHUD() override;
	void SetSelectionMarquee(const TOptional<FBox2D>& NewMarquee);

private:
	void DrawMarquee();
	void DrawMoveDestination();
	void DrawAttackTargets(TConstArrayView<class ARTSCombatUnit*> SelectedUnits);
	void DrawUnitAffiliation(const class ARTSCombatUnit& Unit, bool bSelected);
	void DrawStructureAffiliation(
		const class ARTSStructure& Structure,
		bool bSelected,
		bool bAttackTarget);
	void DrawAttackFeedback(const class ARTSCombatUnit& Unit);
	void DrawSelectedUnit(const class ARTSCombatUnit& Unit);
	void DrawDeploymentPreview();
	void DrawConstructionStatus();
	void DrawProductionStatus();
	void DrawSelectedStructures();
	void DrawTurretFeedback();
	void DrawMaterialDepositFeedback();
	void DrawWreckageFeedback();
	float GetUnitScreenRadius(const class ARTSCombatUnit& Unit) const;
	float GetViewportUIScale() const;

	TOptional<FBox2D> SelectionMarquee;
	TSharedPtr<class SWidget> StatusOverlayWidget;
	TWeakObjectPtr<class ULocalPlayer> OverlayLocalPlayer;
};
