#include "NightSkyGameState.h"
#include "Script/State.h"
#include "NightSkyEngine/Data/PrimaryCharaData.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"

bool ANightSkyGameState::IsRelayBattle() const
{
    return BattleState.BattleFormat == EBattleFormat::Relay;
}
void ANightSkyGameState::ConfigureRelayMoves(FGameplayTag Sync, FGameplayTag Followup)
{
    RelaySynchronizedMove = Sync;
    RelayFollowupMove = Followup;
}

ERelayRejection ANightSkyGameState::RelayEligibility(bool IsP1, int32 Slot) const
{
    const int32 TeamIndex = !IsP1;
    const auto Fighters = GetTeam(IsP1);
    const auto &RelayState = BattleState.Relay[TeamIndex];
    if (!IsRelayBattle() || BattleState.BattlePhase != EBattlePhase::Battle || Slot < 1 ||
        Slot > Fighters.Num() || Fighters.Num() != 3 || Slot > 3 ||
        (RelayState.Phase != ERelayPhase::Idle && RelayState.Phase != ERelayPhase::Route) ||
        RelayState.RoutePending)
    {
        return ERelayRejection::WrongPhase;
    }
    const auto Fighter = Fighters[Slot - 1];
    if (Fighter == BattleState.MainPlayer[TeamIndex])
    {
        return ERelayRejection::Main;
    }
    if (Fighter->CurrentHealth <= 0 || Fighter->PlayerFlags & PLF_IsDead)
    {
        return ERelayRejection::Dead;
    }
    const bool Participant =
        RelayState.Phase == ERelayPhase::Route && Slot - 1 == RelayState.Participant;
    if (!Participant && Fighter->IsOnScreen())
    {
        return ERelayRejection::Visible;
    }
    if (!Participant && RelayState.Cooldown[Slot - 1] > 0)
    {
        return ERelayRejection::Cooldown;
    }
    if (RelayState.Phase == ERelayPhase::Idle)
    {
        if (RelayState.Resource < 100)
        {
            return ERelayRejection::Resource;
        }
        auto MainFighter = BattleState.MainPlayer[TeamIndex];
        if (MainFighter->PosY > MainFighter->GroundHeight)
        {
            return ERelayRejection::Airborne;
        }
        if (MainFighter->CheckIsStunned() ||
            (MainFighter->PlayerFlags & (PLF_IsThrowLock | PLF_IsKnockedDown | PLF_RoundWinInputLock)) ||
            BattleState.TimeUntilRoundStart > 0 || MainFighter->AttackFlags & ATK_IsAttacking)
        {
            return ERelayRejection::Busy;
        }
        if (MainFighter->PrimaryStateMachine.CurrentState)
        {
            auto Type = MainFighter->PrimaryStateMachine.CurrentState->StateType;
            if (Type == EStateType::NormalAttack || Type == EStateType::SpecialAttack ||
                Type == EStateType::SuperAttack || Type == EStateType::Hitstun ||
                Type == EStateType::Blockstun)
            {
                return ERelayRejection::Busy;
            }
        }
        if (!MainFighter->PrimaryStateMachine.StateNames.Contains(RelaySynchronizedMove) ||
            !Fighter->PrimaryStateMachine.StateNames.Contains(RelaySynchronizedMove))
        {
            return ERelayRejection::MissingMove;
        }
    }
    else if (!Fighter->PrimaryStateMachine.StateNames.Contains(RelayFollowupMove))
    {
        return ERelayRejection::MissingMove;
    }
    return ERelayRejection::None;
}

TArray<FRelaySlotView> ANightSkyGameState::GetRelaySlots(bool IsP1) const
{
    TArray<FRelaySlotView> SlotViews;
    auto Fighters = GetTeam(IsP1);
    for (int32 SlotIndex = 0; SlotIndex < Fighters.Num(); ++SlotIndex)
    {
        const auto Fighter = Fighters[SlotIndex];
        FRelaySlotView SlotView;
        SlotView.Slot = SlotIndex + 1;
        SlotView.Identity = Fighter->GetClass()->GetName();
        SlotView.Main = Fighter->IsMainPlayer();
        SlotView.Visible = Fighter->IsOnScreen();
        if (GameInstance)
        {
            const auto &Roster = IsP1 ? GameInstance->BattleData.PlayerListP1
                                      : GameInstance->BattleData.PlayerListP2;
            if (Roster.IsValidIndex(SlotIndex) && Roster[SlotIndex].Get())
            {
                SlotView.Identity = Roster[SlotIndex]->CharaName.ToString();
            }
        }
        SlotView.Health = Fighter->CurrentHealth;
        SlotView.Recoverable = Fighter->RecoverableHealth;
        SlotView.Cooldown = SlotIndex < 3 ? BattleState.Relay[!IsP1].Cooldown[SlotIndex] : 0;
        SlotView.Eligible = RelayEligibility(IsP1, SlotIndex + 1) == ERelayRejection::None;
        SlotViews.Add(SlotView);
    }
    return SlotViews;
}

