#include "NightSkyGameState.h"
#include "Script/State.h"
bool ANightSkyGameState::IsRelayBattle() const
{
    return BattleState.BattleFormat == EBattleFormat::Relay;
}
void ANightSkyGameState::ConfigureRelayMoves(FGameplayTag Sync, FGameplayTag Followup)
{
    RelaySynchronizedMove = Sync;
    RelayFollowupMove = Followup;
}
TArray<FRelaySlotView> ANightSkyGameState::GetRelaySlots(bool IsP1) const
{
    return {};
}
ERelayRejection ANightSkyGameState::RelayEligibility(bool IsP1, int32 Slot) const
{
    return ERelayRejection::WrongPhase;
}
void ANightSkyGameState::CaptureRelayInputs(int32 FirstTeamInput, int32 SecondTeamInput) {}
void ANightSkyGameState::BeginRelayFrame() {}
void ANightSkyGameState::ResolveRelayFrame() {}
void ANightSkyGameState::ResetRelay(int32 FirstTeamInput, int32 SecondTeamInput) {}
void ANightSkyGameState::NotifyRelayContact(APlayerObject *Fighter) {}
bool ANightSkyGameState::RelayControls(const APlayerObject *Fighter) const
{
    return false;
}
void ANightSkyGameState::FinishRelay(int32 TeamIndex, bool Interrupted) {}
void ANightSkyGameState::StartRelayMove(APlayerObject *Fighter, bool Followup) {}

bool ANightSkyGameState::SetInitialRelayResource(bool IsP1, int32 Amount)
{
    return false;
}

FRelayStatus ANightSkyGameState::GetRelayStatus(bool IsP1) const
{
    return {};
}
