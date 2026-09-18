#include "NightSkyModifierWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
TSharedRef<SWidget> UNightSkyModifierWidget::RebuildWidget()
{
	if (!WidgetTree)
		WidgetTree = NewObject<UWidgetTree>(this);
	Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
	                                                TEXT("ActiveMatchRules"));
	auto* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrushColor(FLinearColor(.008,.016,.03,.92));
	Panel->SetPadding(FMargin(12));
	Panel->SetContent(Label);
	auto* Box = WidgetTree->ConstructWidget<USizeBox>();
	Box->SetWidthOverride(400);
	Box->SetContent(Panel);
	WidgetTree->RootWidget = Box;
	auto Font = Label->GetFont(); Font.Size = 15; Label->SetFont(Font);
	Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	return Super::RebuildWidget();
}
void UNightSkyModifierWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	RefreshRules();
}
FText UNightSkyModifierWidget::GetRenderedRuleText() const
{
	return Label ? Label->GetText() : FText::GetEmpty();
}
void UNightSkyModifierWidget::RefreshRules()
{
	const auto* Game = GetWorld() ? GetWorld()->GetGameState<ANightSkyGameState>() : nullptr;
	if (!Game || !Label)
		return;
	DisplayedRound = Game->MatchModifiers.Round;
	DisplayedFrame = Game->MatchModifiers.Frame;
	DisplayedRules = Game->GetModifierRejection();
	for (const auto& D : Game->GetActiveModifiers())
	{
		if (!DisplayedRules.IsEmpty())
			DisplayedRules += TEXT("\n");
		DisplayedRules +=
		    D.Name + TEXT(" | ") +
		    (D.RemainingFrames == -1 ? FString(TEXT("round end"))
		                             : FString::FromInt(D.RemainingFrames) + TEXT(" frames"));
	}
	Label->SetText(FText::FromString(DisplayedRules));
}
