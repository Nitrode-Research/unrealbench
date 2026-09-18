#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NightSkyEngine/Miscellaneous/NightSkyGameInstance.h"
#include "NightSkyEngine/Miscellaneous/ReplayInfo.h"
#include "Kismet/GameplayStatics.h"
#include "NightSkyEngine/Fixtures/ReplayFixture.h"
#include "NightSkyEngine/Battle/NightSkyPlayerController.h"
#include "NightSkyEngine/Data/PrimaryCharaData.h"
#include "NightSkyEngine/Data/PrimaryStageData.h"
#include "NightSkyEngine/Battle/FighterRunners/FighterLocalRunner.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/Package.h"
#include "NightSkyEngine/Battle/Actors/AudioManager.h"
#include "Components/AudioComponent.h"
#include "Components/LineBatchComponent.h"
#include "Camera/CameraActor.h"
#include "RenderingThread.h"
#include "AudioThread.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include <limits>


namespace ReplayTests
{
bool ReportsReplayRejection(const UNightSkyGameInstance* Game)
{
    // The public interface uses the qualified spelling; the shipped reference
    // uses the enum value alone. Both report the documented rejection state.
    return Game->ReplayStatus == TEXT("Rejected") || Game->ReplayStatus == TEXT("ReplayStatus::Rejected");
}
struct FReplayBattleWorld
{
    UReplayFixtureGameInstance* Game = nullptr;
    UWorld* World = nullptr;
    AReplayFixtureBattle* Battle = nullptr;
    ANightSkyPlayerController* Controllers[2] = {};
    explicit FReplayBattleWorld(UReplaySaveInfo* Source, const FString& Identity = FString(),
                                const FString& SaveNamespace = FString())
    {
        Game = NewObject<UReplayFixtureGameInstance>(GEngine);
        Game->InitializeStandalone();
        World = Game->GetWorld();
        Game->BattleVersion = Source ? Source->Version : FString(TEXT("ReplayFixture1"));
        Game->ReplaySlotPrefix = SaveNamespace.IsEmpty()
            ? TEXT("NSE009_") + FGuid::NewGuid().ToString() : SaveNamespace;
        if (Source)
        {
            Game->BeginReplaySession(Source);
        }
        else
        {
            // Resolve the opaque identity using the public ordinary-playback entry point.
            Game->PlayReplayFromBP(Identity);
            if (!Game->GetReplaySource()) { return; }
        }
        FURL URL;
        URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);
        World->InitializeActorsForPlay(URL);
        World->BeginPlay();
        for (int Side = 0; Side != 2; ++Side)
        {
            Controllers[Side] = World->SpawnActor<ANightSkyPlayerController>();
        }
        Battle = World->SpawnActor<AReplayFixtureBattle>();
    }
    ~FReplayBattleWorld()
    {
        if (World)
        {
            World->EndPlay(EEndPlayReason::Quit);
            World->DestroyWorld(false);
        }
        if (World)
        {
            GEngine->DestroyWorldContext(World);
        }
        if (Game)
        {
            Game->Shutdown();
        }
    }
    void Tick(int32 PlayerOne = 0, int32 PlayerTwo = 0)
    {
        Controllers[0]->Inputs = PlayerOne;
        Controllers[1]->Inputs = PlayerTwo;
        Battle->Tick(OneFrame);
    }
};
UReplaySaveInfo* CreateReplayTape(int32 Length)
{
    auto* Source = NewObject<UReplaySaveInfo>();
    Source->Version = TEXT("ReplayFixture1");
    Source->bIsTraining = true;
    Source->BattleData.bIsValid = true;
    Source->BattleData.Stage = GetMutableDefault<UReplayFixtureStageData>();
    Source->BattleData.Random = FRandomManager(9009);
    Source->BattleData.BattleFormat = EBattleFormat::Rounds;
    Source->BattleData.StartRoundTimer = 999;
    auto* Asset = GetMutableDefault<UReplayFixtureCharaData>();
    Source->BattleData.PlayerListP1.Add(Asset);
    Source->BattleData.PlayerListP2.Add(Asset);
    Source->LengthInFrames = Length;
    for (int Frame = 0; Frame < Length; ++Frame)
    {
        Source->InputsP1.Add(Frame % 11 < 5 ? INP_Right : 0);
        Source->InputsP2.Add(Frame % 13 < 6 ? INP_Left : 0);
    }
    return Source;
}
struct FReplaySnapshot
{
    int32 Position, PlayerOnePositionX, PlayerTwoPositionX, PlayerOneHealth, PlayerTwoHealth, Frame,
        PlayerOneHitstop, PlayerTwoHitstop, Timer;
    int32 PlayerOneMeter = 0, PlayerTwoMeter = 0, PlayerOneActionTime = 0, PlayerTwoActionTime = 0;
    FGameplayTag PlayerOneState, PlayerTwoState;
    TArray<FIntVector> Projectiles;
    static FReplaySnapshot Read(const FReplayBattleWorld& Session)
    {
        const auto* PlayerOne = Session.Battle->GetMainPlayer(true);
        const auto* PlayerTwo = Session.Battle->GetMainPlayer(false);
        FReplaySnapshot ObservedSnapshot{Session.Game->GetReplayPosition(),
                                         PlayerOne->PosX,
                                         PlayerTwo->PosX,
                                         PlayerOne->CurrentHealth,
                                         PlayerTwo->CurrentHealth,
                                         Session.Battle->BattleState.FrameNumber,
                                         PlayerOne->Hitstop,
                                         PlayerTwo->Hitstop,
                                         Session.Battle->BattleState.RoundTimer};
        ObservedSnapshot.PlayerOneMeter = Session.Battle->BattleState.Meter[0];
        ObservedSnapshot.PlayerTwoMeter = Session.Battle->BattleState.Meter[1];
        ObservedSnapshot.PlayerOneActionTime = PlayerOne->ActionTime;
        ObservedSnapshot.PlayerTwoActionTime = PlayerTwo->ActionTime;
        ObservedSnapshot.PlayerOneState = Session.Battle->GetMainPlayer(true)->GetCurrentStateName(
            FGameplayTag::RequestGameplayTag(TEXT("StateMachine.Primary")));
        ObservedSnapshot.PlayerTwoState = Session.Battle->GetMainPlayer(false)->GetCurrentStateName(
            FGameplayTag::RequestGameplayTag(TEXT("StateMachine.Primary")));
        for (const auto* Object : Session.Battle->Objects)
        {
            if (Object->IsActive)
            {
                ObservedSnapshot.Projectiles.Add(FIntVector(Object->PosX, Object->PosY, Object->ActionTime));
            }
        }
        ObservedSnapshot.Projectiles.Sort(
            [](const FIntVector& A, const FIntVector& B)
            { return A.X != B.X ? A.X < B.X : (A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z); });
        return ObservedSnapshot;
    }
    bool operator==(const FReplaySnapshot& ObservedSnapshot) const
    {
        return Position == ObservedSnapshot.Position &&
               PlayerOnePositionX == ObservedSnapshot.PlayerOnePositionX &&
               PlayerTwoPositionX == ObservedSnapshot.PlayerTwoPositionX &&
               PlayerOneHealth == ObservedSnapshot.PlayerOneHealth &&
               PlayerTwoHealth == ObservedSnapshot.PlayerTwoHealth && Frame == ObservedSnapshot.Frame &&
               PlayerOneHitstop == ObservedSnapshot.PlayerOneHitstop &&
               PlayerTwoHitstop == ObservedSnapshot.PlayerTwoHitstop && Timer == ObservedSnapshot.Timer &&
               PlayerOneMeter == ObservedSnapshot.PlayerOneMeter &&
               PlayerTwoMeter == ObservedSnapshot.PlayerTwoMeter &&
               PlayerOneActionTime == ObservedSnapshot.PlayerOneActionTime &&
               PlayerTwoActionTime == ObservedSnapshot.PlayerTwoActionTime &&
               PlayerOneState == ObservedSnapshot.PlayerOneState &&
               PlayerTwoState == ObservedSnapshot.PlayerTwoState &&
               Projectiles == ObservedSnapshot.Projectiles;
    }
};
} // namespace ReplayTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayEmptyBranch, "UnrealBench.ReplayTakeover.EmptyBranchPersistence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayEmptyBranch::RunTest(const FString&)
{
    using namespace ReplayTests;
    auto* Source = CreateReplayTape(0);
    Source->BattleData.StartRoundTimer = 137;
    FReplayBattleWorld Session(Source);
    auto* Game = Session.Game;
    for (int Side = 0; Side != 2; ++Side)
    {
        TestTrue(TEXT("load initialized empty tape"), Game->BeginReplaySession(Source));
        TestEqual(TEXT("fresh ownership"), Game->GetReplayOwner(), -1);
        TestFalse(TEXT("invalid owner (2)"), Game->TakeOverReplay(2));
        TestFalse(TEXT("invalid owner (3)"), Game->TakeOverReplay(3));
        TestFalse(TEXT("invalid owner (-1)"), Game->TakeOverReplay(-1));
        TestEqual(TEXT("rejection preserves position"), Game->GetReplayPosition(), 0);
        if (!TestTrue(TEXT("empty takeover accepted"), Game->TakeOverReplay(Side)))
        {
            return false;
        }
        TestFalse(TEXT("second takeover rejected"), Game->TakeOverReplay(1 - Side));
        TestFalse(TEXT("seek after takeover rejected"), Game->SeekReplay(0));
        TestTrue(TEXT("finish empty branch"), Game->FinishReplayBranch());
        auto Slot = Game->GetSavedBranchSlot();
        TestTrue(TEXT("completion reported"), Game->IsReplayComplete());
        Game->FinishReplayBranch(); // Return value on repeated finish is unspecified.
        TestEqual(TEXT("same saved identity"), Game->GetSavedBranchSlot(), Slot);
        FReplayBattleWorld Reopened(nullptr, Slot, Game->ReplaySlotPrefix);
        auto* Saved = Reopened.Game->GetReplaySource();
        if (TestNotNull(TEXT("independent saved replay"), Saved))
        {
            TestEqual(TEXT("empty branch length"), Saved->LengthInFrames, 0);
            TestEqual(TEXT("metadata side"), Saved->TakeoverSide, Side);
            TestEqual(TEXT("metadata frame"), Saved->TakeoverFrame, 0);
            TestEqual(TEXT("initialization saved"), Saved->BattleData.StartRoundTimer, 137);
            TestEqual(TEXT("reopened playback ownership"), Reopened.Game->GetReplayOwner(), -1);
        }
        TestEqual(TEXT("source metadata immutable"), Source->TakeoverSide, -1);
        TestEqual(TEXT("source remains empty"), Source->InputsP1.Num(), 0);
    }
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayBoundary, "UnrealBench.ReplayTakeover.BoundaryRouting",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayBoundary::RunTest(const FString&)
{
    using namespace ReplayTests;
    for (int32 Side : {0, 1})
    {
        for (int32 TakeoverFrame : {0, 1, 11, 12})
        {
            auto* Source = CreateReplayTape(12);
            FReplayBattleWorld Session(Source);
            for (int32 FrameIndex = 0; FrameIndex < TakeoverFrame; ++FrameIndex)
            {
                Session.Tick(INP_Left, INP_Right);
            }
            const auto Before = FReplaySnapshot::Read(Session);
            if (!TestTrue(TEXT("takeover at exact position"), Session.Game->TakeOverReplay(Side)))
            {
                return false;
            }
            TestTrue(TEXT("takeover preserves combat state"), FReplaySnapshot::Read(Session) == Before);
            for (int32 FrameIndex = TakeoverFrame; FrameIndex < 140; ++FrameIndex)
            {
                Session.Tick(INP_Right | INP_Rematch | INP_ResetTraining,
                             INP_Left | INP_Rematch | INP_ResetTraining);
                auto* Branch = Session.Game->GetWorkingBranch();
                if (!TestTrue(TEXT("branch contains simulated pair"),
                              Branch && Branch->InputsP1.IsValidIndex(FrameIndex) &&
                                  Branch->InputsP2.IsValidIndex(FrameIndex)))
                {
                    return false;
                }
                const int32 Expected1 =
                    Side == 0 ? INP_Right : (FrameIndex < 12 ? Source->InputsP1[FrameIndex] : 0);
                const int32 Expected2 =
                    Side == 1 ? INP_Left : (FrameIndex < 12 ? Source->InputsP2[FrameIndex] : 0);
                TestEqual(TEXT("P1 effective pair"), Branch->InputsP1[FrameIndex], Expected1);
                TestEqual(TEXT("P2 effective pair"), Branch->InputsP2[FrameIndex], Expected2);
                if (FrameIndex == TakeoverFrame)
                {
                    TestEqual(TEXT("first live frame has exact fixture displacement"),
                              Side == 0 ? Session.Battle->GetMainPlayer(true)->PosX
                                        : Session.Battle->GetMainPlayer(false)->PosX,
                              Side == 0 ? Before.PlayerOnePositionX + 1000
                                        : Before.PlayerTwoPositionX - 1000);
                }
            }
            auto* Branch = Session.Game->GetWorkingBranch();
            for (int32 FrameIndex = 0; FrameIndex < TakeoverFrame; ++FrameIndex)
            {
                TestEqual(TEXT("P1 exact original prefix"), Branch->InputsP1[FrameIndex],
                          Source->InputsP1[FrameIndex]);
                TestEqual(TEXT("P2 exact original prefix"), Branch->InputsP2[FrameIndex],
                          Source->InputsP2[FrameIndex]);
            }
            TestEqual(TEXT("continues beyond source"), Session.Game->GetReplayPosition(), 140);
            TestTrue(TEXT("live movement occurs"),
                     Side == 0 ? Session.Battle->GetMainPlayer(true)->PosX > Before.PlayerOnePositionX
                               : Session.Battle->GetMainPlayer(false)->PosX < Before.PlayerTwoPositionX);
            TestEqual(TEXT("source length unchanged"), Source->LengthInFrames, 12);
            TestEqual(TEXT("metadata K"), Branch->TakeoverFrame, TakeoverFrame);
            TestEqual(TEXT("metadata owner"), Branch->TakeoverSide, Side);
            TestTrue(TEXT("finish"), Session.Game->FinishReplayBranch());
            const auto End = FReplaySnapshot::Read(Session);
            for (int32 FrameIndex = 0; FrameIndex != 120; ++FrameIndex)
            {
                Session.Tick(INP_A, INP_A);
            }
            TestTrue(TEXT("finish freezes battle"), FReplaySnapshot::Read(Session) == End);
            FReplayBattleWorld FinishedPlayback(nullptr, Session.Game->GetSavedBranchSlot(),
                                                Session.Game->ReplaySlotPrefix);
            auto* Finished = FinishedPlayback.Game->GetReplaySource();
            if (!TestNotNull(TEXT("finished recording reopens"), Finished)) { return false; }
            TestEqual(TEXT("finish freezes recording"), Finished->LengthInFrames, 140);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayLifecycle, "UnrealBench.ReplayTakeover.LifecycleAndRejections",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayLifecycle::RunTest(const FString&)
{
    using namespace ReplayTests;
    for (int32 Length : {0, 1, 20})
    {
        auto* Source = CreateReplayTape(Length);
        FReplayBattleWorld Session(Source);
        Session.Game->PauseReplay(true);
        const auto Initial = FReplaySnapshot::Read(Session);
        for (int32 FrameIndex = 0; FrameIndex != 120; ++FrameIndex)
        {
            Session.Tick();
        }
        TestTrue(TEXT("paused simulation unchanged"), FReplaySnapshot::Read(Session) == Initial);
        for (double Bad : {-1.0, Length + 1.0, 0.5, 0.25, 1.5})
        {
            TestFalse(TEXT("invalid seek"), Session.Game->SeekReplay(Bad));
            TestTrue(TEXT("rejection is atomic"), FReplaySnapshot::Read(Session) == Initial);
        }
        Session.Game->PauseReplay(false);
        for (int32 FrameIndex = 0; FrameIndex != Length + 120; ++FrameIndex)
        {
            Session.Tick();
        }
        TestEqual(TEXT("playback ends in current world at L"), Session.Game->GetReplayPosition(), Length);
        const auto End = FReplaySnapshot::Read(Session);
        TestTrue(TEXT("endpoint takeover"), Session.Game->TakeOverReplay(1));
        Session.Game->PauseReplay(true);
        for (int32 FrameIndex = 0; FrameIndex != 120; ++FrameIndex)
        {
            Session.Tick(INP_Right, INP_Left);
        }
        TestTrue(TEXT("branch pause freezes"), FReplaySnapshot::Read(Session) == End);
        if (!Session.Game->GetWorkingBranch())
        {
            return false;
        }
        TestEqual(TEXT("pause does not record"), Session.Game->GetWorkingBranch()->LengthInFrames, Length);
        TestFalse(TEXT("second takeover"), Session.Game->TakeOverReplay(0));
        TestFalse(TEXT("post-takeover seek"), Session.Game->SeekReplay(0));
        Session.Game->PauseReplay(false);
        Session.Tick(0, INP_Left);
        TestEqual(TEXT("resume advances exactly once"), Session.Game->GetReplayPosition(), Length + 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayCatchup, "UnrealBench.ReplayTakeover.CatchupContinuation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayCatchup::RunTest(const FString&)
{
    using namespace ReplayTests;
    for (int32 TakeoverFrame : {0, 1, 3, 6, 28, 37, 120})
    {
        auto* Source = CreateReplayTape(120);
        Source->InputsP1[0] |= INP_B;
        Source->InputsP1[9] |= INP_A;
        Source->InputsP1[27] |= INP_A;
        Source->InputsP2[25] |= INP_B;
        auto* Modeled = DuplicateObject<UReplaySaveInfo>(Source, GetTransientPackage());
        Modeled->InputsP1.SetNum(TakeoverFrame);
        Modeled->InputsP2.SetNum(TakeoverFrame);
        for (int32 FrameIndex = 0; FrameIndex < 120; ++FrameIndex)
        {
            Modeled->InputsP1.Add((FrameIndex % 2 ? INP_Right : INP_Left) |
                                  (FrameIndex % 31 == 0 ? INP_B : 0));
            Modeled->InputsP2.Add(TakeoverFrame + FrameIndex < Source->LengthInFrames
                                      ? Source->InputsP2[TakeoverFrame + FrameIndex]
                                      : 0);
        }
        Modeled->LengthInFrames = TakeoverFrame + 120;
        FReplayBattleWorld Uninterrupted(Modeled);
        for (int32 FrameIndex = 0; FrameIndex < TakeoverFrame; ++FrameIndex)
        {
            Uninterrupted.Tick();
        }
        FReplayBattleWorld Ordinary(Source);
        for (int FrameIndex = 0; FrameIndex < TakeoverFrame; ++FrameIndex)
        {
            Ordinary.Tick();
        }
        FReplayBattleWorld Seeked(Source);
        if (!TestTrue(TEXT("catch-up accepted"), Seeked.Game->SeekReplay(TakeoverFrame)))
        {
            return false;
        }
        TestTrue(TEXT("same destination"), FReplaySnapshot::Read(Ordinary) == FReplaySnapshot::Read(Seeked));
        const auto Destination = FReplaySnapshot::Read(Ordinary);
        Ordinary.Game->TakeOverReplay(0);
        Seeked.Game->TakeOverReplay(0);
        TestTrue(TEXT("ordinary takeover preserves combat destination"),
                 FReplaySnapshot::Read(Ordinary) == Destination);
        TestTrue(TEXT("caught-up takeover preserves combat destination"),
                 FReplaySnapshot::Read(Seeked) == Destination);
        for (int FrameIndex = 0; FrameIndex < 120; ++FrameIndex)
        {
            const int Input = (FrameIndex % 2 ? INP_Right : INP_Left) | (FrameIndex % 31 == 0 ? INP_B : 0);
            Ordinary.Tick(Input, INP_A);
            Seeked.Tick(Input, INP_A);
            Uninterrupted.Tick();
            TestTrue(TEXT("same continuation"),
                     FReplaySnapshot::Read(Ordinary) == FReplaySnapshot::Read(Seeked));
            TestTrue(TEXT("takeover preserves uninterrupted buffered and seeded outcomes"),
                     FReplaySnapshot::Read(Ordinary) == FReplaySnapshot::Read(Uninterrupted));
        }
    }
    // A valid destination beyond a common catch-up work budget must still run
    // every input pair. Late movement and attacks make truncated simulation observable.
    {
        constexpr int32 Length = 5003, RewindFrame = 4507;
        auto* Source = CreateReplayTape(Length);
        Source->InputsP1.Init(0, Length);
        Source->InputsP2.Init(0, Length);
        Source->InputsP1[4099] = INP_B;
        for (int32 Frame = 4401; Frame < 4414; ++Frame)
        {
            Source->InputsP1[Frame] = INP_Right;
        }
        Source->InputsP2[4501] = INP_A;
        Source->InputsP1[4999] = INP_B;
        FReplayBattleWorld Ordinary(Source), Seeked(Source);
        for (int32 Frame = 0; Frame < RewindFrame; ++Frame)
        {
            Ordinary.Tick();
        }
        const auto RewindDestination = FReplaySnapshot::Read(Ordinary);
        for (int32 Frame = RewindFrame; Frame < Length; ++Frame)
        {
            Ordinary.Tick();
        }
        const auto Destination = FReplaySnapshot::Read(Ordinary);
        if (!TestTrue(TEXT("long forward catch-up accepts valid endpoint"), Seeked.Game->SeekReplay(Length)))
        {
            return false;
        }
        TestTrue(TEXT("long catch-up simulates every pair and late combat"),
                 FReplaySnapshot::Read(Seeked) == Destination);
        if (!TestTrue(TEXT("long backward catch-up accepts valid destination"),
                      Seeked.Game->SeekReplay(RewindFrame)))
        {
            return false;
        }
        TestTrue(TEXT("long backward catch-up reconstructs actual battle"),
                 FReplaySnapshot::Read(Seeked) == RewindDestination);
        if (!TestTrue(TEXT("long catch-up resumes to endpoint"), Seeked.Game->SeekReplay(Length)) ||
            !TestTrue(TEXT("ordinary long replay takeover"), Ordinary.Game->TakeOverReplay(0)) ||
            !TestTrue(TEXT("long caught-up replay takeover"), Seeked.Game->TakeOverReplay(0)))
        {
            return false;
        }
        TestTrue(TEXT("long destination remains equal after seek sequence"),
                 FReplaySnapshot::Read(Seeked) == Destination);
        for (int32 Frame = 0; Frame < 60; ++Frame)
        {
            const int32 Input = INP_Right | (Frame == 40 ? INP_A : 0);
            Ordinary.Tick(Input, 0);
            Seeked.Tick(Input, 0);
            TestTrue(TEXT("long catch-up preserves subsequent gameplay"),
                     FReplaySnapshot::Read(Seeked) == FReplaySnapshot::Read(Ordinary));
        }
    }
    // B is pressed during the final recovery frames, then released before takeover.
    // A later projectile must come from the retained input buffer, not new live input.
    auto* BufferedSource = CreateReplayTape(70);
    BufferedSource->InputsP1.Init(0, 70);
    BufferedSource->InputsP2.Init(0, 70);
    BufferedSource->InputsP1[0] = INP_B;
    BufferedSource->InputsP1[48] = INP_B;
    auto* NoPulseSource = DuplicateObject<UReplaySaveInfo>(BufferedSource, GetTransientPackage());
    NoPulseSource->InputsP1[48] = 0;
    FReplayBattleWorld Buffered(BufferedSource), BufferedSeek(BufferedSource), NoPulse(NoPulseSource);
    for (int32 FrameIndex = 0; FrameIndex < 49; ++FrameIndex)
    {
        Buffered.Tick();
        NoPulse.Tick();
    }
    for (int32 FrameIndex = 0; FrameIndex < 70; ++FrameIndex)
    {
        BufferedSeek.Tick();
    }
    if (!TestTrue(TEXT("backward buffered catch-up accepted"), BufferedSeek.Game->SeekReplay(49)))
    {
        return false;
    }
    TestTrue(TEXT("backward seek reconstructs the buffered destination"),
             FReplaySnapshot::Read(Buffered) == FReplaySnapshot::Read(BufferedSeek));
    TestEqual(TEXT("earlier projectile expired before buffer probe"),
              FReplaySnapshot::Read(Buffered).Projectiles.Num(), 0);
    Buffered.Game->TakeOverReplay(0);
    BufferedSeek.Game->TakeOverReplay(0);
    NoPulse.Game->TakeOverReplay(0);
    for (int32 FrameIndex = 0; FrameIndex < 3; ++FrameIndex)
    {
        Buffered.Tick();
        BufferedSeek.Tick();
        NoPulse.Tick();
    }
    TestTrue(TEXT("released buffered B launches a real later projectile"),
             FReplaySnapshot::Read(Buffered).Projectiles.Num() > 0);
    TestEqual(TEXT("no new live B and no buffered pulse produces no projectile"),
              FReplaySnapshot::Read(NoPulse).Projectiles.Num(), 0);
    TestTrue(TEXT("seek and takeover preserve effective pending command"),
             FReplaySnapshot::Read(Buffered) == FReplaySnapshot::Read(BufferedSeek));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplaySeeded, "UnrealBench.ReplayTakeover.SeededStateful",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplaySeeded::RunTest(const FString&)
{
    using namespace ReplayTests;
    for (int32 Seed : {9009, 19009, 29009, 39019, 79139})
    {
        FRandomStream Random(Seed);
        for (int32 Episode = 0; Episode < 8; ++Episode)
        {
            const int32 Length = Episode % 3 == 0 ? 0 : Random.RandRange(1, 120);
            const int32 TakeoverFrame = Episode % 3 == 1 ? Length : Random.RandRange(0, Length);
            const int32 Side = Random.RandRange(0, 1);
            auto* Source = CreateReplayTape(Length);
            for (int32 Frame = 0; Frame < Length; Frame += 50)
            {
                Source->InputsP1[Frame] |= INP_A;
                if (Frame + 20 < Length)
                {
                    Source->InputsP2[Frame + 20] |= INP_B;
                }
            }
            FReplayBattleWorld Session(Source);
            AddInfo(FString::Printf(TEXT("seed=%d episode=%d L=%d K=%d side=%d"), Seed, Episode, Length,
                                    TakeoverFrame, Side));
            for (int32 Frame = 0; Frame < TakeoverFrame; ++Frame)
            {
                Session.Tick();
            }
            if (!TestTrue(TEXT("model takeover accepted"), Session.Game->TakeOverReplay(Side)))
            {
                return false;
            }
            TArray<int32> Expected1, Expected2;
            for (int32 FrameIndex = 0; FrameIndex < TakeoverFrame; ++FrameIndex)
            {
                Expected1.Add(Source->InputsP1[FrameIndex]);
                Expected2.Add(Source->InputsP2[FrameIndex]);
            }
            FString Actions = FString::Printf(TEXT("seed %d episode %d length %d takeover %d side %d\n"),
                                              Seed, Episode, Length, TakeoverFrame, Side);
            for (int32 FrameIndex = 0; FrameIndex < Length; ++FrameIndex)
            {
                Actions += FString::Printf(TEXT("source %d %d %d\n"), FrameIndex,
                                           Source->InputsP1[FrameIndex], Source->InputsP2[FrameIndex]);
            }
            int32 Next = TakeoverFrame;
            bool Paused = false;
            for (int32 Action = 0; Action < 60; ++Action)
            {
                const int32 Kind = Random.RandRange(0, 9);
                const auto Before = FReplaySnapshot::Read(Session);
                if (Kind == 0)
                {
                    Paused = !Paused;
                    Session.Game->PauseReplay(Paused);
                    Actions += FString::Printf(TEXT("pause %d\n"), Paused);
                }
                else if (Kind == 1)
                {
                    const int32 RequestedSide = Random.RandRange(-2, 3);
                    Actions += FString::Printf(TEXT("takeover %d\n"), RequestedSide);
                    TestFalse(TEXT("model repeated ownership rejection"),
                              Session.Game->TakeOverReplay(RequestedSide));
                    TestTrue(TEXT("rejection preserves state"), FReplaySnapshot::Read(Session) == Before);
                }
                else if (Kind == 2)
                {
                    const int32 RequestedFrame = Random.RandRange(0, Length);
                    Actions += FString::Printf(TEXT("seek %d\n"), RequestedFrame);
                    TestFalse(TEXT("model seek rejection"), Session.Game->SeekReplay(RequestedFrame));
                    TestTrue(TEXT("seek rejection preserves state"),
                             FReplaySnapshot::Read(Session) == Before);
                }
                else
                {
                    const int32 PlayerOne =
                        (Random.RandRange(0, 1) ? INP_Right : INP_Left) | (Action % 20 == 0 ? INP_A : 0);
                    const int32 PlayerTwo =
                        (Random.RandRange(0, 1) ? INP_Right : INP_Left) | (Action % 23 == 0 ? INP_B : 0);
                    Actions += FString::Printf(TEXT("tick %d %d\n"), PlayerOne | INP_ResetTraining,
                                               PlayerTwo | INP_Rematch);
                    Session.Tick(PlayerOne | INP_ResetTraining, PlayerTwo | INP_Rematch);
                    if (!Paused)
                    {
                        Expected1.Add(Side == 0 ? PlayerOne : (Next < Length ? Source->InputsP1[Next] : 0));
                        Expected2.Add(Side == 1 ? PlayerTwo : (Next < Length ? Source->InputsP2[Next] : 0));
                        ++Next;
                    }
                }
                TestEqual(TEXT("model next frame"), Session.Game->GetReplayPosition(), Next);
                TestTrue(TEXT("model P1 tape"), Session.Game->GetWorkingBranch()->InputsP1 == Expected1);
                TestTrue(TEXT("model P2 tape"), Session.Game->GetWorkingBranch()->InputsP2 == Expected2);
            }
            Session.Game->PauseReplay(false);
            Actions += TEXT("pause 0\n");
            bool Moved = false, Hit = false;
            const int32 HUDCount = Session.Battle->PresentedHealth.Num();
            for (int32 Probe = 0; Probe < 60; ++Probe)
            {
                const auto Before = FReplaySnapshot::Read(Session);
                const int32 OwnX = Side == 0 ? Before.PlayerOnePositionX : Before.PlayerTwoPositionX;
                const int32 OtherX = Side == 0 ? Before.PlayerTwoPositionX : Before.PlayerOnePositionX;
                const int32 Live =
                    (OwnX < OtherX ? INP_Right : INP_Left) | INP_A | (Probe % 50 == 0 ? INP_B : 0);
                const int32 PlayerOne = Side == 0 ? Live : 0, PlayerTwo = Side == 1 ? Live : 0;
                Actions += FString::Printf(TEXT("tick %d %d\n"), PlayerOne, PlayerTwo);
                Session.Tick(PlayerOne, PlayerTwo);
                Expected1.Add(Side == 0 ? PlayerOne : (Next < Length ? Source->InputsP1[Next] : 0));
                Expected2.Add(Side == 1 ? PlayerTwo : (Next < Length ? Source->InputsP2[Next] : 0));
                ++Next;
                const auto After = FReplaySnapshot::Read(Session);
                Moved |= Before.PlayerOnePositionX != After.PlayerOnePositionX ||
                         Before.PlayerTwoPositionX != After.PlayerTwoPositionX;
                Hit |= After.PlayerOneHealth < Before.PlayerOneHealth ||
                       After.PlayerTwoHealth < Before.PlayerTwoHealth;
            }
            TestTrue(TEXT("fuzz episode has real displacement"), Moved);
            TestTrue(TEXT("fuzz episode has real collision damage"), Hit);
            TestTrue(TEXT("fuzz episode has real HUD output"),
                     Session.Battle->PresentedHealth.Num() > HUDCount);
            const FString Artifact =
                FPaths::ProjectSavedDir() / FString::Printf(TEXT("NSE009_fuzz_%d_%d.txt"), Seed, Episode);
            for (int32 FrameIndex = 0; FrameIndex < Expected1.Num(); ++FrameIndex)
            {
                Actions += FString::Printf(TEXT("expected %d %d %d\n"), FrameIndex, Expected1[FrameIndex],
                                           Expected2[FrameIndex]);
            }
            TestTrue(TEXT("reproducible script artifact written"),
                     FFileHelper::SaveStringToFile(Actions, *Artifact));
            if (HasAnyErrors())
            {
                return false;
            }
            TestTrue(TEXT("model finish"), Session.Game->FinishReplayBranch());
            const FString Slot = Session.Game->GetSavedBranchSlot();
            Session.Game->FinishReplayBranch(); // Return value on repeated finish is unspecified.
            TestEqual(TEXT("no duplicate save"), Session.Game->GetSavedBranchSlot(), Slot);
            FReplayBattleWorld Reopened(nullptr, Slot, Session.Game->ReplaySlotPrefix);
            auto* Saved = Reopened.Game->GetReplaySource();
            if (!TestNotNull(TEXT("persisted branch"), Saved))
            {
                return false;
            }
            TestTrue(TEXT("saved decoded P1"), Saved->InputsP1 == Expected1);
            TestTrue(TEXT("saved decoded P2"), Saved->InputsP2 == Expected2);
            TestEqual(TEXT("source stays immutable"), Source->LengthInFrames, Length);
            for (int32 Frame = 0; Frame < Next; ++Frame)
            {
                Reopened.Tick(INP_A, INP_A);
            }
            TestTrue(TEXT("independent playback reproduces terminal state"),
                     FReplaySnapshot::Read(Session) == FReplaySnapshot::Read(Reopened));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayPersistence, "UnrealBench.ReplayTakeover.SourceFreeBattlePersistence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayPersistence::RunTest(const FString&)
{
    using namespace ReplayTests;
    for (int32 Length : {0, 24})
    {
        for (int32 Side : {0, 1})
        {
            for (int32 Suffix : {0, 120})
            {
                auto* Source = CreateReplayTape(Length);
                FReplayBattleWorld Session(Source);
                const FString SourceSlot = Session.Game->ReplaySlotPrefix + TEXT("0");
                const FString OtherSlot = Session.Game->ReplaySlotPrefix + TEXT("1");
                TestTrue(TEXT("save source"), UGameplayStatics::SaveGameToSlot(Source, SourceSlot, 0));
                auto* Unrelated = DuplicateObject<UReplaySaveInfo>(Source, GetTransientPackage());
                Unrelated->BattleData.StartRoundTimer = 73;
                TestTrue(TEXT("save unrelated"), UGameplayStatics::SaveGameToSlot(Unrelated, OtherSlot, 0));
                const TArray<FString> ExistingIdentities = Session.Game->GetSavedReplaySlots();
                TArray<uint8> SourceBefore, SourceFileBefore, OtherFileBefore;
                const FString OtherPath =
                    FPaths::ProjectSavedDir() / TEXT("SaveGames") / (OtherSlot + TEXT(".sav"));
                TestTrue(TEXT("unrelated file readable"),
                         FFileHelper::LoadFileToArray(OtherFileBefore, *OtherPath));
                const FString SourcePath =
                    FPaths::ProjectSavedDir() / TEXT("SaveGames") / (SourceSlot + TEXT(".sav"));
                TestTrue(TEXT("source file readable"),
                         FFileHelper::LoadFileToArray(SourceFileBefore, *SourcePath));
                UGameplayStatics::SaveGameToMemory(Source, SourceBefore);
                TArray<FReplaySnapshot> Trace;
                Trace.Add(FReplaySnapshot::Read(Session));
                const int32 TakeoverFrame = Length / 2;
                for (int32 FrameIndex = 0; FrameIndex < TakeoverFrame; ++FrameIndex)
                {
                    Session.Tick();
                    Trace.Add(FReplaySnapshot::Read(Session));
                }
                if (!TestTrue(TEXT("takeover"), Session.Game->TakeOverReplay(Side)))
                {
                    return false;
                }
                for (int32 FrameIndex = 0; FrameIndex < Suffix; ++FrameIndex)
                {
                    Session.Tick(FrameIndex % 4 == 0 ? INP_Right : INP_Left,
                                 FrameIndex % 3 == 0 ? INP_Left : INP_Right);
                    Trace.Add(FReplaySnapshot::Read(Session));
                }
                TestTrue(TEXT("save branch"), Session.Game->FinishReplayBranch());
                TestEqual(TEXT("exactly one replay identity added"),
                          Session.Game->GetSavedReplaySlots().Num(), ExistingIdentities.Num() + 1);
                for (const FString& Identity : ExistingIdentities)
                {
                    TestTrue(TEXT("existing identity remains discoverable"),
                             Session.Game->GetSavedReplaySlots().Contains(Identity));
                }
                const FString BranchSlot = Session.Game->GetSavedBranchSlot();
                TestFalse(TEXT("branch identity is nonempty"), BranchSlot.IsEmpty());
                TestTrue(TEXT("branch identity differs from source and unrelated saves"),
                         BranchSlot != SourceSlot && BranchSlot != OtherSlot);
                TestTrue(TEXT("returned branch identity is discoverable"),
                         Session.Game->GetSavedReplaySlots().Contains(BranchSlot));
                TArray<uint8> SourceAfter;
                UGameplayStatics::SaveGameToMemory(Source, SourceAfter);
                TestTrue(TEXT("source memory unchanged"), SourceBefore == SourceAfter);
                TArray<uint8> SourceFileAfter;
                TestTrue(TEXT("source file remains readable"),
                         FFileHelper::LoadFileToArray(SourceFileAfter, *SourcePath));
                TestTrue(TEXT("source file bytes immutable"), SourceFileBefore == SourceFileAfter);
                auto* OriginalOnDisk =
                    Cast<UReplaySaveInfo>(UGameplayStatics::LoadGameFromSlot(SourceSlot, 0));
                if (!TestNotNull(TEXT("source remains loadable"), OriginalOnDisk))
                {
                    return false;
                }
                TArray<uint8> DiskDecoded;
                UGameplayStatics::SaveGameToMemory(OriginalOnDisk, DiskDecoded);
                TestTrue(TEXT("source storage unchanged"), SourceBefore == DiskDecoded);
                auto* OtherOnDisk = Cast<UReplaySaveInfo>(UGameplayStatics::LoadGameFromSlot(OtherSlot, 0));
                if (!TestNotNull(TEXT("unrelated save remains loadable"), OtherOnDisk))
                {
                    return false;
                }
                TestEqual(TEXT("unrelated initialization unchanged"), OtherOnDisk->BattleData.StartRoundTimer,
                          73);
                TArray<uint8> OtherFileAfter;
                TestTrue(TEXT("unrelated file remains readable"),
                         FFileHelper::LoadFileToArray(OtherFileAfter, *OtherPath));
                TestTrue(TEXT("unrelated file bytes immutable"), OtherFileBefore == OtherFileAfter);
                TestTrue(TEXT("hide source before reopening"),
                         UGameplayStatics::DeleteGameInSlot(SourceSlot, 0));
                FReplayBattleWorld Reopened(nullptr, BranchSlot, Session.Game->ReplaySlotPrefix);
                auto* Saved = Reopened.Game->GetReplaySource();
                if (!TestNotNull(TEXT("returned branch identity reopens"), Saved))
                {
                    return false;
                }
                TestEqual(TEXT("saved compatible version"), Saved->Version, Source->Version);
                TestEqual(TEXT("saved training initialization"), Saved->bIsTraining, Source->bIsTraining);
                TestTrue(TEXT("saved original character assets"),
                         Saved->BattleData.PlayerListP1 == Source->BattleData.PlayerListP1 &&
                         Saved->BattleData.PlayerListP2 == Source->BattleData.PlayerListP2);
                TestTrue(TEXT("saved original stage asset"), Saved->BattleData.Stage == Source->BattleData.Stage);
                TestEqual(TEXT("saved original round timer"), Saved->BattleData.StartRoundTimer,
                          Source->BattleData.StartRoundTimer);
                TestEqual(TEXT("saved original round count"), Saved->BattleData.RoundCount,
                          Source->BattleData.RoundCount);
                TestEqual(TEXT("saved requested position"), Saved->TakeoverFrame, TakeoverFrame);
                TestEqual(TEXT("saved selected side"), Saved->TakeoverSide, Side);
                TestEqual(TEXT("saved immediate or continued length"), Saved->LengthInFrames,
                          TakeoverFrame + Suffix);
                {
                    TestTrue(TEXT("independent initialization"), FReplaySnapshot::Read(Reopened) == Trace[0]);
                    for (int32 Frame = 1; Frame < Trace.Num(); ++Frame)
                    {
                        Reopened.Tick(INP_A, INP_A);
                        TestTrue(TEXT("independent complete battle trace"),
                                 FReplaySnapshot::Read(Reopened) == Trace[Frame]);
                    }
                }
                TArray<FString> BeforeRepeat = Session.Game->GetSavedReplaySlots();
                BeforeRepeat.Sort();
                Session.Game->FinishReplayBranch(); // Return value on repeated finish is unspecified.
                TestEqual(TEXT("repeat retains the same branch identity"), Session.Game->GetSavedBranchSlot(),
                          BranchSlot);
                TArray<FString> AfterRepeat = Session.Game->GetSavedReplaySlots();
                AfterRepeat.Sort();
                TestTrue(TEXT("repeat creates no duplicate"), BeforeRepeat == AfterRepeat);
                UGameplayStatics::DeleteGameInSlot(OtherSlot, 0);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayCombat, "UnrealBench.ReplayTakeover.CombatBoundary",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayCombat::RunTest(const FString&)
{
    using namespace ReplayTests;
    for (int32 Side : {0, 1})
    {
        auto* Source = CreateReplayTape(0);
        FReplayBattleWorld Session(Source);
        auto* Attacker = Session.Battle->GetMainPlayer(Side == 0);
        auto* Defender = Session.Battle->GetMainPlayer(Side != 0);
        const int32 HP = Defender->CurrentHealth;
        if (!TestTrue(TEXT("empty combat takeover"), Session.Game->TakeOverReplay(Side)))
        {
            return false;
        }
        bool Hit = false;
        for (int32 FrameIndex = 0; FrameIndex < 20; ++FrameIndex)
        {
            Session.Tick(Side == 0 ? INP_A : INP_B, Side == 1 ? INP_A : INP_B);
            if (Defender->CurrentHealth < HP)
            {
                TestEqual(TEXT("fixture exact first-hit damage"), HP - Defender->CurrentHealth, 500);
                TestTrue(TEXT("real hitstop"), Attacker->Hitstop > 0 && Defender->Hitstop > 0);
                Hit = true;
                break;
            }
        }
        TestTrue(TEXT("non-vacuous combat hit"), Hit);
        TestEqual(TEXT("unselected combat ignored"), Attacker->CurrentHealth, Attacker->MaxHealth);
    }
    return true;
}


namespace ReplayTests
{
TArray<FColor> RenderBattle(FReplayBattleWorld& Session)
{
    auto* Capture = Session.World->SpawnActor<ASceneCapture2D>();
    auto* Component = Capture->GetCaptureComponent2D();
    auto* Target = NewObject<UTextureRenderTarget2D>(Capture);
    Target->ClearColor = FLinearColor::Black;
    Target->InitAutoFormat(128, 128);
    Component->TextureTarget = Target;
    Component->bCaptureEveryFrame = false;
    Component->bCaptureOnMovement = false;
    Component->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    Component->FOVAngle = 54;
    Capture->SetActorTransform(Session.Battle->CameraActor->GetActorTransform());
    Component->CaptureScene();
    FlushRenderingCommands();
    TArray<FColor> Pixels;
    Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
    Capture->Destroy();
    return Pixels;
}
int32 LitPixelCount(const TArray<FColor>& Pixels)
{
    int32 Count = 0;
    for (FColor Pixel : Pixels)
    {
        if (Pixel.R > 12 || Pixel.G > 12 || Pixel.B > 12)
        {
            ++Count;
        }
    }
    return Count;
}
} // namespace ReplayTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayHeadlessPresentation, "UnrealBench.ReplayTakeover.HeadlessPresentationFence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayHeadlessPresentation::RunTest(const FString&)
{
    using namespace ReplayTests;
    // These observations work without an RHI or sound device. Pixels and real
    // audio playback retain their positive controls in the integration test.
    for (bool Rewind : {false, true})
    {
        AddInfo(Rewind ? TEXT("backward headless presentation catch-up")
                       : TEXT("forward headless presentation catch-up"));
        auto* Source = CreateReplayTape(30);
        Source->InputsP1[0] |= INP_A | INP_B;
        FReplayBattleWorld Ordinary(Source), Seeked(Source);
        const auto BatchType = UWorld::ELineBatcherType::World;
        auto* OrdinaryLines = Ordinary.World->GetLineBatcher(BatchType);
        auto* SeekedLines = Seeked.World->GetLineBatcher(BatchType);
        if (!TestNotNull(TEXT("ordinary collision line batcher available"), OrdinaryLines) ||
            !TestNotNull(TEXT("seek collision line batcher available"), SeekedLines))
        {
            return false;
        }
        Ordinary.Battle->bViewCollision = true;
        const FVector InitialFighter = Ordinary.Battle->GetMainPlayer(true)->GetActorLocation();
        const FVector InitialCamera = Ordinary.Battle->CameraActor->GetActorLocation();
        for (int32 Frame = 0; Frame < 10; ++Frame)
        {
            OrdinaryLines->Flush();
            Ordinary.Tick();
        }
        TestTrue(TEXT("ordinary HUD output positive control"), Ordinary.Battle->PresentedHealth.Num() > 1);
        TestTrue(TEXT("ordinary fighter transform positive control"),
                 Ordinary.Battle->GetMainPlayer(true)->GetActorLocation() != InitialFighter);
        TestTrue(TEXT("ordinary camera transform positive control"),
                 Ordinary.Battle->CameraActor->GetActorLocation() != InitialCamera);
        const int32 ExpectedLines = OrdinaryLines->BatchedLines.Num();
        TestTrue(TEXT("ordinary collision output positive control"), ExpectedLines > 0);
        if (Rewind)
        {
            for (int32 Frame = 0; Frame < 30; ++Frame)
            {
                Seeked.Tick();
            }
        }
        SeekedLines->Flush();
        Seeked.Battle->bViewCollision = true;
        Seeked.Battle->PresentedHealth.Reset();
        // USceneComponent::TransformUpdated needs no RHI, so intermediate visual
        // transforms during the synchronous seek are observable headlessly. The
        // rendered lane keeps the pixel positive controls.
        TArray<FVector> FighterLocations, OtherFighterLocations, CameraLocations;
        TWeakObjectPtr<USceneComponent> Root = Seeked.Battle->GetMainPlayer(true)->GetRootComponent();
        TWeakObjectPtr<USceneComponent> OtherRoot = Seeked.Battle->GetMainPlayer(false)->GetRootComponent();
        TWeakObjectPtr<USceneComponent> CameraRoot = Seeked.Battle->CameraActor->GetRootComponent();
        FVector LastFighterLocation = Root->GetComponentLocation();
        FVector LastOtherLocation = OtherRoot->GetComponentLocation();
        FVector LastCameraLocation = CameraRoot->GetComponentLocation();
        // TransformUpdated also fires for scale/rotation changes. The ordinary
        // fixture sets those before location, so unchanged translations do not
        // constitute an intermediate location presentation.
        const auto RecordLocationChange = [](USceneComponent* Component, FVector& Last,
                                             TArray<FVector>& Changes)
        {
            const FVector Location = Component->GetComponentLocation();
            if (Location != Last)
            {
                Changes.Add(Location);
                Last = Location;
            }
        };
        const auto TransformHandle = Root->TransformUpdated.AddLambda(
            [&](USceneComponent* Component, EUpdateTransformFlags, ETeleportType)
            { RecordLocationChange(Component, LastFighterLocation, FighterLocations); });
        const auto OtherTransformHandle = OtherRoot->TransformUpdated.AddLambda(
            [&](USceneComponent* Component, EUpdateTransformFlags, ETeleportType)
            { RecordLocationChange(Component, LastOtherLocation, OtherFighterLocations); });
        const auto CameraTransformHandle = CameraRoot->TransformUpdated.AddLambda(
            [&](USceneComponent* Component, EUpdateTransformFlags, ETeleportType)
            { RecordLocationChange(Component, LastCameraLocation, CameraLocations); });
        const bool bSeekAccepted = Seeked.Game->SeekReplay(10);
        // Seeking may replace actors. These listeners observe the original
        // components only during the synchronous seek; the destination checks
        // below read the current actors.
        if (Root.IsValid()) { Root->TransformUpdated.Remove(TransformHandle); }
        if (OtherRoot.IsValid()) { OtherRoot->TransformUpdated.Remove(OtherTransformHandle); }
        if (CameraRoot.IsValid()) { CameraRoot->TransformUpdated.Remove(CameraTransformHandle); }
        if (!TestTrue(TEXT("headless seek accepted"), bSeekAccepted))
        {
            return false;
        }
        for (auto Location : FighterLocations)
        {
            TestEqual(TEXT("no intermediate P1 visual transform"), Location,
                      Ordinary.Battle->GetMainPlayer(true)->GetActorLocation());
        }
        for (auto Location : OtherFighterLocations)
        {
            TestEqual(TEXT("no intermediate P2 visual transform"), Location,
                      Ordinary.Battle->GetMainPlayer(false)->GetActorLocation());
        }
        for (auto Location : CameraLocations)
        {
            TestEqual(TEXT("no intermediate camera transform"), Location,
                      Ordinary.Battle->CameraActor->GetActorLocation());
        }
        TestTrue(TEXT("headless destination gameplay matches ordinary playback"),
                 FReplaySnapshot::Read(Seeked) == FReplaySnapshot::Read(Ordinary));
        TestEqual(TEXT("headless seek emits only destination HUD output"),
                  Seeked.Battle->PresentedHealth.Num(), 1);
        TestEqual(TEXT("headless seek emits only destination collision output"),
                  SeekedLines->BatchedLines.Num(), ExpectedLines);
        for (int32 Side = 0; Side < 2; ++Side)
        {
            const FVector Destination = Ordinary.Battle->GetMainPlayer(Side == 0)->GetActorLocation();
            TestEqual(TEXT("destination fighter transform is restored"),
                      Seeked.Battle->GetMainPlayer(Side == 0)->GetActorLocation(), Destination);
        }
        const FVector DestinationCamera = Ordinary.Battle->CameraActor->GetActorLocation();
        TestEqual(TEXT("destination camera is restored"),
                  Seeked.Battle->CameraActor->GetActorLocation(), DestinationCamera);
        if (!TestTrue(TEXT("takeover after headless seek"), Seeked.Game->TakeOverReplay(0)))
        {
            return false;
        }
        // Seeking may replace actors. Observe the current public presentation
        // after every live tick, without requiring stable actor/component identity.
        FVector PreviousFighter = Seeked.Battle->GetMainPlayer(true)->GetActorLocation();
        FVector PreviousCamera = Seeked.Battle->CameraActor->GetActorLocation();
        bool FighterMoved = false, CameraMoved = false;
        for (int32 Frame = 0; Frame < 30; ++Frame)
        {
            SeekedLines->Flush();
            Seeked.Tick(INP_Left, 0);
            const FVector Fighter = Seeked.Battle->GetMainPlayer(true)->GetActorLocation();
            const FVector Camera = Seeked.Battle->CameraActor->GetActorLocation();
            FighterMoved |= !Fighter.Equals(PreviousFighter);
            CameraMoved |= !Camera.Equals(PreviousCamera);
            PreviousFighter = Fighter;
            PreviousCamera = Camera;
        }
        TestTrue(TEXT("HUD presentation resumes after seek"), Seeked.Battle->PresentedHealth.Num() > 1);
        TestTrue(TEXT("fighter presentation resumes after seek"), FighterMoved);
        TestTrue(TEXT("camera presentation resumes after seek"), CameraMoved);
        TestTrue(TEXT("collision presentation resumes after seek"), SeekedLines->BatchedLines.Num() > 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayPresentation, "UnrealBenchIntegration.ReplayTakeover.PresentationFence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayPresentation::RunTest(const FString&)
{
    using namespace ReplayTests;
    for (bool Rewind : {false, true})
    {
        AddInfo(Rewind ? TEXT("backward presentation catch-up") : TEXT("forward presentation catch-up"));
        auto* Source = CreateReplayTape(30);
        Source->InputsP1[0] |= INP_A | INP_B;
        FReplayBattleWorld Ordinary(Source);
        Ordinary.Battle->bViewCollision = true;
        int32 OrdinarySounds = 0;
        TArray<FDelegateHandle> Handles;
        for (auto* Component : Ordinary.Battle->AudioManager->CommonAudioPlayers)
        {
            Handles.Add(Component->OnAudioPlayStateChangedNative.AddLambda(
                [&](const UAudioComponent*, EAudioComponentPlayState State)
                {
                    if (State == EAudioComponentPlayState::Playing)
                    {
                        ++OrdinarySounds;
                    }
                }));
        }
        const auto BatchType = UWorld::ELineBatcherType::World;
        for (int32 FrameIndex = 0; FrameIndex < 10; ++FrameIndex)
        {
            Ordinary.World->GetLineBatcher(BatchType)->Flush();
            Ordinary.Tick();
        }
        FAudioCommandFence AudioFence;
        AudioFence.BeginFence();
        AudioFence.Wait();
        FlushRenderingCommands();
        TestTrue(TEXT("ordinary real sound playback positive control"), OrdinarySounds > 0);
        TestTrue(TEXT("ordinary HUD event positive control"), Ordinary.Battle->PresentedHealth.Num() > 0);
        const int32 ExpectedLines = Ordinary.World->GetLineBatcher(BatchType)->BatchedLines.Num();
        TestTrue(TEXT("ordinary collision debug draw positive control"), ExpectedLines > 0);
        FReplayBattleWorld Seeked(Source);
        if (Rewind)
        {
            for (int32 FrameIndex = 0; FrameIndex < 30; ++FrameIndex)
            {
                Seeked.Tick();
            }
        }
        AudioFence.BeginFence();
        AudioFence.Wait();
        FlushRenderingCommands();
        Seeked.World->GetLineBatcher(BatchType)->Flush();
        Seeked.Battle->bViewCollision = true;
        Seeked.Battle->PresentedHealth.Reset();
        int32 SkippedSounds = 0;
        TArray<FDelegateHandle> SeekHandles;
        for (auto* Component : Seeked.Battle->AudioManager->CommonAudioPlayers)
        {
            SeekHandles.Add(Component->OnAudioPlayStateChangedNative.AddLambda(
                [&](const UAudioComponent*, EAudioComponentPlayState State)
                {
                    if (State == EAudioComponentPlayState::Playing)
                    {
                        ++SkippedSounds;
                    }
                }));
        }
        TArray<FVector> FighterLocations, OtherFighterLocations, CameraLocations;
        TWeakObjectPtr<USceneComponent> Root = Seeked.Battle->GetMainPlayer(true)->GetRootComponent();
        TWeakObjectPtr<USceneComponent> OtherRoot = Seeked.Battle->GetMainPlayer(false)->GetRootComponent();
        TWeakObjectPtr<USceneComponent> CameraRoot = Seeked.Battle->CameraActor->GetRootComponent();
        FVector LastFighterLocation = Root->GetComponentLocation();
        FVector LastOtherLocation = OtherRoot->GetComponentLocation();
        FVector LastCameraLocation = CameraRoot->GetComponentLocation();
        // TransformUpdated also fires for scale/rotation changes. The ordinary
        // fixture sets those before location, so unchanged translations do not
        // constitute an intermediate location presentation.
        const auto RecordLocationChange = [](USceneComponent* Component, FVector& Last,
                                             TArray<FVector>& Changes)
        {
            const FVector Location = Component->GetComponentLocation();
            if (Location != Last)
            {
                Changes.Add(Location);
                Last = Location;
            }
        };
        const auto OtherTransformHandle = OtherRoot->TransformUpdated.AddLambda(
            [&](USceneComponent* Component, EUpdateTransformFlags, ETeleportType)
            { RecordLocationChange(Component, LastOtherLocation, OtherFighterLocations); });
        const auto CameraTransformHandle = CameraRoot->TransformUpdated.AddLambda(
            [&](USceneComponent* Component, EUpdateTransformFlags, ETeleportType)
            { RecordLocationChange(Component, LastCameraLocation, CameraLocations); });
        const auto TransformHandle = Root->TransformUpdated.AddLambda(
            [&](USceneComponent* Component, EUpdateTransformFlags, ETeleportType)
            { RecordLocationChange(Component, LastFighterLocation, FighterLocations); });
        TestTrue(TEXT("seek completes synchronously"), Seeked.Game->SeekReplay(10));
        AudioFence.BeginFence();
        AudioFence.Wait();
        FlushRenderingCommands();
        // Seeking may replace actors. These listeners observe the original components
        // only during synchronous seek; destination checks below use the current actors.
        if (Root.IsValid()) { Root->TransformUpdated.Remove(TransformHandle); }
        if (OtherRoot.IsValid()) { OtherRoot->TransformUpdated.Remove(OtherTransformHandle); }
        if (CameraRoot.IsValid()) { CameraRoot->TransformUpdated.Remove(CameraTransformHandle); }
        TestEqual(TEXT("no skipped audio playback"), SkippedSounds, 0);
        TestEqual(TEXT("only destination HUD output"), Seeked.Battle->PresentedHealth.Num(), 1);
        TestEqual(TEXT("only destination collision debug output"),
                  Seeked.World->GetLineBatcher(BatchType)->BatchedLines.Num(), ExpectedLines);
        for (auto Location : FighterLocations)
        {
            TestEqual(TEXT("no intermediate visual transform"), Location,
                      Ordinary.Battle->GetMainPlayer(true)->GetActorLocation());
        }
        for (auto Location : OtherFighterLocations)
        {
            TestEqual(TEXT("no intermediate P2 visual transform"), Location,
                      Ordinary.Battle->GetMainPlayer(false)->GetActorLocation());
        }
        for (auto Location : CameraLocations)
        {
            TestEqual(TEXT("no intermediate camera transform"), Location,
                      Ordinary.Battle->CameraActor->GetActorLocation());
        }
        TestTrue(TEXT("destination P1 visual transform"),
                 Seeked.Battle->GetMainPlayer(true)->GetActorTransform().Equals(
                     Ordinary.Battle->GetMainPlayer(true)->GetActorTransform()));
        TestTrue(TEXT("destination P2 visual transform"),
                 Seeked.Battle->GetMainPlayer(false)->GetActorTransform().Equals(
                     Ordinary.Battle->GetMainPlayer(false)->GetActorTransform()));
        TestEqual(TEXT("destination camera"), Seeked.Battle->CameraActor->GetActorLocation(),
                  Ordinary.Battle->CameraActor->GetActorLocation());
        TestTrue(TEXT("destination combat state"),
                 FReplaySnapshot::Read(Seeked) == FReplaySnapshot::Read(Ordinary));
        const auto OrdinaryPixels = RenderBattle(Ordinary);
        const auto SeekedPixels = RenderBattle(Seeked);
        TestEqual(TEXT("ordinary render target readback"), OrdinaryPixels.Num(), 128 * 128);
        TestEqual(TEXT("catch-up render target readback"), SeekedPixels.Num(), 128 * 128);
        if (OrdinaryPixels.Num() == 128 * 128 && SeekedPixels.Num() == 128 * 128)
        {
            TestTrue(TEXT("ordinary render artifact"),
                     FFileHelper::CreateBitmap(
                         *(FPaths::ProjectSavedDir() /
                           (Rewind ? TEXT("NSE009_ordinary_rewind.bmp") : TEXT("NSE009_ordinary.bmp"))),
                         128, 128, OrdinaryPixels.GetData()));
            TestTrue(TEXT("destination render artifact"),
                     FFileHelper::CreateBitmap(
                         *(FPaths::ProjectSavedDir() /
                           (Rewind ? TEXT("NSE009_destination_rewind.bmp") : TEXT("NSE009_destination.bmp"))),
                         128, 128, SeekedPixels.GetData()));
        }
        const int32 OrdinaryLit = LitPixelCount(OrdinaryPixels);
        const int32 SeekedLit = LitPixelCount(SeekedPixels);
        TestTrue(TEXT("ordinary rendered fighters positive control"),
                 OrdinaryLit > 8 && OrdinaryLit < 128 * 128);
        TestTrue(TEXT("destination rendered fighters restored"), SeekedLit > 8 && SeekedLit < 128 * 128);
        TestTrue(TEXT("destination presentation area matches semantically"),
                 FMath::Abs(OrdinaryLit - SeekedLit) <= FMath::Max(8, OrdinaryLit / 10));
        Seeked.Tick();
        Ordinary.Tick();
        AudioFence.BeginFence();
        AudioFence.Wait();
        TestEqual(TEXT("no deferred skipped audio on resumed playback"), SkippedSounds, 0);
        TestEqual(TEXT("exactly one resumed HUD update"), Seeked.Battle->PresentedHealth.Num(), 2);
        TestTrue(TEXT("ordinary playback resumes after catch-up"),
                 FReplaySnapshot::Read(Seeked) == FReplaySnapshot::Read(Ordinary));
        Seeked.Game->TakeOverReplay(0);
        for (int32 FrameIndex = 0; FrameIndex < 40; ++FrameIndex)
        {
            Seeked.Tick();
        }
        AudioFence.BeginFence();
        AudioFence.Wait();
        FlushRenderingCommands();
        TestEqual(TEXT("quiet continuation does not replay skipped audio"), SkippedSounds, 0);
        // Earlier leaked events must not satisfy the new-attack positive control.
        const int32 SoundsBeforeNewAttack = SkippedSounds;
        for (int32 FrameIndex = 40; FrameIndex < 120; ++FrameIndex)
        {
            Seeked.Tick(FrameIndex == 40 ? INP_A : 0);
        }
        AudioFence.BeginFence();
        AudioFence.Wait();
        FlushRenderingCommands();
        TestTrue(TEXT("new attack resumes audio"), SkippedSounds > SoundsBeforeNewAttack);
        TestTrue(TEXT("normal HUD resumes"), Seeked.Battle->PresentedHealth.Num() > 1);
        int32 Index = 0;
        for (auto* Component : Ordinary.Battle->AudioManager->CommonAudioPlayers)
        {
            Component->OnAudioPlayStateChangedNative.Remove(Handles[Index++]);
        }
        Index = 0;
        for (auto* Component : Seeked.Battle->AudioManager->CommonAudioPlayers)
        {
            Component->OnAudioPlayStateChangedNative.Remove(SeekHandles[Index++]);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayMatchFinish, "UnrealBench.ReplayTakeover.MatchCompletion",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayMatchFinish::RunTest(const FString&)
{
    using namespace ReplayTests;
    auto* Source = CreateReplayTape(0);
    FReplayBattleWorld Session(Source);
    if (!TestTrue(TEXT("takeover"), Session.Game->TakeOverReplay(0)))
    {
        return false;
    }
    for (int32 FrameIndex = 0; FrameIndex < 10; ++FrameIndex)
    {
        Session.Tick(INP_Right | INP_A, INP_B);
    }
    // Deliver the engine match-end event after a supported training takeover.
    Session.Battle->EndMatch();
    Session.Tick();
    if (!TestTrue(TEXT("actual match completion saves branch"), Session.Game->IsReplayComplete()))
    {
        return false;
    }
    TestEqual(TEXT("one saved branch"), Session.Game->GetSavedReplaySlots().Num(), 1);
    FReplayBattleWorld Reopened(nullptr, Session.Game->GetSavedBranchSlot(), Session.Game->ReplaySlotPrefix);
    auto* Saved = Reopened.Game->GetReplaySource();
    if (!TestNotNull(TEXT("match-end branch reopens"), Saved))
    {
        return false;
    }
    TestEqual(TEXT("match-ending frame recorded"), Saved->LengthInFrames, Session.Game->GetReplayPosition());
    const auto End = FReplaySnapshot::Read(Session);
    for (int32 FrameIndex = 0; FrameIndex < 120; ++FrameIndex)
    {
        Session.Tick(INP_A, INP_B);
    }
    TestTrue(TEXT("completed match no longer advances"), FReplaySnapshot::Read(Session) == End);
    for (int32 FrameIndex = 0; FrameIndex < Saved->LengthInFrames; ++FrameIndex)
    {
        Reopened.Tick();
    }
    TestTrue(TEXT("independent match outcome"), FReplaySnapshot::Read(Reopened) == End);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayDirectTakeover, "UnrealBench.ReplayTakeover.DirectTakeoverAtomicity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayDirectTakeover::RunTest(const FString&)
{
    using namespace ReplayTests;
    auto* Source = CreateReplayTape(24);
    FReplayBattleWorld Session(Source);
    const auto Initial = FReplaySnapshot::Read(Session);
    for (double Position : {-1.0, 25.0, 0.5})
    {
        TestFalse(TEXT("invalid requested frame"), Session.Game->TakeOverReplayAt(Position, 0));
    }
    TestFalse(TEXT("invalid side must not seek first"), Session.Game->TakeOverReplayAt(12, 2));
    TestTrue(TEXT("invalid direct requests are atomic"), FReplaySnapshot::Read(Session) == Initial);
    FReplayBattleWorld Ordinary(Source);
    for (int32 FrameIndex = 0; FrameIndex < 12; ++FrameIndex)
    {
        Ordinary.Tick();
    }
    if (!TestTrue(TEXT("direct catch-up takeover"), Session.Game->TakeOverReplayAt(12, 1)))
    {
        return false;
    }
    TestTrue(TEXT("direct takeover destination"),
             FReplaySnapshot::Read(Session) == FReplaySnapshot::Read(Ordinary));
    TestEqual(TEXT("chosen owner"), Session.Game->GetReplayOwner(), 1);
    TestEqual(TEXT("metadata direct position"), Session.Game->GetWorkingBranch()->TakeoverFrame, 12);
    return true;
}

namespace ReplayTests
{
FString DescribeSnapshot(const FReplaySnapshot& ObservedSnapshot)
{
    FString Value = FString::Printf(
        TEXT("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%s"), ObservedSnapshot.Position,
        ObservedSnapshot.PlayerOnePositionX, ObservedSnapshot.PlayerTwoPositionX,
        ObservedSnapshot.PlayerOneHealth, ObservedSnapshot.PlayerTwoHealth, ObservedSnapshot.Frame,
        ObservedSnapshot.PlayerOneHitstop, ObservedSnapshot.PlayerTwoHitstop, ObservedSnapshot.Timer,
        ObservedSnapshot.PlayerOneMeter, ObservedSnapshot.PlayerTwoMeter,
        ObservedSnapshot.PlayerOneActionTime, ObservedSnapshot.PlayerTwoActionTime,
        *ObservedSnapshot.PlayerOneState.ToString(), *ObservedSnapshot.PlayerTwoState.ToString());
    for (const auto& P : ObservedSnapshot.Projectiles)
    {
        Value += FString::Printf(TEXT(";%d,%d,%d"), P.X, P.Y, P.Z);
    }
    return Value;
}
} // namespace ReplayTests
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayFreshProcess, "UnrealBenchIntegration.ReplayTakeover.FreshProcessPersistence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayFreshProcess::RunTest(const FString&)
{
    using namespace ReplayTests;
    FString WorkerSlot, TracePath, ResultPath, SaveNamespace;
    if (FParse::Value(FCommandLine::Get(), TEXT("NSE009WorkerSlot="), WorkerSlot))
    {
        FParse::Value(FCommandLine::Get(), TEXT("NSE009Trace="), TracePath);
        FParse::Value(FCommandLine::Get(), TEXT("NSE009Result="), ResultPath);
        const FString IdentityPath = WorkerSlot;
        if (!TestTrue(TEXT("opaque identity artifact readable"),
                      FFileHelper::LoadFileToString(WorkerSlot, *IdentityPath))) { return false; }
        FParse::Value(FCommandLine::Get(), TEXT("NSE009Namespace="), SaveNamespace);
        FReplayBattleWorld Reopened(nullptr, WorkerSlot, SaveNamespace);
        auto* Saved = Reopened.Game->GetReplaySource();
        if (!TestNotNull(TEXT("fresh process loads independent branch"), Saved))
        {
            return false;
        }
        TArray<FString> Expected;
        if (!TestTrue(TEXT("trace artifact readable"),
                      FFileHelper::LoadFileToStringArray(Expected, *TracePath)))
        {
            return false;
        }
        if (!TestTrue(TEXT("trace contains initialization"), Expected.Num() > 0))
        {
            return false;
        }
        TestFalse(TEXT("source unavailable in child"),
                  UGameplayStatics::DoesSaveGameExist(SaveNamespace + TEXT("_source"), 0));
        TestEqual(TEXT("metadata survives process boundary"), Saved->TakeoverSide, 1);
        TestEqual(TEXT("fresh process initial state"), DescribeSnapshot(FReplaySnapshot::Read(Reopened)),
                  Expected[0]);
        for (int32 Frame = 1; Frame < Expected.Num(); ++Frame)
        {
            Reopened.Tick(INP_A, INP_A);
            TestEqual(TEXT("fresh process complete combat trace"),
                      DescribeSnapshot(FReplaySnapshot::Read(Reopened)), Expected[Frame]);
        }
        if (!HasAnyErrors())
        {
            FFileHelper::SaveStringToFile(TEXT("passed"), *ResultPath);
        }
        return true;
    }
    auto* Source = CreateReplayTape(48);
    Source->InputsP1[0] |= INP_B;
    Source->InputsP1[11] |= INP_A;
    Source->InputsP2[5] |= INP_B;
    FReplayBattleWorld Session(Source);
    TArray<FString> Trace{DescribeSnapshot(FReplaySnapshot::Read(Session))};
    for (int32 FrameIndex = 0; FrameIndex < 17; ++FrameIndex)
    {
        Session.Tick();
        Trace.Add(DescribeSnapshot(FReplaySnapshot::Read(Session)));
    }
    if (!TestTrue(TEXT("fresh process branch takeover"), Session.Game->TakeOverReplay(1)))
    {
        return false;
    }
    for (int32 FrameIndex = 17; FrameIndex < 160; ++FrameIndex)
    {
        Session.Tick(INP_A,
                     INP_Left | (FrameIndex % 29 == 0 ? INP_A : 0) | (FrameIndex % 53 == 0 ? INP_B : 0));
        Trace.Add(DescribeSnapshot(FReplaySnapshot::Read(Session)));
    }
    if (!TestTrue(TEXT("fresh process branch saved"), Session.Game->FinishReplayBranch()))
    {
        return false;
    }
    WorkerSlot = Session.Game->GetSavedBranchSlot();
    SaveNamespace = Session.Game->ReplaySlotPrefix;
    TestTrue(TEXT("source initially on disk"),
             UGameplayStatics::SaveGameToSlot(Source, SaveNamespace + TEXT("_source"), 0));
    TestTrue(TEXT("source removed before process launch"),
             UGameplayStatics::DeleteGameInSlot(SaveNamespace + TEXT("_source"), 0));
    TracePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / (SaveNamespace + TEXT(".trace")));
    const FString IdentityPath = TracePath + TEXT(".identity");
    TestTrue(TEXT("opaque identity stored for child"), FFileHelper::SaveStringToFile(WorkerSlot, *IdentityPath));
    ResultPath = TracePath + TEXT(".result");
    TestTrue(TEXT("expected trace stored"), FFileHelper::SaveStringArrayToFile(Trace, *TracePath));
    // The child never sees the command line this editor was given, so the RHI
    // selection from spec.yaml would be lost on it. Forward only capability
    // flags: the child still picks -NullRHI for itself below.
    FString EditorArgs;
    {
        static const TCHAR* const Capability[] = {
            TEXT("-d3d11"), TEXT("-d3d12"), TEXT("-vulkan"), TEXT("-opengl"),
            TEXT("-sm5"), TEXT("-sm6"), TEXT("-AllowSoftwareRendering")};
        const FString Parent = FCommandLine::Get();
        for (const TCHAR* const Flag : Capability)
        {
            if (Parent.Contains(Flag, ESearchCase::IgnoreCase))
            {
                if (!EditorArgs.IsEmpty()) { EditorArgs += TEXT(" "); }
                EditorArgs += Flag;
            }
        }
    }
    const FString Args = FString::Printf(
        TEXT("\"%s\" -Unattended -NullRHI -NoSplash -NoSound -ExecCmds=\"Automation RunTests "
             "UnrealBenchIntegration.ReplayTakeover.FreshProcessPersistence; Quit\" -NSE009WorkerSlot=\"%s\" -NSE009Namespace=%s "
             "-NSE009Trace=\"%s\" -NSE009Result=\"%s\" -ABSLOG=\"%s.log\" %s"),
        *FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()), *IdentityPath, *SaveNamespace, *TracePath,
        *ResultPath, *TracePath, *EditorArgs);
    const FString LaunchExecutable = FPlatformProcess::ExecutablePath();
    FProcHandle Child = FPlatformProcess::CreateProc(*LaunchExecutable, *Args, false, true, true,
                                                     nullptr, 0, nullptr, nullptr);
    if (!TestTrue(TEXT("independent Unreal process launched"), Child.IsValid()))
    {
        return false;
    }
    const double Deadline = FPlatformTime::Seconds() + 120;
    while (FPlatformProcess::IsProcRunning(Child) && FPlatformTime::Seconds() < Deadline)
    {
        FPlatformProcess::Sleep(.05f);
    }
    if (FPlatformProcess::IsProcRunning(Child))
    {
        FPlatformProcess::TerminateProc(Child, true);
        AddError(TEXT("child replay timed out"));
    }
    int32 ExitCode = -1;
    FPlatformProcess::GetProcReturnCode(Child, &ExitCode);
    FPlatformProcess::CloseProc(Child);
    TestEqual(TEXT("fresh process automation exit"), ExitCode, 0);
    FString Result;
    TestTrue(TEXT("fresh process validated full trace"), FFileHelper::LoadFileToString(Result, *ResultPath));
    TestEqual(TEXT("worker passed"), Result, FString(TEXT("passed")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayNumericAtomicity, "UnrealBench.ReplayTakeover.NumericAtomicity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayNumericAtomicity::RunTest(const FString&)
{
    using namespace ReplayTests;
    for (int32 Owner : {0, 1})
    {
        auto* Source = CreateReplayTape(12);
        FReplayBattleWorld Session(Source);
        for (int32 Frame = 0; Frame < 5; ++Frame) { Session.Tick(); }
        const TArray<double> InvalidPositions = {
            -1.0, 13.0, .5, 5.25, 11.999999, 12.000001,
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::max(), 2147483648.0, -2147483649.0};
        const auto Before = FReplaySnapshot::Read(Session);
        const TArray<int32> SourceP1 = Source->InputsP1, SourceP2 = Source->InputsP2;
        const auto AssertUnchanged = [&]() {
            TestTrue(TEXT("rejection reports status"), ReportsReplayRejection(Session.Game));
            TestTrue(TEXT("rejection preserves battle"), FReplaySnapshot::Read(Session) == Before);
            TestEqual(TEXT("rejection preserves unowned playback"), Session.Game->GetReplayOwner(), -1);
            TestTrue(TEXT("rejection preserves source P1"), Source->InputsP1 == SourceP1);
            TestTrue(TEXT("rejection preserves source P2"), Source->InputsP2 == SourceP2);
            TestEqual(TEXT("rejection creates no save"), Session.Game->GetSavedReplaySlots().Num(), 0);
        };
        for (double Position : InvalidPositions)
        {
            TestFalse(TEXT("invalid numeric seek"), Session.Game->SeekReplay(Position));
            AssertUnchanged();
            for (int32 Side : {0, 1})
            {
                TestFalse(TEXT("invalid numeric direct takeover"), Session.Game->TakeOverReplayAt(Position, Side));
                AssertUnchanged();
            }
        }
        for (int32 Side : {-1, 2, MIN_int32, MAX_int32})
        {
            TestFalse(TEXT("invalid current-position side"), Session.Game->TakeOverReplay(Side));
            AssertUnchanged();
            TestFalse(TEXT("invalid side rejects before catch-up"), Session.Game->TakeOverReplayAt(10, Side));
            AssertUnchanged();
        }
        if (!TestTrue(TEXT("valid takeover still possible after rejection"), Session.Game->TakeOverReplay(Owner)))
        {
            return false;
        }
        Session.Tick(INP_A, INP_Left);
        const auto BranchState = FReplaySnapshot::Read(Session);
        auto* Branch = Session.Game->GetWorkingBranch();
        if (!TestNotNull(TEXT("working branch"), Branch)) { return false; }
        const TArray<int32> BranchP1 = Branch->InputsP1, BranchP2 = Branch->InputsP2;
        const int32 BranchLength = Branch->LengthInFrames;
        const auto AssertBranchUnchanged = [&]() {
            TestTrue(TEXT("branch rejection status"), ReportsReplayRejection(Session.Game));
            TestEqual(TEXT("branch owner remains selected side"), Session.Game->GetReplayOwner(), Owner);
            TestTrue(TEXT("branch battle unchanged"), FReplaySnapshot::Read(Session) == BranchState);
            Branch = Session.Game->GetWorkingBranch();
            if (!TestNotNull(TEXT("rejection retains branch"), Branch)) { return false; }
            TestTrue(TEXT("rejection retains P1 recording"), Branch->InputsP1 == BranchP1);
            TestTrue(TEXT("rejection retains P2 recording"), Branch->InputsP2 == BranchP2);
            TestEqual(TEXT("rejection retains recording length"), Branch->LengthInFrames, BranchLength);
            TestEqual(TEXT("rejection retains K metadata"), Branch->TakeoverFrame, 5);
            TestEqual(TEXT("rejection retains side metadata"), Branch->TakeoverSide, Owner);
            return true;
        };
        for (int32 Operation = 0; Operation < 3; ++Operation)
        {
            const bool Accepted = Operation == 0 ? Session.Game->TakeOverReplay(1 - Owner)
                : Operation == 1 ? Session.Game->TakeOverReplayAt(0, 1 - Owner) : Session.Game->SeekReplay(0);
            TestFalse(TEXT("branch rejects ownership and seek changes"), Accepted);
            if (!AssertBranchUnchanged()) { return false; }
        }
        for (int32 BadSide : {-2, -1})
        {
            TestFalse(TEXT("negative repeated takeover rejects"), Session.Game->TakeOverReplay(BadSide));
            if (!AssertBranchUnchanged()) { return false; }
            TestFalse(TEXT("negative repeated direct takeover rejects"), Session.Game->TakeOverReplayAt(0, BadSide));
            if (!AssertBranchUnchanged()) { return false; }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayDirectBoundaries, "UnrealBench.ReplayTakeover.DirectBoundaries",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayDirectBoundaries::RunTest(const FString&)
{
    using namespace ReplayTests;
    for (int32 Side : {0, 1})
    {
        for (int32 Position : {0, 1, 11, 12})
        {
            auto* Source = CreateReplayTape(12);
            FReplayBattleWorld Ordinary(Source), Direct(Source);
            for (int32 Frame = 0; Frame < Position; ++Frame) { Ordinary.Tick(); }
            // Exercise a rewind from EOF as well as a forward direct request.
            if (Side == 1)
            {
                for (int32 Frame = 0; Frame < 12; ++Frame) { Direct.Tick(); }
            }
            if (!TestTrue(TEXT("integer boundary direct takeover"), Direct.Game->TakeOverReplayAt(Position, Side)))
            {
                return false;
            }
            TestTrue(TEXT("direct boundary matches ordinary destination"),
                     FReplaySnapshot::Read(Direct) == FReplaySnapshot::Read(Ordinary));
            TestEqual(TEXT("direct boundary selects side"), Direct.Game->GetReplayOwner(), Side);
            auto* Branch = Direct.Game->GetWorkingBranch();
            if (!TestNotNull(TEXT("direct boundary branch"), Branch)) { return false; }
            TestEqual(TEXT("direct boundary metadata"), Branch->TakeoverFrame, Position);
            for (int32 Frame = 0; Frame < Position; ++Frame)
            {
                if (!TestTrue(TEXT("direct boundary prefix is available"),
                              Branch->InputsP1.IsValidIndex(Frame) && Branch->InputsP2.IsValidIndex(Frame)))
                { return false; }
                TestEqual(TEXT("direct boundary P1 prefix"), Branch->InputsP1[Frame], Source->InputsP1[Frame]);
                TestEqual(TEXT("direct boundary P2 prefix"), Branch->InputsP2[Frame], Source->InputsP2[Frame]);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayPauseAndCommands, "UnrealBench.ReplayTakeover.PauseAndCommands",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayPauseAndCommands::RunTest(const FString&)
{
    using namespace ReplayTests;
    for (int32 Side : {0, 1})
    {
        auto* Source = CreateReplayTape(8);
        // Reset commands from either input stream must not escape after takeover.
        Source->InputsP1.Init(INP_A | INP_B | INP_Rematch | INP_ResetTraining, 8);
        Source->InputsP2 = Source->InputsP1;
        FReplayBattleWorld Session(Source);
        if (!TestTrue(TEXT("command probe takeover"), Session.Game->TakeOverReplay(Side))) { return false; }
        const int32 Combat = INP_A | INP_B;
        for (int32 Frame = 0; Frame < 3; ++Frame)
        {
            Session.Tick(Combat | INP_Rematch | INP_ResetTraining, Combat | INP_Rematch | INP_ResetTraining);
            auto* Branch = Session.Game->GetWorkingBranch();
            if (!TestTrue(TEXT("held input frame recorded"), Branch &&
                          Branch->InputsP1.IsValidIndex(Frame) && Branch->InputsP2.IsValidIndex(Frame)))
            { return false; }
            TestEqual(TEXT("P1 retains held combat and strips reset commands"), Branch->InputsP1[Frame], Combat);
            TestEqual(TEXT("P2 retains held combat and strips reset commands"), Branch->InputsP2[Frame], Combat);
        }
        const auto Paused = FReplaySnapshot::Read(Session);
        const int32 Recorded = Session.Game->GetWorkingBranch()->LengthInFrames;
        Session.Battle->bPauseGame = true;
        Session.Game->PauseReplay(false);
        for (int32 Frame = 0; Frame < 3; ++Frame) { Session.Tick(INP_Left, INP_Right); }
        TestTrue(TEXT("battle pause freezes replay battle"), FReplaySnapshot::Read(Session) == Paused);
        TestEqual(TEXT("battle pause freezes recording"), Session.Game->GetWorkingBranch()->LengthInFrames, Recorded);
        Session.Game->PauseReplay(true);
        Session.Battle->bPauseGame = false;
        Session.Tick(INP_Left, INP_Right);
        TestTrue(TEXT("replay pause remains effective when battle pause clears"), FReplaySnapshot::Read(Session) == Paused);
        Session.Game->PauseReplay(false);
        Session.Tick(INP_Left, INP_Right);
        TestEqual(TEXT("clearing both pauses advances once"), Session.Game->GetReplayPosition(), Recorded + 1);
        TestTrue(TEXT("finish pause probe"), Session.Game->FinishReplayBranch());
        const FString SavedSlot = Session.Game->GetSavedBranchSlot();
        Session.Game->FinishReplayBranch();
        TestEqual(TEXT("second finish keeps one save"), Session.Game->GetSavedReplaySlots().Num(), 1);
        TestEqual(TEXT("second finish keeps identity"), Session.Game->GetSavedBranchSlot(), SavedSlot);
        auto* Fresh = CreateReplayTape(8);
        if (!TestTrue(TEXT("fresh session accepted after finished branch"), Session.Game->BeginReplaySession(Fresh)))
        { return false; }
        TestEqual(TEXT("fresh session clears takeover owner"), Session.Game->GetReplayOwner(), -1);
        TestEqual(TEXT("fresh session starts at zero"), Session.Game->GetReplayPosition(), 0);
        TestFalse(TEXT("nonempty fresh session is not complete"), Session.Game->IsReplayComplete());
    }
    return true;
}

#include "hlsl-tests.inl"

#include "common-particle-tests.inl"

#include "headless-audio-tests.inl"
