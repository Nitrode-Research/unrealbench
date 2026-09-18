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
	SnapshotAttribute = Arguments._Snapshot;
	SetVisibility(EVisibility::HitTestInvisible);
	const FRTSHUDLayoutMetrics Metrics;

	const TSharedRef<SUniformGridPanel> ActionGrid = SNew(SUniformGridPanel)
		.SlotPadding(FMargin(Metrics.TightGap * 0.5f));
	for (int32 ActionIndex = 0; ActionIndex < 6; ++ActionIndex)
	{
		ActionGrid->AddSlot(ActionIndex % 3, ActionIndex / 3)
		[
			SNew(SBorder)
			.Visibility_Lambda([this, ActionIndex]() { return GetActionVisibility(ActionIndex); })
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(PanelBorder)
			.Padding(1.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(PanelSurfaceRaised)
				.Padding(Metrics.ActionPadding)
				[
					SNew(SBox).MinDesiredWidth(72.0f).MinDesiredHeight(42.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Text_Lambda([this, ActionIndex]() { return GetActionHotkey(ActionIndex); })
							.ColorAndOpacity(FLinearColor(0.48f, 0.72f, 1.0f))
							.Font(Metrics.ActionKeyFont)
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, Metrics.TightGap, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([this, ActionIndex]() { return GetActionLabel(ActionIndex); })
							.ColorAndOpacity(PrimaryText)
							.Font(Metrics.ActionLabelFont)
						]
					]
				]
			]
		];
	}

	const TSharedRef<SWidget> Resources = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeResourceCell(
				FText::FromString(TEXT("MATERIALS")),
				FLinearColor(0.65f, 0.70f, 0.76f),
				FVector2D(14.0f, 14.0f),
				TAttribute<FText>::CreateLambda([this]() { return GetMaterialsValueText(); }),
				FText::FromString(TEXT("AVAILABLE")),
				PrimaryText,
				Metrics)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeResourceCell(
				FText::FromString(TEXT("POWER")),
				FLinearColor(1.0f, 0.78f, 0.12f),
				FVector2D(6.0f, 22.0f),
				TAttribute<FText>::CreateLambda([this]() { return GetPowerValueText(); }),
				TAttribute<FText>::CreateLambda([this]() { return GetPowerCaptionText(); }),
				TAttribute<FSlateColor>::CreateLambda([this]() { return GetPowerColor(); }),
				Metrics)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeResourceCell(
				FText::FromString(TEXT("SUPPLY")),
				FLinearColor(0.94f, 0.20f, 0.24f),
				FVector2D(12.0f, 20.0f),
				TAttribute<FText>::CreateLambda([this]() { return GetSupplyValueText(); }),
				TAttribute<FText>::CreateLambda([this]() { return GetSupplyCaptionText(); }),
				PrimaryText,
				Metrics)
		];

	const TSharedRef<SWidget> StatusPanel = MakeFrame(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Text(this, &SRTSStatusOverlay::GetStatusHeading).ColorAndOpacity(SecondaryText).Font(Metrics.ResourceLabelFont)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, Metrics.TightGap, 0.0f, 0.0f)
		[
			SNew(STextBlock).Text(this, &SRTSStatusOverlay::GetStatusPrimaryText).ColorAndOpacity(this, &SRTSStatusOverlay::GetStatusColor).Font(Metrics.BodyFont).WrapTextAt(350.0f)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, Metrics.TightGap, 0.0f, 0.0f)
		[
			SNew(STextBlock).Text(this, &SRTSStatusOverlay::GetStatusSecondaryText).ColorAndOpacity(SecondaryText).Font(Metrics.SmallFont).WrapTextAt(350.0f)
		],
		Metrics.PanelPadding);

	const TSharedRef<SWidget> SelectionPanel = MakeFrame(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)
		[
			SNew(SBox).MinDesiredWidth(104.0f).MinDesiredHeight(82.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(this, &SRTSStatusOverlay::GetSelectionAccent)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(this, &SRTSStatusOverlay::GetSelectionGlyph).ColorAndOpacity(PrimaryText).Font(Metrics.GlyphFont)
				]
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(Metrics.SectionGap, 0.0f, 0.0f, 0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(this, &SRTSStatusOverlay::GetSelectionTitle).ColorAndOpacity(PrimaryText).Font(Metrics.PanelTitleFont).AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, Metrics.TightGap, 0.0f, 0.0f)
			[
				SNew(STextBlock).Text(this, &SRTSStatusOverlay::GetSelectionDetail).Visibility(this, &SRTSStatusOverlay::GetSelectionDetailVisibility).ColorAndOpacity(SecondaryText).Font(Metrics.BodyFont).AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, Metrics.ItemGap, 0.0f, 0.0f)
			[
				SNew(SProgressBar).Visibility(this, &SRTSStatusOverlay::GetHealthVisibility).Percent(this, &SRTSStatusOverlay::GetHealthFraction).FillColorAndOpacity(FLinearColor(0.16f, 0.88f, 0.34f))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, Metrics.ItemGap, 0.0f, 0.0f)
			[
				SNew(STextBlock).Text(this, &SRTSStatusOverlay::GetQueueText).Visibility(this, &SRTSStatusOverlay::GetQueueVisibility).ColorAndOpacity(FLinearColor(0.48f, 0.78f, 1.0f)).Font(Metrics.SmallFont).AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, Metrics.TightGap, 0.0f, 0.0f)
			[
				SNew(STextBlock).Text(this, &SRTSStatusOverlay::GetAvailableActionsText).ColorAndOpacity(SecondaryText).Font(Metrics.SmallFont).AutoWrapText(true)
			]
		],
		Metrics.PanelPadding);

	const TSharedRef<SWidget> ResultPanel = MakeFrame(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(STextBlock).Text(this, &SRTSStatusOverlay::GetResultTitle).ColorAndOpacity(this, &SRTSStatusOverlay::GetResultColor).Font(Metrics.ResultTitleFont)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, Metrics.ItemGap, 0.0f, 0.0f)
		[
			SNew(STextBlock).Text(this, &SRTSStatusOverlay::GetResultReason).ColorAndOpacity(PrimaryText).Font(Metrics.ResultStatusFont).AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, Metrics.TightGap, 0.0f, 0.0f)
		[
			SNew(STextBlock).Text(this, &SRTSStatusOverlay::GetResultDuration).ColorAndOpacity(SecondaryText).Font(Metrics.BodyFont)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, Metrics.SectionGap, 0.0f, 0.0f)
		[
			SNew(STextBlock).Text(this, &SRTSStatusOverlay::GetPlayerResultStatistics).ColorAndOpacity(FLinearColor(0.42f, 0.76f, 1.0f)).Font(Metrics.BodyFont).AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, Metrics.TightGap, 0.0f, 0.0f)
		[
			SNew(STextBlock).Text(this, &SRTSStatusOverlay::GetEnemyResultStatistics).ColorAndOpacity(FLinearColor(1.0f, 0.42f, 0.34f)).Font(Metrics.BodyFont).AutoWrapText(true)
		],
		Metrics.ResultPadding,
		FLinearColor(0.015f, 0.035f, 0.06f, 0.98f));

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(Metrics.EdgeInset)
		[
			SNew(SSafeZone).IsTitleSafe(false)[Resources]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(Metrics.EdgeInset)
		[
			SNew(SBox).MinDesiredWidth(280.0f).MaxDesiredWidth(390.0f)[StatusPanel]
		]
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(Metrics.EdgeInset)
		[
			SNew(SSafeZone).IsTitleSafe(false)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.16f)[SNew(SSpacer)]
				+ SHorizontalBox::Slot().FillWidth(0.59f).VAlign(VAlign_Bottom)[SelectionPanel]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(Metrics.ItemGap, 0.0f, 0.0f, 0.0f)
				[
					SNew(SBorder)
					.Visibility(this, &SRTSStatusOverlay::GetActionPanelVisibility)
					.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
					.BorderBackgroundColor(PanelBorder)
					.Padding(1.0f)
					[
						SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(PanelSurface).Padding(Metrics.TightGap)[ActionGrid]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(0.25f)
				[
					SNew(SSpacer).Visibility(this, &SRTSStatusOverlay::GetEmptyActionSpacerVisibility)
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).Visibility(this, &SRTSStatusOverlay::GetResultVisibility)[ResultPanel]
		]
	];
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
	const FRTSEconomySnapshot Economy = ReadSnapshot().Economy;
	return FText::FromString(FString::Printf(
		TEXT("%d / %d"),
		Economy.SupplyUsed + Economy.SupplyReserved,
		Economy.SupplyCapacity));
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
	const FRTSHUDSnapshot Snapshot = ReadSnapshot();
	if (Snapshot.SelectionMode == ERTSHUDSelectionMode::SingleUnit)
	{
		switch (Snapshot.SelectedUnitType)
		{
		case ERTSUnitType::CommandVehicle: return FText::FromString(TEXT("CV"));
		case ERTSUnitType::InfantrySquad: return FText::FromString(TEXT("INF"));
		case ERTSUnitType::LightVehicle: return FText::FromString(TEXT("LGT"));
		case ERTSUnitType::HeavyVehicle: return FText::FromString(TEXT("HVY"));
		default: break;
		}
	}
	if (Snapshot.SelectionMode == ERTSHUDSelectionMode::SingleStructure)
	{
		switch (Snapshot.SelectedStructureType)
		{
		case ERTSStructureType::Headquarters: return FText::FromString(TEXT("HQ"));
		case ERTSStructureType::MaterialExtractor: return FText::FromString(TEXT("EXT"));
		case ERTSStructureType::PowerGenerator: return FText::FromString(TEXT("PWR"));
		case ERTSStructureType::Factory: return FText::FromString(TEXT("FAC"));
		case ERTSStructureType::SupplyDepot: return FText::FromString(TEXT("SUP"));
		case ERTSStructureType::DefensiveTurret: return FText::FromString(TEXT("TUR"));
		default: break;
		}
	}
	if (Snapshot.SelectionMode == ERTSHUDSelectionMode::Multiple)
	{
		return FText::AsNumber(Snapshot.SelectedUnitCount + Snapshot.SelectedStructureCount);
	}
	return FText::FromString(TEXT("--"));
}

