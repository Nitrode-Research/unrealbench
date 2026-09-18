#include "MatchModifiers.h"

bool FModifierConfiguration::Validate(FString& Reason) const
{
	Reason = TEXT("Match modifiers are not implemented.");
	return false;
}

FString FModifierConfiguration::Canonical() const
{
	return {};
}

FModifierConfiguration FModifierConfiguration::FourRulePreset()
{
	return {};
}

bool FMatchModifierState::Configure(const FModifierConfiguration&, FString& Reason)
{
	Reason = TEXT("Match modifiers are not implemented.");
	return false;
}

void FMatchModifierState::Deactivate(const FString&)
{
}
TArray<FModifierDisplay> FMatchModifierState::Display() const
{
	return {};
}
