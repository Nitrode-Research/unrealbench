// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/RTSHUDSnapshot.h"
#include "Widgets/SCompoundWidget.h"

/** Content-driven Slate projection of one ephemeral HUD snapshot. */
class RTS_API SRTSStatusOverlay final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRTSStatusOverlay) {}
		SLATE_ATTRIBUTE(FRTSHUDSnapshot, Snapshot)
	SLATE_END_ARGS()

	void Construct(const FArguments& Arguments);

private:
	FText GetMaterialsValueText() const;
	FText GetPowerValueText() const;
	FText GetPowerCaptionText() const;
	FText GetSupplyValueText() const;
	FText GetSupplyCaptionText() const;
	FSlateColor GetPowerColor() const;
	FText GetSelectionTitle() const;
	FText GetSelectionDetail() const;
	FText GetSelectionGlyph() const;
	FSlateColor GetSelectionAccent() const;
	TOptional<float> GetHealthFraction() const;
	FText GetQueueText() const;
	FText GetAvailableActionsText() const;
	FText GetStatusHeading() const;
	FText GetStatusPrimaryText() const;
	FText GetStatusSecondaryText() const;
	FSlateColor GetStatusColor() const;
	FText GetActionHotkey(int32 ActionIndex) const;
	FText GetActionLabel(int32 ActionIndex) const;
	EVisibility GetActionVisibility(int32 ActionIndex) const;
	EVisibility GetActionPanelVisibility() const;
	EVisibility GetEmptyActionSpacerVisibility() const;
	FText GetResultTitle() const;
	FText GetResultReason() const;
	FText GetResultDuration() const;
	FText GetPlayerResultStatistics() const;
	FText GetEnemyResultStatistics() const;
	FSlateColor GetResultColor() const;
	EVisibility GetSelectionDetailVisibility() const;
	EVisibility GetHealthVisibility() const;
	EVisibility GetQueueVisibility() const;
	EVisibility GetResultVisibility() const;

	FRTSHUDSnapshot ReadSnapshot() const;
	TAttribute<FRTSHUDSnapshot> SnapshotAttribute;
};
