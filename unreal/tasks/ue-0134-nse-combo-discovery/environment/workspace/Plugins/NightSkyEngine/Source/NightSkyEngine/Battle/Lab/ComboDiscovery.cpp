#include "ComboDiscovery.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"

FComboSearchResult UComboDiscovery::DiscoverCombos(ANightSkyGameState* Battle, const FComboSearchRequest& Request)
{
	// Combo discovery is not implemented yet. Nothing is searched and the battle is left untouched.
	return FComboSearchResult();
}

FComboTrialReport UComboDiscovery::ValidateTrial(ANightSkyGameState* Battle, const FComboTrial& Trial, int32 OpponentInput)
{
	// Trial validation is not implemented yet. No trial completes.
	FComboTrialReport Report;
	Report.FailedStep = 0;
	return Report;
}