FSlateColor SRTSStatusOverlay::GetSelectionAccent() const
{
	return ReadSnapshot().SelectionMode == ERTSHUDSelectionMode::None
		? FLinearColor(0.10f, 0.15f, 0.20f, 1.0f)
		: PlayerBlue;
}

TOptional<float> SRTSStatusOverlay::GetHealthFraction() const
{
	const FRTSHealthSnapshot Health = ReadSnapshot().SelectedHealth;
	return Health.MaximumHealth > 0.0f
		? TOptional<float>(FMath::Clamp(Health.CurrentHealth / Health.MaximumHealth, 0.0f, 1.0f))
		: TOptional<float>();
}

FText SRTSStatusOverlay::GetQueueText() const { return FText::FromString(ReadSnapshot().QueueText); }
FText SRTSStatusOverlay::GetAvailableActionsText() const { return FText::FromString(ReadSnapshot().AvailableActionsText); }

FText SRTSStatusOverlay::GetStatusHeading() const
{
	return FText::FromString(ReadSnapshot().WarningText.IsEmpty() ? TEXT("FIELD STATUS") : TEXT("ALERT"));
}

FText SRTSStatusOverlay::GetStatusPrimaryText() const
{
	const FRTSHUDSnapshot Snapshot = ReadSnapshot();
	if (!Snapshot.WarningText.IsEmpty())
	{
		return FText::FromString(Snapshot.WarningText);
	}
	if (!Snapshot.ContextText.IsEmpty())
	{
		return FText::FromString(Snapshot.ContextText);
	}
	return FText::FromString(TEXT("COMMAND NETWORK ONLINE"));
}

