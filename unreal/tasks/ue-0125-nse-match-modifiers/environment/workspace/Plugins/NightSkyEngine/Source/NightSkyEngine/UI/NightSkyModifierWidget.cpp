#include "NightSkyModifierWidget.h"

TSharedRef<SWidget> UNightSkyModifierWidget::RebuildWidget()
{
	return Super::RebuildWidget();
}

void UNightSkyModifierWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
}

FText UNightSkyModifierWidget::GetRenderedRuleText() const
{
	return FText::GetEmpty();
}

void UNightSkyModifierWidget::RefreshRules()
{
}
