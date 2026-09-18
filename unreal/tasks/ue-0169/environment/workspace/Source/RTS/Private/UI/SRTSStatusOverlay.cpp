// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SRTSStatusOverlay.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
struct FRTSHUDLayoutMetrics
{
	float TightGap = 4.0f;
	float ItemGap = 8.0f;
	float SectionGap = 14.0f;
	FMargin EdgeInset = FMargin(18.0f, 16.0f);
	FMargin ResourcePadding = FMargin(12.0f, 8.0f);
	FMargin PanelPadding = FMargin(14.0f, 12.0f);
	FMargin ActionPadding = FMargin(10.0f, 8.0f);
	FMargin ResultPadding = FMargin(32.0f, 26.0f);
	FSlateFontInfo ResourceLabelFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10.0f);
	FSlateFontInfo ResourceValueFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 18.0f);
	FSlateFontInfo CaptionFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10.0f);
	FSlateFontInfo PanelTitleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16.0f);
	FSlateFontInfo BodyFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13.0f);
	FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11.0f);
	FSlateFontInfo ActionKeyFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12.0f);
	FSlateFontInfo ActionLabelFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11.0f);
	FSlateFontInfo GlyphFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 25.0f);
	FSlateFontInfo ResultTitleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 34.0f);
	FSlateFontInfo ResultStatusFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 18.0f);
};

const FLinearColor PanelSurface(0.025f, 0.055f, 0.085f, 0.95f);
const FLinearColor PanelSurfaceRaised(0.045f, 0.085f, 0.125f, 0.97f);
const FLinearColor PanelBorder(0.22f, 0.31f, 0.40f, 0.95f);
const FLinearColor PrimaryText(0.91f, 0.95f, 0.98f, 1.0f);
const FLinearColor SecondaryText(0.64f, 0.73f, 0.81f, 1.0f);
const FLinearColor PlayerBlue(0.16f, 0.42f, 0.95f, 1.0f);

FSlateColor ToneColor(const ERTSHUDMessageTone Tone)
{
	switch (Tone)
	{
	case ERTSHUDMessageTone::Positive: return FLinearColor(0.28f, 0.92f, 0.48f);
	case ERTSHUDMessageTone::Warning: return FLinearColor(1.0f, 0.76f, 0.20f);
	case ERTSHUDMessageTone::Critical: return FLinearColor(1.0f, 0.34f, 0.27f);
	case ERTSHUDMessageTone::Neutral:
	default: return SecondaryText;
	}
}

TSharedRef<SWidget> MakeFrame(
	const TSharedRef<SWidget>& Content,
	const FMargin ContentPadding,
	const FLinearColor Surface = PanelSurface)
{
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(PanelBorder)
		.Padding(1.0f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(Surface)
			.Padding(ContentPadding)
			[
				Content
			]
		];
}

TSharedRef<SWidget> MakeResourceCell(
	const FText Label,
	const FLinearColor MarkerColor,
	const FVector2D MarkerSize,
	const TAttribute<FText>& Value,
	const TAttribute<FText>& Caption,
	const TAttribute<FSlateColor>& ValueColor,
	const FRTSHUDLayoutMetrics& Metrics)
{
	return MakeFrame(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(MarkerSize.X).HeightOverride(MarkerSize.Y)
			[
				SNew(SColorBlock).Color(MarkerColor)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(Metrics.ItemGap, 0.0f, 0.0f, 0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(Label).ColorAndOpacity(SecondaryText).Font(Metrics.ResourceLabelFont)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(Value).ColorAndOpacity(ValueColor).Font(Metrics.ResourceValueFont)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(Caption).ColorAndOpacity(SecondaryText).Font(Metrics.CaptionFont)
			]
		],
		Metrics.ResourcePadding);
}
}

void SRTSStatusOverlay::Construct(const FArguments& Arguments)
{
	// Restore this contractor-owned implementation.
}

FRTSHUDSnapshot SRTSStatusOverlay::ReadSnapshot() const
{
	return SnapshotAttribute.IsSet() ? SnapshotAttribute.Get() : FRTSHUDSnapshot();
}

FText SRTSStatusOverlay::GetMaterialsValueText() const
{
	return FText::AsNumber(ReadSnapshot().Economy.Materials);
}

FText SRTSStatusOverlay::GetPowerValueText() const
{
	return FText::AsNumber(ReadSnapshot().Economy.PowerGeneration);
}