FText SRTSStatusOverlay::GetStatusSecondaryText() const
{
	const FRTSHUDSnapshot Snapshot = ReadSnapshot();
	if (!Snapshot.WarningText.IsEmpty() && !Snapshot.ContextText.IsEmpty())
	{
		return FText::FromString(Snapshot.ContextText);
	}
	if (!Snapshot.WarningText.IsEmpty())
	{
		return FText::FromString(TEXT("ACTION REQUIRED"));
	}
	if (!Snapshot.ContextText.IsEmpty())
	{
		return FText::FromString(TEXT("CURRENT COMMAND FEEDBACK"));
	}
	return FText::FromString(TEXT("NO ACTIVE ALERTS"));
}

FSlateColor SRTSStatusOverlay::GetStatusColor() const
{
	const FRTSHUDSnapshot Snapshot = ReadSnapshot();
	return ToneColor(Snapshot.WarningText.IsEmpty() ? Snapshot.ContextTone : Snapshot.WarningTone);
}

FText SRTSStatusOverlay::GetActionHotkey(const int32 ActionIndex) const
{
	const FRTSHUDSnapshot Snapshot = ReadSnapshot();
	return Snapshot.Actions.IsValidIndex(ActionIndex)
		? FText::FromString(Snapshot.Actions[ActionIndex].Hotkey)
		: FText::GetEmpty();
}

FText SRTSStatusOverlay::GetActionLabel(const int32 ActionIndex) const
{
	const FRTSHUDSnapshot Snapshot = ReadSnapshot();
	return Snapshot.Actions.IsValidIndex(ActionIndex)
		? FText::FromString(Snapshot.Actions[ActionIndex].Label)
		: FText::GetEmpty();
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
	return ReadSnapshot().bPlayerVictory
		? FLinearColor(0.24f, 0.95f, 0.44f)
		: FLinearColor(1.0f, 0.28f, 0.20f);
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
