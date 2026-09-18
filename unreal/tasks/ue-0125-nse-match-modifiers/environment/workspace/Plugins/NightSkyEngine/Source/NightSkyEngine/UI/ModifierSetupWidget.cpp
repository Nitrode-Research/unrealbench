#include "ModifierSetupWidget.h"

void UModifierSetupWidget::ConfigureForMatch(const FModifierConfiguration&)
{
}
bool UModifierSetupWidget::SetRuleEnabled(const FString&, bool)
{
	return false;
}
bool UModifierSetupWidget::SetInterval(int32, int32, int32, int32)
{
	return false;
}
TSharedRef<SWidget> UModifierSetupWidget::RebuildWidget()
{
	return Super::RebuildWidget();
}
FText UModifierSetupWidget::GetSetupSummary() const
{
	return FText::GetEmpty();
}
FText UModifierSetupWidget::GetSetupError() const
{
	return FText::GetEmpty();
}
bool UModifierSetupWidget::StartSelectedMatch()
{
	return false;
}
