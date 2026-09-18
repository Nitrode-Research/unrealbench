#include "RelayPeerWorker.h"
#include "NightSkyEngine/Network/RpcConnectionManager.h"
#include "NightSkyEngine/Network/NetworkPawn.h"
#include "NightSkyEngine/Battle/FighterRunners/FighterMultiplayerRunner.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/NetDriver.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
namespace
{
FString SemanticSource(const ABattleObject *Object)
{
    if (!Object)
    {
        return TEXT("none");
    }
    const auto Owner = Object->IsPlayer ? Cast<APlayerObject>(Object) : Object->Player;
    if (!Owner)
    {
        return TEXT("expired-projectile");
    }
    return FString::Printf(
        TEXT("%s:%d:%d:%s"), Object->IsPlayer ? TEXT("fighter") : TEXT("projectile"),
        Owner->PlayerIndex, Owner->TeamIndex,
        Object->IsPlayer || !Object->ObjectState ? TEXT("")
                                                 : *Object->ObjectState->Name.ToString());
}
} // namespace
ARelayPeerGameMode::ARelayPeerGameMode()
{
    DefaultPawnClass = ANetworkPawn::StaticClass();
}
void URelayPeerWorker::Initialize(FSubsystemCollectionBase &Collection)
{
    Super::Initialize(Collection);
    Staggered = FParse::Param(FCommandLine::Get(), TEXT("RelayPeerStaggered"));
    Enabled = FParse::Value(FCommandLine::Get(), TEXT("RelayPeerDir="), Directory) &&
              FParse::Value(FCommandLine::Get(), TEXT("RelayPeerRole="), Role);
    if (Enabled)
    {
        IFileManager::Get().MakeDirectory(*Directory, true);
    }
}
void URelayPeerWorker::Observe(ANightSkyGameState *Battle, int32 FirstTeamInput,
                               int32 SecondTeamInput, bool Resimulation)
{
    ++ObservedFrames;
    FString Line = FString::Printf(
        TEXT("%d,%d,%d,%d,%d,%d"), Battle->BattleState.FrameNumber, FirstTeamInput, SecondTeamInput,
        Resimulation, int32(Battle->BattleState.CurrentWinSide), Battle->BattleState.RoundTimer);
    for (int TeamIndex = 0; TeamIndex < 2; ++TeamIndex)
    {
        const auto Status = Battle->GetRelayStatus(TeamIndex == 0);
        const auto Slots = Battle->GetRelaySlots(TeamIndex == 0);
        Line += FString::Printf(TEXT(",%d,%d,%d,%d,%d,%d"), int32(Status.Stage),
                                Status.ElapsedFrames, Status.Resource, 0, 0, 0);
        for (auto Fighter : Battle->GetTeam(TeamIndex == 0))
        {
            Line += FString::Printf(
                TEXT(",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d"), Fighter->TeamIndex,
                Fighter->CurrentHealth, Fighter->RecoverableHealth, Fighter->IsMainPlayer(),
                Fighter->IsOnScreen(),
                Slots.IsValidIndex(Fighter->TeamIndex) ? Slots[Fighter->TeamIndex].Cooldown : 0,
                Fighter->PosX, Fighter->PosY, Fighter->Hitstop, Fighter->ComboCounter,
                Fighter->AttackOwner ? Fighter->AttackOwner->ObjNumber : -1);
        }
    }
    Line += FString::Printf(TEXT(",seed,%u"), Battle->BattleState.RandomManager.GetSeed());
    for (auto Object : Battle->Objects)
    {
        if (Object->IsActive)
        {
            Line +=
                FString::Printf(TEXT(",obj,%d,%d,%d,%d"), Object->ObjNumber,
                                Object->Player ? Object->Player->ObjNumber : -1, Object->PosX, 0);
        }
    }
    const auto Machine = FGameplayTag::RequestGameplayTag(FName("StateMachine.Primary"));
    for (auto Fighter : Battle->Players)
    {
        Line +=
            FString::Printf(TEXT(",fighter,%d,%s,%d,%d,%d,%d,%d,%d,%u,%u,%d"), Fighter->ObjNumber,
                            *Fighter->GetCurrentStateName(Machine).ToString(), Fighter->ActionTime,
                            Fighter->PosY, Fighter->SpeedX, Fighter->SpeedY, Fighter->ComboTimer,
                            Fighter->TotalProration, Fighter->AttackFlags, Fighter->PlayerFlags, 0);
    }
    for (auto Object : Battle->Objects)
    {
        if (Object->IsActive)
        {
            Line += FString::Printf(TEXT(",motion,%d,%d,%d,%d,%d,%d,%u"), Object->ObjNumber,
                                    Object->ActionTime, Object->PosY, Object->SpeedX,
                                    Object->SpeedY, Object->Hitstop, Object->AttackFlags);
        }
    }
    for (int Index = 0; Index < Battle->Players.Num(); ++Index)
    {
        Line += FString::Printf(TEXT(",contact-owner,%d,%s"), Index,
                                *(Battle->Players[Index]->Hitstop > 0 ||
                                          (Battle->Players[Index]->PlayerFlags & PLF_IsThrowLock)
                                      ? SemanticSource(Battle->Players[Index]->AttackOwner)
                                      : FString(TEXT("none"))));
    }
    for (auto Object : Battle->Objects)
    {
        if (Object->IsActive)
        {
            Line += FString::Printf(TEXT(",attack-state,%d,%s"), Object->ObjNumber,
                                    Object->ObjectState ? *Object->ObjectState->Name.ToString()
                                                        : TEXT("none"));
        }
    }
    int32 Locks = 0;
    for (int Index = 0; Index < Battle->Players.Num(); ++Index)
    {
        if (Battle->Players[Index]->PlayerFlags & PLF_IsThrowLock)
        {
            Locks |= 1 << Index;
        }
    }
    Line += FString::Printf(TEXT(",freeze,%d,%d,locks,%d"), Battle->BattleState.SuperFreezeDuration,
                            Battle->BattleState.SuperFreezeSelfDuration, Locks);
    Line += TEXT("\n");
    FFileHelper::SaveStringToFile(Line, *(Directory / (Role + TEXT("-frames.csv"))),
                                  FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
                                  &IFileManager::Get(), FILEWRITE_Append | FILEWRITE_AllowRead);
}
void URelayPeerWorker::Tick(float)
{
    auto World = GetWorld();
    if (!World || !World->HasBegunPlay())
    {
        return;
    }
    auto Battle = World->GetGameState<ANightSkyGameState>();
    if (!Battle || Battle->Players.Num() != 6)
    {
        return;
    }
    if (!Bound)
    {
        int32 Spacing = 600000;
        FParse::Value(FCommandLine::Get(), TEXT("RelayPeerSpacing="), Spacing);
        for (auto P : Battle->Players)
        {
            P->PosX = P->PlayerIndex == 0 ? -Spacing : Spacing;
            auto Fighter = CastChecked<ARelayFixtureFighter>(P);
            Fighter->SampleThrowRange = 1000000;
            if (Staggered)
            {
                Fighter->SampleProjectileHeight = 500000;
            }
        }
        if (Staggered)
        {
            // Authored before the first simulated frame. Ordinary projectile motion and
            // collision cause contact during the staggered relay, without live edits.
            auto Owner = Battle->Players[5];
            auto State = NewObject<URelayFixtureProjectile>(Owner);
            State->Name = FGameplayTag::RequestGameplayTag(TEXT("State.Relay.Projectile"));
            Battle->AddBattleObject(State, Battle->Players[0]->PosX + 910000, 0, DIR_Left, 0, false,
                                    Owner);
        }
        Battle->FrameObservation = [this](ANightSkyGameState *B, int32 A, int32 C, bool R) {
            Observe(B, A, C, R);
        };
        Bound = true;
    }
    auto Runner = Cast<AFighterMultiplayerRunner>(Battle->FighterRunner);
    if (!GateBound && FParse::Param(FCommandLine::Get(), TEXT("RelaySelectedInputDelay")))
    {
        TWeakObjectPtr<ANightSkyGameState> WeakBattle = Battle;
        TWeakObjectPtr<URelayPeerWorker> Self = this;
        Battle->RemoteInputDeliveryGate = [WeakBattle,
                                           Self](const TArray<FRemoteInputChange> &Inputs) {
            if (!WeakBattle.IsValid() || !Self.IsValid())
            {
                return true;
            }
            auto Worker = Self.Get();
            const int Frame = WeakBattle->BattleState.FrameNumber;
            if (Worker->GateRelease >= 0)
            {
                if (Frame < Worker->GateRelease)
                {
                    return false;
                }
                const FString Row = FString::Printf(
                    TEXT("release,%d,%d,%d,%d,%d\n"), Worker->GateWindow, Worker->GateStart, Frame,
                    Frame - Worker->GateInputFrame, Frame - Worker->GateStart);
                FFileHelper::SaveStringToFile(
                    Row, *(Worker->Directory / (Worker->Role + TEXT("-delivery.csv"))),
                    FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(),
                    FILEWRITE_Append | FILEWRITE_AllowRead);
                Worker->GateRelease = -1;
                return true;
            }
            const TArray<int32> Windows = Worker->Staggered
                                              ? TArray<int32>{60, 64, 78, 91}
                                              : TArray<int32>{60, 78, 170, 200, 235, 240, 262, 320};
            const TArray<uint32> Buttons =
                Worker->Staggered ? TArray<uint32>{RelaySlot2, RelaySlot3, RelaySlot3, RelaySlot3}
                                  : TArray<uint32>{RelaySlot2, RelaySlot3, INP_A,      INP_A,
                                                   INP_C,      RelaySlot2, RelaySlot2, INP_B};
            for (const FRemoteInputChange &Input : Inputs)
            {
                const int32 InputFrame = Input.Frame;
                const uint32 Pressed = Input.Pressed;
                for (int Index = 0; Index < Windows.Num(); ++Index)
                {
                    if (Worker->UsedWindows & (1u << Index))
                    {
                        continue;
                    }
                    if ((Pressed & Buttons[Index]) && InputFrame >= Windows[Index] &&
                        InputFrame <= Windows[Index] + 8)
                    {
                        if (Frame > InputFrame + 8)
                        {
                            // No gate can manufacture delivery within eight frames
                            // after the input has already arrived outside that bound.
                            // Keep explicit evidence instead of silently omitting it.
                            const FString Late = FString::Printf(
                                TEXT("late,%d,%d,%d,%d,%u\n"), Windows[Index], Frame,
                                Frame - InputFrame, InputFrame, Pressed);
                            FFileHelper::SaveStringToFile(
                                Late, *(Worker->Directory / (Worker->Role + TEXT("-delivery.csv"))),
                                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
                                &IFileManager::Get(), FILEWRITE_Append | FILEWRITE_AllowRead);
                            continue;
                        }
                        Worker->UsedWindows |= 1u << Index;
                        Worker->GateWindow = Windows[Index];
                        Worker->GateStart = Frame;
                        // Withhold this actual batch now; a later gate invocation
                        // releases it. A packet already seven/eight frames late can
                        // sit at GGPO's prediction limit, so it must not require
                        // another simulated frame before the transport may resume.
                        Worker->GateInputFrame = InputFrame;
                        Worker->GateRelease = FMath::Max(Frame, InputFrame + 5);
                        const FString Row =
                            FString::Printf(TEXT("hold,%d,%d,%d,%d,%u\n"), Windows[Index], Frame,
                                            Worker->GateRelease, InputFrame, Pressed);
                        FFileHelper::SaveStringToFile(
                            Row, *(Worker->Directory / (Worker->Role + TEXT("-delivery.csv"))),
                            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
                            &IFileManager::Get(), FILEWRITE_Append | FILEWRITE_AllowRead);
                        return false;
                    }
                }
            }
            return true;
        };
        GateBound = true;
    }

    if (Role == TEXT("host") && Battle->ConfirmedInputFrame &&
        Battle->ConfirmedInputFrame() >= 420 && !Saved)
    {
        FString Slot;
        if (FParse::Value(FCommandLine::Get(), TEXT("RelaySaveSlot="), Slot))
        {
            Saved = Cast<UNightSkyGameInstance>(GetGameInstance())->SaveCurrentReplayToSlot(Slot);
        }
    }
    if (Role == TEXT("replay") && Battle->BattleState.FrameNumber >= 415)
    {
        FPlatformMisc::RequestExit(false);
        return;
    }
    for (TActorIterator<ANightSkyPlayerController> It(World); It; ++It)
    {
        if (It->IsLocalController())
        {
            const int FrameIndex = Battle->BattleState.FrameNumber;
            int Input = 0;
            if ((FrameIndex >= 60 && FrameIndex <= 63) || (FrameIndex >= 240 && FrameIndex <= 243))
            {
                Input = RelaySlot2;
            }
            if (FrameIndex >= 78 && FrameIndex <= 81)
            {
                Input = RelaySlot3;
            }
            if (FrameIndex >= 262 && FrameIndex <= 265)
            {
                Input = RelaySlot2;
            }
            if (Role == TEXT("client") && FrameIndex >= 170 && FrameIndex < 175)
            {
                Input = INP_A;
            }
            if (Role == TEXT("client") && FrameIndex >= 110 && FrameIndex < 160)
            {
                Input = INP_Left;
            }
            if (Role == TEXT("host") && FrameIndex >= 110 && FrameIndex < 200)
            {
                Input = INP_Right;
            }
            if (Role == TEXT("host") && FrameIndex >= 200 && FrameIndex < 205)
            {
                Input = INP_A;
            }
            if (Role == TEXT("client") && FrameIndex >= 235 && FrameIndex < 240)
            {
                Input = INP_C;
            }
            if (Role == TEXT("host") && FrameIndex >= 320 && FrameIndex < 325)
            {
                Input = INP_B;
            }
            if (Staggered)
            {
                Input = 0;
                if (Role == TEXT("host") && FrameIndex >= 60 && FrameIndex <= 63)
                {
                    Input = RelaySlot2;
                }
                if (Role == TEXT("client") && FrameIndex >= 64 && FrameIndex <= 67)
                {
                    Input = RelaySlot3;
                }
                if (Role == TEXT("host") && FrameIndex >= 78 && FrameIndex <= 81)
                {
                    Input = RelaySlot3;
                }
                if (Role == TEXT("client") && FrameIndex >= 91 && FrameIndex <= 94)
                {
                    Input = RelaySlot3;
                }
                if (FrameIndex >= 120 && FrameIndex < 160)
                {
                    Input = Role == TEXT("host") ? INP_Right : INP_Left;
                }
            }
            if (FrameIndex >= 380 && FrameIndex < 384)
            {
                Input = INP_A | RelaySlot2;
            }
            It->Inputs = Input;
        }
    }
    if (FPlatformTime::Seconds() < Heartbeat)
    {
        return;
    }
    Heartbeat = FPlatformTime::Seconds() + .1;
    auto Driver = World->GetNetDriver();
    FString Status = FString::Printf(
        TEXT("{\"pid\":%u,\"mode\":%d,\"frame\":%d,\"running\":%s,\"confirmed\":%d,\"loads\":%d,"
             "\"resimulated\":%d,\"connections\":%d,\"server_connection\":%s,\"saved\":%s}"),
        FPlatformProcess::GetCurrentProcessId(), int32(World->GetNetMode()),
        Battle->BattleState.FrameNumber, ObservedFrames > 0 ? TEXT("true") : TEXT("false"),
        Battle->ConfirmedInputFrame ? Battle->ConfirmedInputFrame() : -1,
        Runner ? Runner->RollbackLoads : 0, Runner ? Runner->ResimulatedFrames : 0,
        Driver ? Driver->ClientConnections.Num() : 0,
        Driver && Driver->ServerConnection ? TEXT("true") : TEXT("false"),
        Saved ? TEXT("true") : TEXT("false"));
    FFileHelper::SaveStringToFile(Status, *(Directory / (Role + TEXT("-status.json"))),
                                  FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
                                  &IFileManager::Get(), FILEWRITE_AllowRead);
}