void ANightSkyGameState::CaptureRelayInputs(int32 FirstTeamInput, int32 SecondTeamInput)
{
    if (!IsRelayBattle())
    {
        return;
    }
    const int32 Inputs[2] = {FirstTeamInput, SecondTeamInput};
    for (int32 TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
    {
        auto &RelayState = BattleState.Relay[TeamIndex];
        int32 Edge = (Inputs[TeamIndex] & ~RelayState.PreviousInput) & RelayInputMask;
        RelayState.PreviousInput = Inputs[TeamIndex];
        if (!Edge)
        {
            continue;
        }
        auto& Queue = TeamIndex == 0 ? BattleState.RelayQueuedInputsP1 : BattleState.RelayQueuedInputsP2;
        Queue.Add(Edge);
        RelayState.QueueCount = Queue.Num();
    }
}

void ANightSkyGameState::ResetRelay(int32 FirstTeamInput, int32 SecondTeamInput)
{
    if (!IsRelayBattle())
    {
        return;
    }
    BattleState.RelayQueuedInputsP1.Reset();
    BattleState.RelayQueuedInputsP2.Reset();
    BattleState.RelayGameplayFrame = -1;
    BattleState.CurrentWinSide = WIN_None;
    for (int32 TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
    {
        BattleState.Relay[TeamIndex] = FRelayTeamState();
        BattleState.Relay[TeamIndex].PreviousInput = TeamIndex ? SecondTeamInput : FirstTeamInput;
        auto Fighters = GetTeam(TeamIndex == 0);
        if (Fighters.Num() != 3)
        {
            continue;
        }
        BattleState.MainPlayer[TeamIndex] = Fighters[0];
        for (int32 SlotIndex = 0; SlotIndex < 3; ++SlotIndex)
        {
            auto Fighter = Fighters[SlotIndex];
            Fighter->TeamIndex = SlotIndex;
            Fighter->CurrentHealth = Fighter->MaxHealth;
            Fighter->RecoverableHealth = 0;
            Fighter->Inputs = INP_Neutral;
            Fighter->PlayerFlags &=
                ~(PLF_IsDead | PLF_IsThrowLock | PLF_IsStunned | PLF_RoundWinInputLock);
            Fighter->StoredInputBuffer = FInputBuffer();
            Fighter->SetOnScreen(SlotIndex == 0);
            Fighter->RelaySequence = 0;
            Fighter->JumpToStatePrimary(State_Universal_Stand);
        }
    }
    for (auto BattleObject : Objects)
    {
        BattleObject->ResetObject();
    }
}

bool ANightSkyGameState::RelayControls(const APlayerObject *Fighter) const
{
    return IsRelayBattle() && (BattleState.BattlePhase != EBattlePhase::Battle ||
                               BattleState.Relay[Fighter->PlayerIndex].Phase != ERelayPhase::Idle ||
                               !Fighter->IsMainPlayer());
}

void ANightSkyGameState::BeginRelayFrame()
{
    if (!IsRelayBattle() || BattleState.SuperFreezeDuration ||
        BattleState.SuperFreezeSelfDuration || BattleState.BattlePhase != EBattlePhase::Battle)
    {
        return;
    }
    ++BattleState.RelayGameplayFrame;
    for (int32 TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
    {
        auto &RelayState = BattleState.Relay[TeamIndex];
        auto Fighters = GetTeam(TeamIndex == 0);
        for (int32 SlotIndex = 0; SlotIndex < Fighters.Num() && SlotIndex < 3; ++SlotIndex)
        {
            if (RelayState.Cooldown[SlotIndex] > 0)
            {
                --RelayState.Cooldown[SlotIndex];
            }
            auto Fighter = Fighters[SlotIndex];
            if (Fighter->CurrentHealth <= 0 || Fighter->PlayerFlags & PLF_IsDead)
            {
                Fighter->CurrentHealth = 0;
                Fighter->RecoverableHealth = 0;
            }
            else
            {
                Fighter->CurrentHealth = FMath::Min(Fighter->CurrentHealth, Fighter->MaxHealth);
                Fighter->RecoverableHealth = FMath::Clamp(
                    Fighter->RecoverableHealth, 0, Fighter->MaxHealth - Fighter->CurrentHealth);
                if (!Fighter->IsOnScreen() && Fighter->RecoverableHealth > 0)
                {
                    ++Fighter->CurrentHealth;
                    --Fighter->RecoverableHealth;
                }
            }
            if ((RelayState.Removing & (1 << SlotIndex)) && !Fighter->CheckIsStunned() &&
                !(Fighter->PlayerFlags & PLF_IsThrowLock))
            {
                if (!Fighter->IsMainPlayer())
                {
                    Fighter->SetOnScreen(false);
                }
                RelayState.Removing &= ~(1 << SlotIndex);
            }
        }
    }
}

void ANightSkyGameState::NotifyRelayContact(APlayerObject *Victim)
{
    if (!IsRelayBattle() || !Victim)
    {
        return;
    }
    auto &RelayState = BattleState.Relay[Victim->PlayerIndex];
    if (RelayState.Phase != ERelayPhase::Idle && (RelayState.Enrolled & (1 << Victim->TeamIndex)))
    {
        RelayState.Interrupted = true;
    }
}

void ANightSkyGameState::StartRelayMove(APlayerObject *Fighter, bool Followup)
{
    Fighter->JumpToStatePrimary(Followup ? RelayFollowupMove : RelaySynchronizedMove);
}

void ANightSkyGameState::FinishRelay(int32 TeamIndex, bool Interrupted)
{
    auto &RelayState = BattleState.Relay[TeamIndex];
    const auto Fighters = GetTeam(TeamIndex == 0);
    if (Fighters.Num() != 3)
    {
        return;
    }
    int32 MainSlot = Interrupted || RelayState.Route < 0 ? RelayState.Original : RelayState.Route;
    if (Fighters[MainSlot]->CurrentHealth <= 0)
    {
        MainSlot = -1;
        for (int32 SlotIndex = 0; SlotIndex < 3; ++SlotIndex)
        {
            if (Fighters[SlotIndex]->CurrentHealth > 0)
            {
                MainSlot = SlotIndex;
                break;
            }
        }
    }
    auto PreviousMain = BattleState.MainPlayer[TeamIndex];
    if (MainSlot >= 0)
    {
        auto PromotedMain = Fighters[MainSlot];
        PromotedMain->ComboCounter = PreviousMain->ComboCounter;
        PromotedMain->ComboTimer = PreviousMain->ComboTimer;
        if (!PromotedMain->IsOnScreen())
        {
            PromotedMain->PosX = PreviousMain->PosX;
            PromotedMain->PosY = 0;
            PromotedMain->Direction = PreviousMain->Direction;
        }
        BattleState.MainPlayer[TeamIndex] = PromotedMain;
        PromotedMain->SetOnScreen(true);
    }
    for (int32 SlotIndex = 0; SlotIndex < 3; ++SlotIndex)
    {
        auto Fighter = Fighters[SlotIndex];
        if (RelayState.Enrolled & (1 << SlotIndex))
        {
            RelayState.Cooldown[SlotIndex] = 120;
            Fighter->AttackFlags &= ~ATK_IsAttacking;
            if (!Fighter->CheckIsStunned() && !(Fighter->PlayerFlags & PLF_IsThrowLock))
            {
                Fighter->JumpToStatePrimary(State_Universal_Stand);
            }
            Fighter->Inputs = INP_Neutral;
        }
        if (SlotIndex != MainSlot && Fighter->IsOnScreen())
        {
            if (Fighter->CheckIsStunned() || Fighter->PlayerFlags & PLF_IsThrowLock)
            {
                RelayState.Removing |= 1 << SlotIndex;
            }
            else
            {
                Fighter->SetOnScreen(false);
            }
        }
    }
    if (Interrupted)
    {
        for (auto BattleObject : Objects)
        {
            if (BattleObject->IsActive && BattleObject->Player &&
                BattleObject->Player->PlayerIndex == TeamIndex &&
                BattleObject->RelaySequence == RelayState.Sequence)
            {
                BattleObject->ResetObject();
            }
        }
    }
    RelayState.Phase = ERelayPhase::Idle;
    RelayState.Age = 0;
    RelayState.Interrupted = false;
    RelayState.RoutePending = false;
    RelayState.Enrolled = 0;
    AssignEnemy();
}

void ANightSkyGameState::AdvanceRelayPhase(int32 TeamIndex)
{
    auto &RelayState = BattleState.Relay[TeamIndex];
    const auto Fighters = GetTeam(TeamIndex == 0);
    if (RelayState.Phase != ERelayPhase::Idle)
    {
        ++RelayState.Age;
        if (RelayState.RoutePending)
        {
            RelayState.RoutePending = false;
            RelayState.Phase = ERelayPhase::Followup;
            RelayState.Age = 0;
            StartRelayMove(Fighters[RelayState.Route], true);
        }
        else if (RelayState.Phase == ERelayPhase::Entry && RelayState.Age == 6)
        {
            RelayState.Phase = ERelayPhase::Synchronized;
            RelayState.Age = 0;
            StartRelayMove(Fighters[RelayState.Original], false);
            StartRelayMove(Fighters[RelayState.Participant], false);
        }
        else if (RelayState.Phase == ERelayPhase::Synchronized && RelayState.Age == 12)
        {
            RelayState.Phase = ERelayPhase::Route;
            RelayState.Age = 0;
        }
        else if ((RelayState.Phase == ERelayPhase::Route && RelayState.Age == 10) ||
                 (RelayState.Phase == ERelayPhase::Followup && RelayState.Age == 12))
        {
            RelayState.Phase = ERelayPhase::Exit;
            RelayState.Age = 0;
        }
        else if (RelayState.Phase == ERelayPhase::Exit && RelayState.Age == 6)
        {
            FinishRelay(TeamIndex, false);
        }
    }
}

void ANightSkyGameState::ConsumeRelayCommands(int32 TeamIndex)
{
    auto &RelayState = BattleState.Relay[TeamIndex];
    const auto Fighters = GetTeam(TeamIndex == 0);
    auto& Queue = TeamIndex == 0 ? BattleState.RelayQueuedInputsP1 : BattleState.RelayQueuedInputsP2;
    for (int32 QueuedInputIndex = 0; QueuedInputIndex < Queue.Num(); ++QueuedInputIndex)
    {
        for (int32 SlotIndex = 0; SlotIndex < 3; ++SlotIndex)
        {
            if (!(Queue[QueuedInputIndex] & (RelaySlot1 << SlotIndex)))
            {
                continue;
            }
            RelayState.Rejection = RelayEligibility(TeamIndex == 0, SlotIndex + 1);
            if (RelayState.Rejection != ERelayRejection::None)
            {
                continue;
            }
            if (RelayState.Phase == ERelayPhase::Idle)
            {
                RelayState.Resource -= 100;
                RelayState.Original = BattleState.MainPlayer[TeamIndex]->TeamIndex;
                RelayState.Participant = SlotIndex;
                RelayState.Route = -1;
                RelayState.Enrolled = (1 << SlotIndex) | (1 << RelayState.Original);
                ++RelayState.Sequence;
                RelayState.Phase = ERelayPhase::Entry;
                RelayState.Age = 0;
            }
            else
            {
                RelayState.Route = SlotIndex;
                RelayState.RoutePending = true;
                RelayState.Enrolled |= 1 << SlotIndex;
            }
            auto Fighter = Fighters[SlotIndex];
            if (!Fighter->IsOnScreen())
            {
                Fighter->PosX = BattleState.MainPlayer[TeamIndex]->PosX;
                Fighter->PosY = 0;
                Fighter->Direction = BattleState.MainPlayer[TeamIndex]->Direction;
            }
            Fighter->SetOnScreen(true);
            break;
        }
    }
    RelayState.QueueCount = 0;
    Queue.Reset();
}

void ANightSkyGameState::ResolveRelayFrame()
{
    if (!IsRelayBattle())
    {
        return;
    }
    if (BattleState.BattlePhase != EBattlePhase::Battle)
    {
        BattleState.RelayQueuedInputsP1.Reset();
        BattleState.RelayQueuedInputsP2.Reset();
        for (auto &RelayState : BattleState.Relay)
        {
            RelayState.QueueCount = 0;
        }
        return;
    }
    int32 Health[2] = {};
    bool Cancel[2] = {};
    for (int32 TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
    {
        auto &RelayState = BattleState.Relay[TeamIndex];
        Cancel[TeamIndex] = RelayState.Interrupted;
        for (auto Fighter : GetTeam(TeamIndex == 0))
        {
            // A survivor still exposed from an earlier relay is not a participant
            // in this relay. Its KO only ends this sequence if the match ends.
            const bool CurrentRelayParticipant = RelayState.Phase != ERelayPhase::Idle &&
                (RelayState.Enrolled & (1 << Fighter->TeamIndex));
            if (Fighter->CurrentHealth <= 0)
            {
                Fighter->CurrentHealth = 0;
                Fighter->RecoverableHealth = 0;
                if (CurrentRelayParticipant)
                {
                    Cancel[TeamIndex] = true;
                }
            }
            Health[TeamIndex] += Fighter->CurrentHealth;
            if (CurrentRelayParticipant && Fighter->PlayerFlags & PLF_IsThrowLock)
            {
                Cancel[TeamIndex] = true;
            }
        }
    }
    bool End = Health[0] == 0 || Health[1] == 0 || BattleState.RoundTimer <= 0;
    if (End)
    {
        BattleState.CurrentWinSide = Health[0] == Health[1]  ? WIN_Draw
                                     : Health[0] > Health[1] ? WIN_P1
                                                             : WIN_P2;
        if (Health[0] == 0 && Health[1] > 0)
        {
            BattleState.CurrentWinSide = WIN_P2;
        }
        if (Health[1] == 0 && Health[0] > 0)
        {
            BattleState.CurrentWinSide = WIN_P1;
        }
    }
    for (int32 TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
    {
        auto &RelayState = BattleState.Relay[TeamIndex];
        auto Fighters = GetTeam(TeamIndex == 0);
        if (Fighters.Num() != 3)
        {
            continue;
        }
        if (End || Cancel[TeamIndex])
        {
            if (RelayState.Phase != ERelayPhase::Idle)
            {
                FinishRelay(TeamIndex, true);
            }
            RelayState.QueueCount = 0;
            (TeamIndex == 0 ? BattleState.RelayQueuedInputsP1 : BattleState.RelayQueuedInputsP2).Reset();
            continue;
        }
        if (BattleState.MainPlayer[TeamIndex]->CurrentHealth <= 0)
        {
            RelayState.Original = BattleState.MainPlayer[TeamIndex]->TeamIndex;
            FinishRelay(TeamIndex, true);
        }
        AdvanceRelayPhase(TeamIndex);
        ConsumeRelayCommands(TeamIndex);
    }
    if (End)
    {
        BattleState.BattlePhase = EBattlePhase::EndScreen;
        BattleState.PauseTimer = true;
        for (auto Fighter : Players)
        {
            Fighter->PlayerFlags |= PLF_RoundWinInputLock;
            Fighter->Inputs = INP_Neutral;
            Fighter->AttackFlags &= ~ATK_IsAttacking;
        }
        for (auto BattleObject : Objects)
        {
            if (BattleObject->IsActive)
            {
                BattleObject->ResetObject();
            }
        }
    }
    BattleState.ScreenData.TargetObjects.Empty();
    for (auto Fighter : Players)
    {
        if (Fighter->IsOnScreen())
        {
            BattleState.ScreenData.TargetObjects.Add(Fighter);
        }
    }
}

bool ANightSkyGameState::SetInitialRelayResource(bool IsP1, int32 Amount)
{
    if (!IsRelayBattle() || BattleState.RelayGameplayFrame >= 0 || Amount < 0 || Amount > 200)
    {
        return false;
    }
    BattleState.Relay[!IsP1].Resource = Amount;
    return true;
}

FRelayStatus ANightSkyGameState::GetRelayStatus(bool IsP1) const
{
    const FRelayTeamState &State = BattleState.Relay[!IsP1];
    FRelayStatus Status;
    Status.Stage = State.Phase;
    Status.ElapsedFrames = State.Age;
    Status.Resource = State.Resource;
    Status.Rejection = State.Rejection;
    return Status;
}

void ANightSkyGameState::QueueRelayThrowCheck(APlayerObject* Fighter, int32 PlaybackAge)
{
    if (IsRelayBattle() && BattleState.BattlePhase == EBattlePhase::Battle)
    {
        PendingRelayThrowChecks.Emplace(Fighter, PlaybackAge);
    }
}

void ANightSkyGameState::ResolveRelayThrows()
{
    // Every actor has finished playback and every due hit was considered before
    // a successful throw can clear a target's attack data via ThrowLock.
    auto Checks = MoveTemp(PendingRelayThrowChecks);
    for (const auto& Check : Checks)
    {
        if (IsValid(Check.Key))
        {
            Check.Key->ResolveRelayThrowCollision(Check.Value);
        }
    }
}
