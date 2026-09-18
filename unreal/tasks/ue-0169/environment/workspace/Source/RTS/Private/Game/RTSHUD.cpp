// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/RTSHUD.h"

#include "Combat/RTSCombatTypes.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSTurretCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Configuration/RTSMilestone2Configuration.h"
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/UserInterfaceSettings.h"
#include "EngineUtils.h"
#include "Game/RTSPlayerController.h"
#include "Orders/RTSCommandSubsystem.h"
#include "Orders/RTSOrderTypes.h"
#include "Orders/RTSUnitOrderComponent.h"
#include "Production/RTSProductionComponent.h"
#include "Reclaim/RTSReclaimComponent.h"
#include "Reclaim/RTSWreckage.h"
#include "Selection/RTSSelectionSubsystem.h"
#include "Structures/RTSDeploymentViewSubsystem.h"
#include "Structures/RTSStructure.h"
#include "Structures/RTSStructureSubsystem.h"
#include "UI/RTSPresentationText.h"
#include "UI/SRTSStatusOverlay.h"
#include "Units/RTSCombatUnit.h"
#include "Units/RTSTeams.h"
#include "World/RTSMaterialDeposit.h"
#include "World/RTSStartingZone.h"

void ARTSHUD::BeginPlay()
{
	// Restore this contractor-owned implementation.
	Super::BeginPlay();
}

void ARTSHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Restore this contractor-owned implementation.
	Super::EndPlay(EndPlayReason);
}

void ARTSHUD::DrawHUD()
{
	// Restore this contractor-owned implementation.
}

void ARTSHUD::DrawTurretFeedback()
{
	// Restore this contractor-owned implementation.
}

void ARTSHUD::DrawMaterialDepositFeedback()
{
	// Restore this contractor-owned implementation.
}

void ARTSHUD::DrawWreckageFeedback()
{
	// Restore this contractor-owned implementation.
}

void ARTSHUD::DrawSelectedStructures()
{
	// Restore this contractor-owned implementation.
}

void ARTSHUD::DrawConstructionStatus()
{
	// Restore this contractor-owned implementation.
}

void ARTSHUD::DrawProductionStatus()
{
	// Restore this contractor-owned implementation.
}

void ARTSHUD::DrawDeploymentPreview()
{
	// Restore this contractor-owned implementation.
}

void ARTSHUD::DrawAttackTargets(const TConstArrayView<ARTSCombatUnit*> SelectedUnits)
{
	// Restore this contractor-owned implementation.
}

void ARTSHUD::DrawMoveDestination()
{
	// Restore this contractor-owned implementation.
}

void ARTSHUD::DrawUnitAffiliation(const ARTSCombatUnit& Unit, const bool bSelected)
{
	// Restore this contractor-owned implementation.
}

void ARTSHUD::DrawStructureAffiliation(
	const ARTSStructure& Structure,
	const bool bSelected,
	const bool bAttackTarget)
{
	// Restore this contractor-owned implementation.
}

void ARTSHUD::DrawAttackFeedback(const ARTSCombatUnit& Unit)
{
	// Restore this contractor-owned implementation.
}

void ARTSHUD::SetSelectionMarquee(const TOptional<FBox2D>& NewMarquee)
{
	// Restore this contractor-owned implementation.
}

void ARTSHUD::DrawMarquee()
{
	// Restore this contractor-owned implementation.
}

void ARTSHUD::DrawSelectedUnit(const ARTSCombatUnit& Unit)
{
	// Restore this contractor-owned implementation.
}

float ARTSHUD::GetUnitScreenRadius(const ARTSCombatUnit& Unit) const
{
	// Restore this contractor-owned implementation.
	return {};
}

float ARTSHUD::GetViewportUIScale() const
{
	// Restore this contractor-owned implementation.
	return {};
}