FText SRTSStatusOverlay::GetPowerCaptionText() const
{
	return FText::FromString(FString::Printf(TEXT("%d REQUIRED"), ReadSnapshot().Economy.PowerDemand));
}

FText SRTSStatusOverlay::GetSupplyValueText() const
{
	// Restore this contractor-owned implementation.
	return {};
}

FText SRTSStatusOverlay::GetSupplyCaptionText() const
{
	return FText::FromString(FString::Printf(TEXT("%d QUEUED"), ReadSnapshot().Economy.SupplyReserved));
}

FSlateColor SRTSStatusOverlay::GetPowerColor() const
{
	return ReadSnapshot().Economy.IsInBrownout() ? ToneColor(ERTSHUDMessageTone::Critical) : PrimaryText;
}

FText SRTSStatusOverlay::GetSelectionTitle() const { return FText::FromString(ReadSnapshot().SelectionTitle); }
FText SRTSStatusOverlay::GetSelectionDetail() const { return FText::FromString(ReadSnapshot().SelectionDetail); }

FText SRTSStatusOverlay::GetSelectionGlyph() const
{
	// Restore this contractor-owned implementation.
	return {};
}

FSlateColor SRTSStatusOverlay::GetSelectionAccent() const
{
	// Restore this contractor-owned implementation.
	return {};
}

TOptional<float> SRTSStatusOverlay::GetHealthFraction() const
{
	// Restore this contractor-owned implementation.
	return {};
}

FText SRTSStatusOverlay::GetQueueText() const { return FText::FromString(ReadSnapshot().QueueText); }
FText SRTSStatusOverlay::GetAvailableActionsText() const { return FText::FromString(ReadSnapshot().AvailableActionsText); }

FText SRTSStatusOverlay::GetStatusHeading() const
{
	return FText::FromString(ReadSnapshot().WarningText.IsEmpty() ? TEXT("FIELD STATUS") : TEXT("ALERT"));
}

FText SRTSStatusOverlay::GetStatusPrimaryText() const
{
	// Restore this contractor-owned implementation.
	return {};
}

FText SRTSStatusOverlay::GetStatusSecondaryText() const
{
	// Restore this contractor-owned implementation.
	return {};
}

FSlateColor SRTSStatusOverlay::GetStatusColor() const
{
	// Restore this contractor-owned implementation.
	return {};
}

FText SRTSStatusOverlay::GetActionHotkey(const int32 ActionIndex) const
{
	// Restore this contractor-owned implementation.
	return {};
}

FText SRTSStatusOverlay::GetActionLabel(const int32 ActionIndex) const
{
	// Restore this contractor-owned implementation.
	return {};
}

EVisibility SRTSStatusOverlay::GetActionVisibility(const int32 ActionIndex) const
{
	return ReadSnapshot().Actions.IsValidIndex(ActionIndex) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SRTSStatusOverlay::GetActionPanelVisibility() const
{
	return ReadSnapshot().Actions.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SRTSStatusOverlay::GetEmptyActionSpacerVisibility() const
{
	return ReadSnapshot().Actions.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SRTSStatusOverlay::GetResultTitle() const { return FText::FromString(ReadSnapshot().ResultTitle); }
FText SRTSStatusOverlay::GetResultReason() const { return FText::FromString(ReadSnapshot().ResultReason); }
FText SRTSStatusOverlay::GetResultDuration() const { return FText::FromString(ReadSnapshot().ResultDuration); }
FText SRTSStatusOverlay::GetPlayerResultStatistics() const { return FText::FromString(ReadSnapshot().PlayerResultStatistics); }
FText SRTSStatusOverlay::GetEnemyResultStatistics() const { return FText::FromString(ReadSnapshot().EnemyResultStatistics); }

FSlateColor SRTSStatusOverlay::GetResultColor() const
{
	// Restore this contractor-owned implementation.
	return {};
}

EVisibility SRTSStatusOverlay::GetSelectionDetailVisibility() const
{
	return ReadSnapshot().SelectionDetail.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SRTSStatusOverlay::GetHealthVisibility() const
{
	return GetHealthFraction().IsSet() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SRTSStatusOverlay::GetQueueVisibility() const
{
	return ReadSnapshot().QueueText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SRTSStatusOverlay::GetResultVisibility() const
{
	return ReadSnapshot().Match.State == ERTSMatchState::Resolved ? EVisibility::Visible : EVisibility::Collapsed;
}
