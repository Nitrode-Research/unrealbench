#include "NightSkyEngine/Fixtures/RoomReplayTestCatalog.h"
#include "NightSkyEngine/Fixtures/RoomWidgetTestAccess.h"
#include "NightSkyEngine/Fixtures/RoomWidgetCapture.h"
#include "Components/Button.h"
#include "Misc/ScopeExit.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NightSkyEngine/Network/SpectatorRoom.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"

namespace
{
USpectatorRoom *NewRoom()
{
    auto *GameInstance = NewObject<UGameInstance>();
    auto *Room = NewObject<USpectatorRoom>(GameInstance);
    Room->AddToRoot();
    return Room;
}

FRoomCommand Operation(const TCHAR *Name, int32 Number = 0, const FString &Value = TEXT(""))
{
    FRoomCommand Request;
    Request.Operation = Name;
    Request.Number = Number;
    Request.Value = Value;
    Request.Nonce = FGuid::NewGuid().ToString();
    return Request;
}

// Diagnostic text is user-facing. Tests verify effects, never English spellings.
bool RoomResponseHasEffect(const FRoomDelivery &Delivery, const TCHAR *Expectation)
{
    const FString Expected(Expectation);
    if (Expected != TEXT("accepted") && Expected != TEXT("exported") && Expected != TEXT("duplicate") &&
        Delivery.Status.TrimStartAndEnd().IsEmpty())
        return false;
    if (FCString::Strcmp(Expectation, TEXT("exported")) == 0)
        return !Delivery.Replay.IsEmpty();
    if (FCString::Strcmp(Expectation, TEXT("pending")) == 0)
        return Delivery.Replay.IsEmpty();
    return true;
}

bool CheckRoomCommand(FAutomationTestBase &Test, USpectatorRoom *Room, const FString &Identity,
                      const FRoomCommand &Request, const TCHAR *Expectation)
{
    const auto Before = Room->Observe(Identity);
    const auto Result = Room->Command(Identity, Request);
    if (!RoomResponseHasEffect(Result, Expectation))
        return false;
    const FString Expected(Expectation);
    if (Expected != TEXT("accepted") && Expected != TEXT("exported") && Expected != TEXT("pending"))
    {
        const auto After = Room->Observe(Identity);
        return Before.Match == After.Match && Before.Frame == After.Frame && Before.Mode == After.Mode &&
               Before.Assignment == After.Assignment && Before.Offer == After.Offer &&
               Before.Membership == After.Membership && Before.Role == After.Role &&
               Before.Locked == After.Locked && Before.Paused == After.Paused &&
               (Expected != TEXT("duplicate") || Before.Acknowledgement == After.Acknowledgement);
    }
    if (Request.Operation == TEXT("lock") || Request.Operation == TEXT("unlock"))
        return Result.Locked == (Request.Operation == TEXT("lock"));
    if (Request.Operation == TEXT("combat-pause") || Request.Operation == TEXT("combat-resume"))
        return Result.Paused == (Request.Operation == TEXT("combat-pause"));
    if (Request.Operation == TEXT("start"))
        // Starting combat does not switch the organizer's selected historical timeline.
        // Fresh active-match identity is checked through the current fighters below.
        return Room->IsActive();
    if (Request.Operation == TEXT("stage"))
        return Result.SelectedStage == Request.Value;
    if (Request.Operation == TEXT("character"))
        return Result.SelectedCharacters.Contains(Request.Value);
    if (Request.Operation == TEXT("offer"))
        return !Room->Observe(Request.Value).Offer.IsEmpty();
    return true; // ready, queue and inputs are checked by subsequent offer/start/native-frame evidence.
}

bool RefreshCurrentFighterAssignments(FAutomationTestBase &Test, USpectatorRoom *Room,
                                      const FString &FirstIdentity, const FString &SecondIdentity,
                                      FString &FirstAssignment, FString &SecondAssignment)
{
    const auto First = Room->Observe(FirstIdentity);
    const auto Second = Room->Observe(SecondIdentity);
    auto OwnsSeat = [](const FRoomDelivery &Delivery, const FString &Identity) {
        return !Delivery.Membership.IsEmpty() && !Delivery.Assignment.IsEmpty() &&
               (Delivery.Role == TEXT("fighter") ||
                (Identity == TEXT("host") && Delivery.Role == TEXT("organizer")));
    };
    if (!Test.TestTrue(TEXT("current authenticated fighters retain distinct owned seats"),
                       OwnsSeat(First, FirstIdentity) && OwnsSeat(Second, SecondIdentity) &&
                           First.Assignment != Second.Assignment))
        return false;
    FirstAssignment = First.Assignment;
    SecondAssignment = Second.Assignment;
    return true;
}

bool CheckReplayPublicFrames(FAutomationTestBase &Test, const FRoomMatch &Replay)
{
    for (int32 FrameIndex = 0; FrameIndex < Replay.Frames.Num(); ++FrameIndex)
    {
        FString Match, Description;
        TArray<int32> InputsA, InputsB;
        if (!Test.TestTrue(TEXT("exported frame decodes through public interface"),
                           USpectatorRoom::DecodeConfirmedInputs(Replay.Frames[FrameIndex], Match,
                                                                 InputsA, InputsB, Description)))
            return false;
        if (!Test.TestEqual(TEXT("exported frame retains match identity"), Match, Replay.Id) ||
            !Test.TestEqual(TEXT("exported first input prefix ends at represented frame"), InputsA.Num(), FrameIndex) ||
            !Test.TestEqual(TEXT("exported second input prefix ends at represented frame"), InputsB.Num(), FrameIndex))
            return false;
        for (int32 InputIndex = 0; InputIndex < FrameIndex; ++InputIndex)
            if (!Test.TestEqual(TEXT("exported first prefix preserves confirmed input"),
                                InputsA[InputIndex], Replay.Frames[InputIndex + 1].Input1) ||
                !Test.TestEqual(TEXT("exported second prefix preserves confirmed input"),
                                InputsB[InputIndex], Replay.Frames[InputIndex + 1].Input2))
                return false;
    }
    return true;
}

FString Slot()
{
    return TEXT("Automation-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
}
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomCreation, "NightSky.Room.CreationAndAuthentication",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomCreation::RunTest(const FString &)
{
    for (int32 Delay : TArray<int32>{-1, 0, 1, 120, 600, 601})
    {
        auto *Room = NewRoom();
        TestEqual(TEXT("integer delay contract"),
                  Room->Create(Slot(), TEXT("organizer"), TEXT("secret"), Delay), Delay >= 0 && Delay <= 600);
        if (Delay >= 0 && Delay <= 600)
        {
            TestFalse(TEXT("bad organizer credentials"),
                      Room->Authenticate(TEXT("organizer"), TEXT("wrong"), {}));
            TestTrue(TEXT("spectator joins"), Room->Authenticate(TEXT("viewer"), TEXT("viewer-secret"), {}));
            auto Delivery = Room->Observe(TEXT("viewer"));
            TestEqual(TEXT("spectator role"), Delivery.Role, FString(TEXT("spectator")));
            TestTrue(TEXT("durable acknowledgement"), Delivery.Acknowledgement > 0);
            TestTrue(TEXT("no initialized match is unavailable"),
                     RoomResponseHasEffect(Delivery, TEXT("history unavailable")));
            auto ObserveRequest = Operation(TEXT("observe"));
            const auto Observation = Room->Command(TEXT("viewer"), ObserveRequest);
            const auto RepeatedObservation = Room->Command(TEXT("viewer"), ObserveRequest);
            TestEqual(TEXT("read response correlates supplied nonce"), Observation.RequestNonce,
                      ObserveRequest.Nonce);
            TestEqual(TEXT("repeated read keeps correlation"), RepeatedObservation.RequestNonce,
                      ObserveRequest.Nonce);
            TestEqual(TEXT("read does not advance durable acknowledgement"),
                      RepeatedObservation.Acknowledgement, Delivery.Acknowledgement);
            TestEqual(TEXT("repeated read preserves room identity"), RepeatedObservation.Room,
                      Observation.Room);
            TestEqual(TEXT("repeated read preserves membership"), RepeatedObservation.Membership,
                      Observation.Membership);
            TestEqual(TEXT("repeated read preserves member role"), RepeatedObservation.Role,
                      Observation.Role);
            TestEqual(TEXT("repeated read preserves fighter assignment"), RepeatedObservation.Assignment,
                      Observation.Assignment);
            TestEqual(TEXT("repeated read preserves selection lock"), RepeatedObservation.Locked,
                      Observation.Locked);
            auto QueueAfterObserve = ObserveRequest;
            QueueAfterObserve.Operation = TEXT("queue");
            const auto Queued = Room->Command(TEXT("viewer"), QueueAfterObserve);
            TestTrue(TEXT("observe does not consume mutation nonce"),
                     RoomResponseHasEffect(Queued, TEXT("accepted")));
            TestEqual(TEXT("mutation still correlates observed nonce"), Queued.RequestNonce,
                      ObserveRequest.Nonce);
            TestTrue(
                TEXT("spectator cannot lock"),
                CheckRoomCommand(*this, Room, TEXT("viewer"), Operation(TEXT("lock")), TEXT("unauthorized")));
            TestTrue(TEXT("rejected controls still return membership"),
                     Room->Observe(TEXT("viewer")).Roster.Num() == 2);
        }
        Room->RemoveFromRoot();
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSuccession, "NightSky.Room.SeatSuccessionAndRecovery",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomSuccession::RunTest(const FString &)
{
    const FString Path = Slot();
    auto *Room = NewRoom();
    if (!TestTrue(TEXT("create"), Room->Create(Path, TEXT("host"), TEXT("secret"), 120)))
    {
        Room->RemoveFromRoot();
        return false;
    }
    Room->Authenticate(TEXT("a"), TEXT("a-secret"), {});
    Room->Authenticate(TEXT("b"), TEXT("b-secret"), {});
    auto Queue = Operation(TEXT("queue"));
    TestTrue(TEXT("queue once"), CheckRoomCommand(*this, Room, TEXT("a"), Queue, TEXT("accepted")));
    TestTrue(TEXT("duplicate queue"), CheckRoomCommand(*this, Room, TEXT("a"), Queue, TEXT("duplicate")));
    TestTrue(TEXT("offer"), CheckRoomCommand(*this, Room, TEXT("host"),
                                             Operation(TEXT("offer"), 0, TEXT("a")), TEXT("accepted")));
    auto Accept = Operation(TEXT("accept"));
    Accept.Assignment = Room->Observe(TEXT("a")).Offer;
    auto First = Room->Command(TEXT("a"), Accept);
    TestEqual(TEXT("promoted"), First.Role, FString(TEXT("fighter")));
    TestFalse(TEXT("assignment published"), First.Assignment.IsEmpty());
    TestEqual(TEXT("duplicate acceptance"), Room->Command(TEXT("a"), Accept).Assignment, First.Assignment);
    Room->Command(TEXT("host"), Operation(TEXT("lock")));
    // Persistence is checked after actual authority death in SixProcessAuthority.
    // These seat operations use the current authority, without a second store owner.
    TestTrue(TEXT("current identity reauthenticates"),
             Room->Authenticate(TEXT("a"), TEXT("a-secret"), {}));
    auto Restored = Room->Observe(TEXT("a"));
    TestEqual(TEXT("same assignment"), Restored.Assignment, First.Assignment);
    TestTrue(TEXT("reauthentication preserves selection lock"), Restored.Locked);
    Room->Depart(TEXT("a"));
    Room->Authenticate(TEXT("a"), TEXT("a-secret"), {});
    TestEqual(TEXT("departed fighter rejoins as spectator"), Room->Observe(TEXT("a")).Role,
              FString(TEXT("spectator")));
    auto StaleLeave = Operation(TEXT("leave"));
    StaleLeave.Membership = Restored.Membership;
    TestTrue(TEXT("stale departure cannot remove newer membership"),
             CheckRoomCommand(*this, Room, TEXT("a"), StaleLeave, TEXT("stale membership")));
    TestTrue(TEXT("new member remains usable"),
             CheckRoomCommand(*this, Room, TEXT("a"), Operation(TEXT("queue")), TEXT("accepted")));
    TestTrue(TEXT("vacancy blocks start"),
             CheckRoomCommand(*this, Room, TEXT("host"), Operation(TEXT("start")),
                              TEXT("fighters not ready")));
    Room->RemoveFromRoot();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomOfferOwnership, "NightSky.Room.OfferOwnershipAndExpiration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomOfferOwnership::RunTest(const FString &)
{
    auto *Room = NewRoom();
    ON_SCOPE_EXIT
    {
        Room->RemoveFromRoot();
    };
    if (!TestTrue(TEXT("create room"), Room->Create(Slot(), TEXT("host"), TEXT("secret"), 0)))
        return false;
    for (const TCHAR *Identity : {TEXT("a"), TEXT("b")})
    {
        if (!TestTrue(TEXT("authenticate queued spectator"),
                      Room->Authenticate(Identity, TEXT("secret"), {})))
            return false;
        Room->Command(Identity, Operation(TEXT("queue")));
    }
    // Withdrawal changes eligibility, even if the command response is acknowledged.
    Room->Command(TEXT("a"), Operation(TEXT("withdraw")));
    Room->Command(TEXT("host"), Operation(TEXT("offer"), 0, TEXT("a")));
    TestTrue(TEXT("withdrawn spectator cannot receive a new vacant-seat offer"),
             Room->Observe(TEXT("a")).Offer.IsEmpty());
    TestEqual(TEXT("withdrawal preserves spectator membership"), Room->Observe(TEXT("a")).Role,
              FString(TEXT("spectator")));
    Room->Command(TEXT("a"), Operation(TEXT("queue")));
    Room->Command(TEXT("host"), Operation(TEXT("offer"), 0, TEXT("a")));
    const FString FirstOffer = Room->Observe(TEXT("a")).Offer;
    if (!TestFalse(TEXT("offer has identity"), FirstOffer.IsEmpty()))
        return false;
    auto Accept = Operation(TEXT("accept"));
    Accept.Assignment = FirstOffer;
    Room->Command(TEXT("b"), Accept);
    TestEqual(TEXT("wrong owner remains spectator"), Room->Observe(TEXT("b")).Role,
              FString(TEXT("spectator")));
    TestEqual(TEXT("wrong owner cannot consume offer"), Room->Observe(TEXT("a")).Offer, FirstOffer);
    auto Decline = Operation(TEXT("decline"));
    Decline.Assignment = FirstOffer;
    Room->Command(TEXT("a"), Decline);
    TestEqual(TEXT("decline preserves spectator"), Room->Observe(TEXT("a")).Role, FString(TEXT("spectator")));
    TestTrue(TEXT("decline removes offer"), Room->Observe(TEXT("a")).Offer.IsEmpty());
    Accept.Nonce = FGuid::NewGuid().ToString();
    Room->Command(TEXT("a"), Accept);
    TestEqual(TEXT("declined offer cannot transfer later"), Room->Observe(TEXT("a")).Role,
              FString(TEXT("spectator")));
    Room->Command(TEXT("a"), Operation(TEXT("queue")));
    Room->Command(TEXT("host"), Operation(TEXT("offer"), 0, TEXT("a")));
    const FString DepartedOffer = Room->Observe(TEXT("a")).Offer;
    if (!TestFalse(TEXT("replacement offer exists"), DepartedOffer.IsEmpty()))
        return false;
    Room->Depart(TEXT("a"));
    Room->Authenticate(TEXT("a"), TEXT("secret"), {});
    Accept.Assignment = DepartedOffer;
    Accept.Nonce = FGuid::NewGuid().ToString();
    Room->Command(TEXT("a"), Accept);
    TestEqual(TEXT("departure expires offered seat"), Room->Observe(TEXT("a")).Role,
              FString(TEXT("spectator")));
    Room->Command(TEXT("host"), Operation(TEXT("offer"), 0, TEXT("b")));
    Accept.Assignment = Room->Observe(TEXT("b")).Offer;
    Accept.Nonce = FGuid::NewGuid().ToString();
    const auto Promoted = Room->Command(TEXT("b"), Accept);
    TestEqual(TEXT("vacancy remains usable by eligible owner"), Promoted.Role, FString(TEXT("fighter")));
    Accept.Nonce = FGuid::NewGuid().ToString();
    Room->Command(TEXT("b"), Accept);
    TestEqual(TEXT("same offer cannot allocate twice with fresh nonce"), Room->Observe(TEXT("b")).Assignment,
              Promoted.Assignment);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomStateful, "NightSky.Room.SeededMembership",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomStateful::RunTest(const FString &)
{
    for (int32 Seed = 24001; Seed <= 24003; ++Seed)
    {
        FRandomStream Random(Seed);
        auto *Room = NewRoom();
        if (!Room->Create(Slot(), TEXT("host"), TEXT("secret"), Seed % 601))
        {
            AddError(TEXT("room creation failed"));
            Room->RemoveFromRoot();
            return false;
        }
        if (!TestTrue(TEXT("initial viewer authentication"),
                      Room->Authenticate(TEXT("viewer"), TEXT("secret"), {})))
        {
            Room->RemoveFromRoot();
            return false;
        }
        const int32 PresentRosterSize = Room->Observe(TEXT("viewer")).Roster.Num();
        bool Locked = false;
        bool Present = true;
        for (int32 Step = 0; Step < 72; ++Step)
        {
            // Every ordered pair of operations is exercised per delay/seek seed.
            const int32 Pair = Step / 2;
            const int32 Choice = Step % 2 == 0 ? Pair / 6 : Pair % 6;
            if (Choice == 0)
            {
                Locked = true;
                Room->Command(TEXT("host"), Operation(TEXT("lock")));
            }
            if (Choice == 1)
            {
                Locked = false;
                Room->Command(TEXT("host"), Operation(TEXT("unlock")));
            }
            if (Choice == 2)
            {
                Room->Depart(TEXT("viewer"));
                Present = false;
            }
            if (Choice == 3)
            {
                Present = Room->Authenticate(TEXT("viewer"), TEXT("secret"), {});
                TestTrue(TEXT("viewer reauthentication succeeds"), Present);
            }
            if (Choice == 4)
                Room->Command(TEXT("viewer"), Operation(TEXT("queue")));
            if (Choice == 5)
                Room->Command(TEXT("viewer"), Operation(TEXT("seek"), Random.RandRange(-1, 100)));
            const auto Delivery = Room->Observe(TEXT("viewer"));
            const auto Organizer = Room->Observe(TEXT("host"));
            TestEqual(FString::Printf(TEXT("seed %d step %d authoritative lock"), Seed, Step),
                      Organizer.Locked, Locked);
            TestEqual(TEXT("no gameplay disclosure without match"), Delivery.Frame, -1);
            TestTrue(TEXT("no gameplay bytes without an initialized match"),
                     Delivery.Gameplay.State.IsEmpty());
            TestTrue(TEXT("authenticated organizer remains usable"), Organizer.Acknowledgement > 0);
            if (Present)
            {
                TestEqual(TEXT("authenticated viewer sees current lock"), Delivery.Locked, Locked);
                TestFalse(TEXT("reauthenticated viewer has acknowledged membership"),
                          Delivery.Membership.IsEmpty());
                TestEqual(TEXT("reauthentication does not add roster entries"), Delivery.Roster.Num(),
                          PresentRosterSize);
            }
            // Departed identities need not receive room metadata. Continue checking
            // authoritative controls through the still-authenticated organizer.
        }
        Room->RemoveFromRoot();
    }
    return true;
}
#endif
#if WITH_DEV_AUTOMATION_TESTS
#include "NightSkyEngine/Fixtures/ReplayFixture.h"
#include "NightSkyEngine/Data/PrimaryCharaData.h"
#include "NightSkyEngine/Data/PrimaryStageData.h"
#include "NightSkyEngine/Battle/NightSkyPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RHIGlobals.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "AudioThread.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NightSkyEngine/Battle/Actors/ParticleManager.h"
#include "Components/AudioComponent.h"
#include "NightSkyEngine/Battle/Actors/AudioManager.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonWriter.h"
#include "RoomPythonAutomation.h"
#include "RoomRenderedEvidence.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"

namespace
{
FString DescribeActiveObject(const ABattleObject *Object, ANightSkyGameState *Battle)
{
    const int32 OwnerSide = Object->Player == Battle->GetMainPlayer(true)    ? 0
                            : Object->Player == Battle->GetMainPlayer(false) ? 1
                                                                             : -1;
    return FString::Printf(TEXT("%s|%s|%d|%d|%d|%d|%d|%d|%d"), *Object->ObjectStateName.ToString(),
                           *Object->GetCelName().ToString(), OwnerSide, Object->ActionTime, Object->PosX,
                           Object->PosY, Object->SpeedX, Object->SpeedY, int32(Object->Hitstop));
}

struct FRoomBattleWorld
{
    UReplayFixtureGameInstance *Game;
    UWorld *World;
    ANightSkyGameState *Battle = nullptr;
    FName WorldContextHandle;
    TArray<TStrongObjectPtr<UObject>> FixtureAssets;

    void OwnConfiguration(const FBattleData &Configuration)
    {
        FixtureAssets.Emplace(Configuration.Stage);
        for (const auto &Character : Configuration.PlayerListP1)
            FixtureAssets.Emplace(Character.LoadSynchronous());
        for (const auto &Character : Configuration.PlayerListP2)
            FixtureAssets.Emplace(Character.LoadSynchronous());
    }

    FRoomBattleWorld()
    {
        Game = NewObject<UReplayFixtureGameInstance>(GEngine);
        Game->InitializeStandalone();
        Game->AddToRoot();
        World = Game->GetWorld();
        WorldContextHandle = GEngine->GetWorldContextFromWorldChecked(World).ContextHandle;
        Game->BattleVersion = TEXT("RoomFixture-1");
        Game->FighterRunner = LocalPlay;
        Game->IsTraining = true;
        // Character selection authors color zero for each one-character seat.
        // Keep the independent expected configuration explicit as well.
        Game->BattleData.ColorIndicesP1 = {0};
        Game->BattleData.ColorIndicesP2 = {0};
    }

    void Begin()
    {
        FURL URL;
        URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);
        World->InitializeActorsForPlay(URL);
        World->BeginPlay();
        World->SpawnActor<ANightSkyPlayerController>();
        World->SpawnActor<ANightSkyPlayerController>();
        Battle = World->SpawnActor<AReplayFixtureBattle>();
    }

    void AssociateRoomBattle()
    {
        Game->UseRoomFixtureBattle(Cast<AReplayFixtureBattle>(Battle));
    }

    static FWorldContext *FindContext(FName Handle)
    {
        return GEngine->GetWorldContextFromHandle(Handle);
    }

    bool HasPendingTravel() const
    {
        const auto *Context = FindContext(WorldContextHandle);
        return !Context || !Context->World() || !Context->World()->HasBegunPlay() ||
               !Context->World()->NextURL.IsEmpty() || !Context->TravelURL.IsEmpty() ||
               Context->PendingNetGame != nullptr || Context->SeamlessTravelHandler.IsInTransition();
    }

    void AdvanceOwnedWorld(float Delta, const TFunction<bool()> &Checkpoint = TFunction<bool()>())
    {
        // InitializeStandalone created this fixture-only context. It is advanced
        // here, once per latent update, rather than entering the editor/engine tick.
        FName PreviousContext;
        for (const auto &Context : GEngine->GetWorldContexts())
            if (Context.World() == GWorld)
                PreviousContext = Context.ContextHandle;
        ON_SCOPE_EXIT
        {
            const auto *Restored = FindContext(PreviousContext);
            GWorld = Restored ? Restored->World() : nullptr;
        };
        auto *Context = FindContext(WorldContextHandle);
        if (!Context || !Context->World())
            return;
        GWorld = Context->World();
        GEngine->TickWorldTravel(*Context, Delta);
        // Travel can replace both the world and its gameplay actors.
        Context = FindContext(WorldContextHandle);
        World = Context ? Context->World() : nullptr;
        GWorld = World;
        Game->CompleteRoomFixtureBinding();
        // Public binding can replace the owned context again.
        Context = FindContext(WorldContextHandle);
        World = Context ? Context->World() : nullptr;
        GWorld = World;
        Battle = Game->AssociatedRoomFixtureBattle();
        // Travel may have completed this setup. Observe public success and native
        // readiness before a further native tick can precede the manual test steps.
        if (Checkpoint && Checkpoint())
            return;
        // Public observation/binding may itself travel or replace the world.
        Context = FindContext(WorldContextHandle);
        World = Context ? Context->World() : nullptr;
        if (World)
        {
            GWorld = World;
            World->Tick(LEVELTICK_All, Delta);
        }
        Context = FindContext(WorldContextHandle);
        World = Context ? Context->World() : nullptr;
        GWorld = World;
        Game->CompleteRoomFixtureBinding();
        Context = FindContext(WorldContextHandle);
        World = Context ? Context->World() : nullptr;
        GWorld = World;
        Battle = Game->AssociatedRoomFixtureBattle();
        if (Checkpoint)
            Checkpoint();
    }

    ~FRoomBattleWorld()
    {
        // Travel may have replaced and destroyed the original standalone world.
        if (UWorld *Current = Game->GetWorld())
        {
            Current->EndPlay(EEndPlayReason::Quit);
            Current->DestroyWorld(false);
            GEngine->DestroyWorldContext(Current);
        }
        Game->Shutdown();
        Game->RemoveFromRoot();
    }
};

// Each phase runs once on the game thread. Returning to the automation runner
// lets normal engine initialization, travel and asynchronous loading progress.
class FRoomScenarioCommand : public IAutomationLatentCommand
{
  public:
    using FStep = TFunction<bool(FRoomScenarioCommand &)>;
    FRoomScenarioCommand(FAutomationTestBase *InTest, FStep InStep) : Test(InTest), Step(MoveTemp(InStep))
    {
    }

    void AwaitStart(
        USpectatorRoom *Room, TSharedRef<FRoomBattleWorld> World, const FString &Fighter,
        const FString &PreviousMatch, int64 InStartTick, const FString &Assertion, FStep Continuation,
        TFunction<bool(const FString &, int64)> ImmediateSuccess = TFunction<bool(const FString &, int64)>())
    {
        PendingRoom = Room;
        PendingWorld = World;
        PendingFighter = Fighter;
        Previous = PreviousMatch;
        RecordedMatch.Empty();
        RecordedStartTick = InStartTick;
        StartAssertion = Assertion;
        NextStep = MoveTemp(Continuation);
        SuccessWitness = MoveTemp(ImmediateSuccess);
        PublicStarted = false;
        Waiting = true;
        WaitStarted = LastPump = FPlatformTime::Seconds();
    }

    const FString &SuccessfulMatch() const
    {
        return RecordedMatch;
    }
    int64 SuccessfulStartTick() const
    {
        return RecordedStartTick;
    }

    void RunFrames(USpectatorRoom *Room, TSharedRef<FRoomBattleWorld> World, int32 Count,
                   TFunction<bool(int32)> Submit, TFunction<bool(int32)> Inspect, FStep Complete,
                   bool StopWhenInactive = false, bool RequireNativeProgress = true)
    {
        struct FFrames
        {
            int32 Frame = 1, BeforeNativeFrame = 0;
            bool Submitted = false, Stepped = false;
            double Began = FPlatformTime::Seconds(), LastAdvance = Began;
        };
        auto Frames = MakeShared<FFrames>();
        NextStep = [=](FRoomScenarioCommand &Scenario) mutable {
            const double Now = FPlatformTime::Seconds();
            const float Delta = float(Now - Frames->LastAdvance);
            Frames->LastAdvance = Now;
            auto AdvanceAndYield = [&]() {
                if (Now - Frames->Began >= 30.)
                {
                    Scenario.Test->AddError(
                        FString::Printf(TEXT("native fixture gameplay did not progress within 30 seconds; "
                                             "requested pair=%d submitted=%d stepped=%d"),
                                        Frames->Frame, int32(Frames->Submitted), int32(Frames->Stepped)));
                    return false;
                }
                World->AdvanceOwnedWorld(Delta);
                Scenario.RepeatStep = true;
                return true;
            };
            World->World = World->Game->GetWorld();
            World->Game->CompleteRoomFixtureBinding();
            World->Battle = World->Game->AssociatedRoomFixtureBattle();
            if (World->HasPendingTravel() || !World->Battle || !World->Battle->GetMainPlayer(true) ||
                !World->Battle->GetMainPlayer(false))
                return AdvanceAndYield();
            if (Frames->Frame > Count || (!Frames->Submitted && StopWhenInactive && !Room->IsActive()))
            {
                Scenario.NextStep = MoveTemp(Complete);
                return true;
            }
            if (!Frames->Submitted)
            {
                Frames->BeforeNativeFrame = World->Battle->BattleState.FrameNumber;
                if (!Submit(Frames->Frame))
                    return false;
                Frames->Submitted = true;
            }
            if (!Frames->Stepped)
            {
                Room->BattleTick(OneFrame);
                Frames->Stepped = true;
                // The dispatch may request travel or complete asynchronous work.
                // Yield before inspecting, without resubmitting the pair or tick.
                Scenario.RepeatStep = true;
                return true;
            }
            // Reacquired at the beginning of each update: never read the previous
            // actor after public dispatch, travel, destruction or setup replacement.
            if (RequireNativeProgress && Room->IsActive() &&
                World->Battle->BattleState.FrameNumber == Frames->BeforeNativeFrame)
                return AdvanceAndYield();
            if (!Inspect(Frames->Frame))
                return false;
            ++Frames->Frame;
            Frames->Submitted = Frames->Stepped = false;
            Scenario.RepeatStep = true;
            return true;
        };
    }

    void Continue(FStep Continuation)
    {
        NextStep = MoveTemp(Continuation);
    }

    void YieldToEngine(FStep Continuation)
    {
        auto Yielded = MakeShared<bool>(false);
        NextStep = [Yielded, Continuation = MoveTemp(Continuation)](FRoomScenarioCommand &Scenario) mutable {
            if (!*Yielded)
            {
                *Yielded = true;
                Scenario.RepeatStep = true;
            }
            else
                Scenario.NextStep = MoveTemp(Continuation);
            return true;
        };
    }

    void AwaitPresentation(USpectatorRoom *Presentation, TSharedRef<FRoomBattleWorld> FixtureWorld,
                           const FRoomDelivery &Delivery, TFunction<bool(ANightSkyGameState *)> MatchesState,
                           bool RequireImage, double Deadline, const FString &Assertion, FStep Continuation)
    {
        Presentation->PresentDelivery(Delivery);
        auto LastAdvance = MakeShared<double>(FPlatformTime::Seconds());
        auto Yielded = MakeShared<bool>(false);
        NextStep = [=](FRoomScenarioCommand &Scenario) mutable {
            // PresentDelivery does not promise that the image updates in its call.
            // Allow ordinary rendering/loading work before the first observation.
            if (!*Yielded)
            {
                *Yielded = true;
                Scenario.RepeatStep = true;
                return true;
            }
            // Read only public presentation state. Allocation identities are unrestricted.
            auto Ready = [&]() {
                auto *Battle = Presentation->PresentationBattle();
                auto *Texture = RequireImage ? Presentation->PresentationTexture() : nullptr;
                return IsValid(Battle) && IsValid(Battle->GetMainPlayer(true)) && IsValid(Battle->GetMainPlayer(false)) &&
                    Battle->BattleState.FrameNumber == Delivery.Frame && MatchesState(Battle) &&
                    (!RequireImage || (IsValid(Texture) && Texture->SizeX > 0 && Texture->SizeY > 0));
            };
            auto CompleteIfReady = [&]() {
                if (!Ready())
                    return false;
                Scenario.Test->TestTrue(Assertion, true);
                Scenario.NextStep = MoveTemp(Continuation);
                return true;
            };
            if (CompleteIfReady())
                return true;
            const double Now = FPlatformTime::Seconds();
            if (Now >= Deadline)
            {
                Scenario.Test->AddError(Assertion + TEXT(": matching presentation did not settle within the fixture bound"));
                return false;
            }
            // Only the explicitly authored fixture context can be pumped here. Any
            // separately allocated candidate presentation gets ordinary engine turns.
            // The checkpoint stops before a native gameplay tick once ready.
            FixtureWorld->AdvanceOwnedWorld(float(Now - *LastAdvance), Ready);
            *LastAdvance = Now;
            if (CompleteIfReady())
                return true;
            Scenario.RepeatStep = true;
            return true;
        };
    }

    bool Update() override
    {
        // A synchronous successful response can enter manual fixture control
        // immediately. Only pending preparation yields another engine frame.
        for (;;)
        {
            if (Waiting)
            {
                const EStartSetup State = ObserveStartSetup(TEXT("before-pump"));
                if (State == EStartSetup::Failed)
                    return true;
                const FString Match = ObservedMatch;
                const bool Started = ObservedStarted;
                if (State == EStartSetup::Ready)
                {
                    // This is the exact actor associated by the completed authored
                    // initial/stage construction event, never an enumerated guess.
                    // This standalone context advances only when this fixture
                    // explicitly pumps it; native initialization ticks remain enabled.
                    Waiting = false;
                    PendingWorld.Reset();
                    PendingRoom = nullptr;
                }
                else
                {
                    const double Now = FPlatformTime::Seconds();
                    if (Now - WaitStarted >= 30.)
                    {
                        Test->AddError(
                            StartAssertion +
                            TEXT(": preparation did not complete within the 30-second fixture bound"));
                        Test->AddInfo(FString::Printf(
                            TEXT("start readiness timeout public-active-now=%d started-this-update=%d success-seen=%d "
                                 "observed-match=%s first-success-match=%s previous-match=%s original-start-tick=%lld"),
                            int32(PendingRoom->IsActive()), int32(Started), int32(PublicStarted), *Match,
                            *RecordedMatch, *Previous, RecordedStartTick));
                        const auto *FixtureContext = FRoomBattleWorld::FindContext(PendingWorld->WorldContextHandle);
                        const auto *FixtureWorld = FixtureContext ? FixtureContext->World() : nullptr;
                        Test->AddInfo(FString::Printf(
                            TEXT("start readiness timeout context=%s context-present=%d world-present=%d begun-play=%d "
                                 "next-url-pending=%d travel-url-pending=%d pending-net-game=%d seamless-transition=%d associated-actor=%d"),
                            *PendingWorld->WorldContextHandle.ToString(), int32(FixtureContext != nullptr), int32(FixtureWorld != nullptr),
                            FixtureWorld ? int32(FixtureWorld->HasBegunPlay()) : -1,
                            FixtureWorld ? int32(!FixtureWorld->NextURL.IsEmpty()) : -1,
                            FixtureContext ? int32(!FixtureContext->TravelURL.IsEmpty()) : -1,
                            FixtureContext ? int32(FixtureContext->PendingNetGame != nullptr) : -1,
                            FixtureContext ? int32(FixtureContext->SeamlessTravelHandler.IsInTransition()) : -1,
                            int32(PendingWorld->Game->AssociatedRoomFixtureBattle() != nullptr)));
                        Test->AddInfo(PendingWorld->Game->DescribeRoomFixtureSetup());
                        return true;
                    }
                    EStartSetup PumpState = EStartSetup::Pending;
                    PendingWorld->AdvanceOwnedWorld(float(Now - LastPump), [this, &PumpState]() {
                        PumpState = ObserveStartSetup(TEXT("pump-checkpoint"));
                        return PumpState != EStartSetup::Pending;
                    });
                    LastPump = Now;
                    if (PumpState == EStartSetup::Failed)
                        return true;
                    if (PumpState == EStartSetup::Ready)
                        continue;
                    return false;
                }
            }
            const bool Succeeded = Step(*this);
            if (Succeeded && RepeatStep)
            {
                RepeatStep = false;
                return false;
            }
            if (!Succeeded || !NextStep)
                return true;
            Step = MoveTemp(NextStep);
            // TFunction move assignment swaps bindings; consume the prior phase.
            NextStep = FStep();
        }
    }

  private:
    enum class EStartSetup { Pending, Ready, Failed };

    bool ObserveSuccessfulStart(const TCHAR *Phase)
    {
        ObservedMatch = PendingRoom->Observe(PendingFighter).Match;
        const bool Active = PendingRoom->IsActive();
        ObservedStarted = Active && !ObservedMatch.IsEmpty() && ObservedMatch != Previous;
        if (!PublicStarted && ObservedStarted)
        {
            RecordedMatch = ObservedMatch;
            Test->TestTrue(StartAssertion, ObservedStarted);
            PublicStarted = true;
            Test->AddInfo(FString::Printf(TEXT("start checkpoint phase=%s first-success=%s tick=%lld"),
                                          Phase, *RecordedMatch, RecordedStartTick));
            if (SuccessWitness && !SuccessWitness(RecordedMatch, RecordedStartTick))
                return false;
            SuccessWitness = TFunction<bool(const FString &, int64)>();
        }
        if (PublicStarted &&
            !Test->TestEqual(TEXT("live setup retains the first successful match identity"),
                             ObservedMatch, RecordedMatch))
            return false;
        if (PublicStarted && !Active)
        {
            // This scenario has not submitted an input, departure or recovery yet.
            // Preserve the active-match requirement; report the unexpected transition directly.
            Test->AddError(StartAssertion + TEXT(": successful match became inactive before manual scenario control"));
            Test->AddInfo(FString::Printf(TEXT("start checkpoint phase=%s match=%s tick=%lld"),
                                          Phase, *ObservedMatch, RecordedStartTick));
            Test->AddInfo(PendingWorld->Game->DescribeRoomFixtureSetup());
            return false;
        }
        return true;
    }

    EStartSetup ObserveStartSetup(const TCHAR *Phase)
    {
        // Binding and observation are public calls. Resample success after binding,
        // then reacquire the exact fixture-owned actor/world before testing readiness.
        if (!ObserveSuccessfulStart(Phase))
            return EStartSetup::Failed;
        PendingWorld->Game->CompleteRoomFixtureBinding();
        if (!ObserveSuccessfulStart(Phase))
            return EStartSetup::Failed;
        PendingWorld->World = PendingWorld->Game->GetWorld();
        PendingWorld->Battle = PendingWorld->Game->AssociatedRoomFixtureBattle();
        return PublicStarted && ObservedStarted && !PendingWorld->HasPendingTravel() && PendingWorld->Battle &&
                       PendingWorld->Battle->GetMainPlayer(true) && PendingWorld->Battle->GetMainPlayer(false) &&
                       CastChecked<AReplayFixtureBattle>(PendingWorld->Battle)->DidCompleteFixtureInitialization()
                   ? EStartSetup::Ready : EStartSetup::Pending;
    }

    FAutomationTestBase *Test;
    FStep Step, NextStep;
    TSharedPtr<FRoomBattleWorld> PendingWorld;
    USpectatorRoom *PendingRoom = nullptr;
    FString PendingFighter, Previous, StartAssertion, ObservedMatch;
    bool ObservedStarted = false;
    bool Waiting = false, PublicStarted = false, RepeatStep = false;
    TFunction<bool(const FString &, int64)> SuccessWitness;
    FString RecordedMatch;
    int64 RecordedStartTick = 0;
    double WaitStarted = 0., LastPump = 0.;
};

// These owners and sound observers span every pending presentation phase.
class FRoomPresentationSequence : public TSharedFromThis<FRoomPresentationSequence>
{
public:
    FRoomPresentationSequence(FAutomationTestBase &InTest, USpectatorRoom *InRoom,
                             TSharedRef<FRoomBattleWorld> InAuthority, TSharedRef<FRoomBattleWorld> InControl,
                             const FBattleData &InConfig, const FRoomDelivery &InDelivery,
                             const FString &InMatch, int32 InInitialX, int32 InInitialHealth, bool InRender,
                             FRoomScenarioCommand::FStep InComplete)
        : Test(InTest), Room(InRoom), AuthorityOwner(InAuthority), ControlOwner(InControl),
          Config(InConfig), Delivery(InDelivery), Match(InMatch), InitialX(InInitialX), InitialHealth(InInitialHealth), Render(InRender),
          Complete(MoveTemp(InComplete))
    {
        Presentation = AutomaticViewer->Game->GetSubsystem<USpectatorRoom>();
        FighterX = AuthorityOwner->Battle->GetMainPlayer(true)->PosX;
    }
    ~FRoomPresentationSequence()
    {
        AReplayFixtureBattle::SoundStarted.Remove(ZeroAudio);
        AReplayFixtureBattle::SoundStarted.Remove(AudioObserver);
    }
    bool Begin(FRoomScenarioCommand &Scenario)
    {
        Await(Scenario, Delivery, TEXT("released delivery automatically loads matching stage and actors"),
              &FRoomPresentationSequence::Released);
        return true;
    }
private:
    using FPhase = bool (FRoomPresentationSequence::*)(FRoomScenarioCommand &);
    void Await(FRoomScenarioCommand &Scenario, const FRoomDelivery &Next, const TCHAR *Label, FPhase Phase)
    {
        // Copy independent native observations before dispatch. GameInstance setup
        // data can be consumed or cleared by a valid initialized presentation.
        const auto *Reference = OrdinaryOwner ? OrdinaryOwner->Battle : AuthorityOwner->Battle;
        const int32 ExpectedX = Next.Frame == 0 ? InitialX : Reference->GetMainPlayer(true)->PosX;
        const int32 ExpectedHealth = Next.Frame == 0 ? InitialHealth : Reference->GetMainPlayer(false)->CurrentHealth;
        const int32 ExpectedMaxHealth = Reference->GetMainPlayer(true)->MaxHealth;
        Scenario.AwaitPresentation(Presentation, AutomaticViewer, Next,
            [=](ANightSkyGameState *Battle) {
                return Battle->GetMainPlayer(true)->PosX == ExpectedX &&
                       Battle->GetMainPlayer(false)->CurrentHealth == ExpectedHealth &&
                       Battle->GetMainPlayer(true)->MaxHealth == ExpectedMaxHealth;
            }, Render, Deadline, Label,
            [Self = AsShared(), Phase](FRoomScenarioCommand &NextScenario) { return ((*Self).*Phase)(NextScenario); });
    }
    bool Released(FRoomScenarioCommand &Scenario)
    {
        auto &Authority = *AuthorityOwner;
        auto *Presented = Presentation->PresentationBattle();
        if (!Test.TestTrue(
                TEXT("loading playback preserves live authority actors"),
                IsValid(Authority.Battle)))
            return false;
        Test.TestEqual(TEXT("loading historical playback leaves authority at its "
                            "confirmed frame"),
                       Authority.Battle->BattleState.FrameNumber, 240);
        Test.TestEqual(TEXT("loading historical playback leaves authority "
                            "position unchanged"),
                       Authority.Battle->GetMainPlayer(true)->PosX, FighterX);
        Test.TestEqual(TEXT("automatically loaded gameplay reproduces health"),
                       Presented->GetMainPlayer(false)->CurrentHealth,
                       Authority.Battle->GetMainPlayer(false)->CurrentHealth);
        Test.TestEqual(TEXT("automatically loaded gameplay reproduces position"),
                       Presented->GetMainPlayer(true)->PosX, FighterX);

        auto Zero = Operation(TEXT("seek"), 0);
        Zero.Match = Match;
        Room->Command(TEXT("viewer"), Zero);
        const auto AtZero = Room->Command(TEXT("viewer"), Operation(TEXT("pause")));
        ZeroAudio = AReplayFixtureBattle::SoundStarted.AddLambda([this](const AReplayFixtureBattle *Battle) {
            if (Battle != AuthorityOwner->Battle && Battle != ControlOwner->Battle)
                ++ZeroSeekSounds;
        });
        Await(Scenario, AtZero, TEXT("automatic playback seeks prejoin frame zero"), &FRoomPresentationSequence::AtZero);
        return true;
    }
    bool AtZero(FRoomScenarioCommand &Scenario)
    {
        if (Render)
        {
            Fence.BeginFence();
            Fence.Wait();
            Test.TestEqual(TEXT("backward seek to frame zero emits no historical sounds"), ZeroSeekSounds, 0);
        }
        AReplayFixtureBattle::SoundStarted.Remove(ZeroAudio);
        ZeroAudio.Reset();
        auto *Presented = Presentation->PresentationBattle();
        if (!Test.TestNotNull(
                TEXT("frame zero matching battle remains available"), Presented))
            return false;
        Test.TestEqual(TEXT("seek restores initialized position"),
                       Presented->GetMainPlayer(true)->PosX, InitialX);

        if (!Render)
            return Finish(Scenario);
        OrdinaryOwner = MakeShared<FRoomBattleWorld>();
        auto &Ordinary = *OrdinaryOwner;
        Ordinary.Game->BattleData = Config;
        Ordinary.Begin();
        AudioObserver = AReplayFixtureBattle::SoundStarted.AddLambda([this](const AReplayFixtureBattle *Battle) {
            if (Battle == OrdinaryOwner->Battle)
                ++OrdinarySounds;
            else if (Battle != AuthorityOwner->Battle && Battle != ControlOwner->Battle)
                ++SkippedSounds;
        });
        for (int32 Frame = 1; Frame <= 15; ++Frame)
        {
            Ordinary.Battle->UpdateGameState(
                INP_Right | (Frame % 20 == 0 ? INP_A : 0) |
                    ((Frame >= 1 && Frame <= 8) || Frame == 30 ||
                             (Frame >= 150 && Frame <= 156)
                         ? INP_B
                         : 0),
                0, false);
            if (Frame == 1 || Frame == 8 || Frame == 15)
            {
                int32 Active = 0;
                for (const auto *Object : Ordinary.Battle->Objects)
                    Active += Object->IsActive ? 1 : 0;
                Test.AddInfo(FString::Printf(
                    TEXT("Ordinary render fixture frame %d: active "
                         "projectiles %d, launch cooldown %d"),
                    Frame, Active,
                    Ordinary.Battle->GetMainPlayer(true)->ObjectReg2));
            }
        }

        auto At = Operation(TEXT("seek"), 15);
        At.Match = Match;
        Room->Command(TEXT("viewer"), At);
        Destination = Room->Command(TEXT("viewer"), Operation(TEXT("pause")));
        Await(Scenario, Destination, TEXT("public DVR seek restores rendered destination"), &FRoomPresentationSequence::AtDestination);
        return true;
    }
    bool AtDestination(FRoomScenarioCommand &Scenario)
    {
        auto &Ordinary = *OrdinaryOwner;
        auto *Presented = Presentation->PresentationBattle();
        Fence.BeginFence();
        Fence.Wait();
        Test.TestTrue(TEXT("ordinary actual sound playback positive control"), OrdinarySounds > 0);
        Test.TestEqual(TEXT("DVR catch-up emits no skipped sounds"), SkippedSounds, 0);
        TArray<FString> OrdinaryObjects, SeekObjects;
        for (const auto *Object : Ordinary.Battle->Objects)
            if (Object->IsActive)
                OrdinaryObjects.Add(DescribeActiveObject(Object, Ordinary.Battle));
        for (const auto *Object : Presented->Objects)
            if (Object->IsActive)
                SeekObjects.Add(DescribeActiveObject(Object, Presented));
        OrdinaryObjects.Sort();
        SeekObjects.Sort();
        Test.TestTrue(TEXT("ordinary active projectile positive control"), !OrdinaryObjects.IsEmpty());
        Test.TestTrue(TEXT("seek restores active projectile gameplay and lifetime phase"), SeekObjects == OrdinaryObjects);
        Test.TestEqual(TEXT("destination fighter animation phase"),
                       Presented->GetMainPlayer(true)->ActionTime, Ordinary.Battle->GetMainPlayer(true)->ActionTime);
        Test.TestEqual(TEXT("destination fighter animation cel"),
                       Presented->GetMainPlayer(true)->GetCelName(), Ordinary.Battle->GetMainPlayer(true)->GetCelName());
        // Reference capture has its own dimensions and camera. Only visible
        // semantics are compared with the published candidate texture.
        OrdinaryCapture = Ordinary.World->SpawnActor<ASceneCapture2D>();
        if (!Test.TestNotNull(TEXT("ordinary capture exists"), OrdinaryCapture))
            return false;
        OrdinaryTarget.Reset(NewObject<UTextureRenderTarget2D>(OrdinaryCapture));
        OrdinaryTarget->InitAutoFormat(960, 540);
        auto *Component = OrdinaryCapture->GetCaptureComponent2D();
        Component->TextureTarget = OrdinaryTarget.Get();
        Component->bCaptureEveryFrame = false;
        Component->bCaptureOnMovement = false;
        Component->CaptureSource = SCS_FinalColorLDR;
        if (!CapturePair() || !Evidence.AddSample(Test, TEXT("duplicate"), Presentation->PresentationTexture()))
            return false;
        Await(Scenario, Destination, TEXT("repeated destination delivery remains valid"), &FRoomPresentationSequence::Repeated);
        return true;
    }

    bool Repeated(FRoomScenarioCommand &Scenario)
    {
        // Presentation may be structurally ready in the delivery call. Yield
        // through a real engine boundary before observing its published image.
        Scenario.YieldToEngine([Self = AsShared()](FRoomScenarioCommand &Next) { return Self->SampleDuplicate(Next); });
        return true;
    }

    bool SampleDuplicate(FRoomScenarioCommand &Scenario)
    {
        Fence.BeginFence();
        Fence.Wait();
        FlushRenderingCommands();
        Test.TestEqual(TEXT("duplicate delivery does not duplicate sounds"), SkippedSounds, 0);
        if (!Evidence.AddSample(Test, TEXT("duplicate"), Presentation->PresentationTexture()))
            return false;
        if (++DuplicateSamples < 2)
        {
            Scenario.YieldToEngine([Self = AsShared()](FRoomScenarioCommand &Next) { return Self->SampleDuplicate(Next); });
            return true;
        }
        OrdinaryBeforeResume = OrdinarySounds;
        PresentationBeforeResume = SkippedSounds;
        Room->Command(TEXT("viewer"), Operation(TEXT("resume")));
        return SubmitResume(Scenario);
    }

    bool CapturePair()
    {
        auto *Camera = OrdinaryOwner->Battle->CameraActor;
        if (!Test.TestNotNull(TEXT("ordinary reference camera exists"), Camera))
            return false;
        OrdinaryCapture->SetActorTransform(Camera->GetActorTransform());
        auto *Component = OrdinaryCapture->GetCaptureComponent2D();
        Component->FOVAngle = Camera->GetCameraComponent()->FieldOfView;
        Component->CaptureScene();
        FlushRenderingCommands();
        return Evidence.AddSample(Test, TEXT("ordinary"), OrdinaryTarget.Get()) &&
               Evidence.AddSample(Test, TEXT("presented"), Presentation->PresentationTexture());
    }

    bool SubmitResume(FRoomScenarioCommand &Scenario)
    {
        if (ResumeFrame > 100)
        {
            Test.TestTrue(TEXT("resumed presentation actually plays a newly reached attack sound"),
                          SkippedSounds > PresentationBeforeResume);
            return Finish(Scenario);
        }
        OrdinaryOwner->Battle->UpdateGameState(
            INP_Right | (ResumeFrame % 20 == 0 ? INP_A : 0) | (ResumeFrame == 30 ? INP_B : 0), 0, false);
        Room->PlaybackTick(TEXT("viewer"));
        const auto Resumed = Room->Observe(TEXT("viewer"));
        Test.TestEqual(TEXT("resumed view advances exactly one frame"), Resumed.Frame, ResumeFrame);
        Await(Scenario, Resumed, TEXT("resumed delivery continues actual gameplay"), &FRoomPresentationSequence::InspectResume);
        return true;
    }
    bool InspectResume(FRoomScenarioCommand &Scenario)
    {
        Fence.BeginFence();
        Fence.Wait();
        Test.TestEqual(TEXT("resumed gameplay emits the same new sound events as ordinary play"),
                       SkippedSounds - PresentationBeforeResume, OrdinarySounds - OrdinaryBeforeResume);
        if (ResumeFrame == 16 || ResumeFrame == 20 || ResumeFrame == 30 ||
            ResumeFrame == 31 || ResumeFrame == 40 || ResumeFrame == 100)
            if (!CapturePair())
                return false;
        ++ResumeFrame;
        return SubmitResume(Scenario);
    }

    bool Finish(FRoomScenarioCommand &Scenario)
    {
        if (Render)
        {
            Evidence.Require(TEXT("visible_gameplay"), TEXT("ordinary"));
            Evidence.Require(TEXT("visible_gameplay"), TEXT("presented"));
            Evidence.Require(TEXT("visible_effects"), TEXT("ordinary"));
            Evidence.Require(TEXT("visible_effects"), TEXT("presented"));
            Evidence.Require(TEXT("effect_progression"), TEXT("ordinary"));
            Evidence.Require(TEXT("effect_progression"), TEXT("presented"));
            Evidence.Require(TEXT("same_gameplay"), TEXT("ordinary"), TEXT("presented"));
            Evidence.Require(TEXT("same_effect_phase"), TEXT("ordinary"), TEXT("presented"));
            Evidence.Require(TEXT("stable_presentation"), TEXT("duplicate"));
            if (!Evidence.Finish(Test))
                return false;
        }
        Scenario.Continue(MoveTemp(Complete));
        return true;
    }

    FAutomationTestBase &Test;
    USpectatorRoom *Room, *Presentation;
    TSharedRef<FRoomBattleWorld> AuthorityOwner, ControlOwner;
    TSharedRef<FRoomBattleWorld> AutomaticViewer = MakeShared<FRoomBattleWorld>();
    TSharedPtr<FRoomBattleWorld> OrdinaryOwner;
    FBattleData Config;
    FRoomDelivery Delivery, Destination;
    FString Match;
    int32 InitialX, InitialHealth, FighterX = 0;
    bool Render;
    FRoomScenarioCommand::FStep Complete;
    const double Deadline = FPlatformTime::Seconds() + 30.;
    FDelegateHandle ZeroAudio, AudioObserver;
    FAudioCommandFence Fence;
    int32 ZeroSeekSounds = 0, OrdinarySounds = 0, SkippedSounds = 0;
    int32 OrdinaryBeforeResume = 0, PresentationBeforeResume = 0, ResumeFrame = 16;
    FRoomRenderedEvidence Evidence;
    ASceneCapture2D *OrdinaryCapture = nullptr;
    TStrongObjectPtr<UTextureRenderTarget2D> OrdinaryTarget;
    int32 DuplicateSamples = 0;
};

template <class T> T *FixtureAsset(const FString &Name)
{
    const FString PackageName = TEXT("/Game/RoomFixture/") + Name;
    auto *Package = CreatePackage(*PackageName);
    auto *Asset = NewObject<T>(Package, *Name, RF_Public | RF_Standalone);
    return Asset;
}

bool SaveFixture(UObject *Asset)
{
    const FString File = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),
                                                                 FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    return UPackage::SavePackage(Asset->GetOutermost(), Asset, *File, Args);
}
} // namespace

bool RunRoomBattleDelay(FAutomationTestBase &Test, bool ColdOnly, bool Render = false)
{
    for (int32 Delay : TArray<int32>{0, 1, 120, 600})
        {
            // Authority-crash timing is exercised by the real process-death scenario.
            if (ColdOnly && Delay != 0)
                continue;
            ADD_LATENT_AUTOMATION_COMMAND(FRoomScenarioCommand(&Test, [&Test, ColdOnly, Render, Delay](
                                                                          FRoomScenarioCommand &Scenario) {
                Test.AddInfo(
                    FString::Printf(TEXT("delay=%d fighter-departure"), Delay));
                auto AuthorityOwner = MakeShared<FRoomBattleWorld>();
                auto &Authority = *AuthorityOwner;
                Authority.Game->BattleData.Random.Reseed(9009);
                FBattleData Config = Authority.Game->BattleData;
                auto *Room = Authority.Game->GetSubsystem<USpectatorRoom>();
                auto Clock = MakeShared<int64>(0);
                Room->SetClock([Clock]() { return *Clock; });
                const FString AuthoritySlot = Slot();
                if (!Test.TestTrue(TEXT("create authority"),
                                   Room->Create(AuthoritySlot, TEXT("host"), TEXT("secret"), Delay)))
                    return false;
                auto *Chara = FixtureAsset<UPrimaryCharaData>(
                    TEXT("Character") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
                Chara->PlayerClass = AReplayFixtureFighter::StaticClass();
                auto *Icon =
                    FixtureAsset<UTexture2D>(TEXT("Icon") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
                Test.TestTrue(TEXT("save referenced icon package"), SaveFixture(Icon));
                Chara->CharaHUDIcon = Icon;
                Test.TestTrue(TEXT("save real character package"), SaveFixture(Chara));
                auto *Stage = FixtureAsset<UPrimaryStageData>(
                    TEXT("Stage") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
                Stage->StageURL = TEXT("/Engine/Maps/Entry?game=/Script/NightSkyEngine.ReplayFixtureRoomGameMode");
                Test.TestTrue(TEXT("save stage package"), SaveFixture(Stage));
                Config.PlayerListP1 = {Chara};
                Config.PlayerListP2 = {Chara};
                Config.Stage = Stage;
                Authority.OwnConfiguration(Config);
                FString Assignment[2];
                for (int32 Seat = 0; Seat < 2; ++Seat)
                {
                    FString Id = FString::Printf(TEXT("fighter%d"), Seat);
                    Room->Authenticate(Id, TEXT("secret"), {});
                    Room->Command(Id, Operation(TEXT("queue")));
                    Room->Command(TEXT("host"), Operation(TEXT("offer"), Seat, Id));
                    auto Accept = Operation(TEXT("accept"));
                    Accept.Assignment = Room->Observe(Id).Offer;
                    Assignment[Seat] = Room->Command(Id, Accept).Assignment;
                    auto Select = Operation(TEXT("character"), 0, Chara->GetPathName());
                    Select.Assignment = Assignment[Seat];
                    Test.TestTrue(TEXT("character accepted"),
                                  CheckRoomCommand(Test, Room, Id, Select, TEXT("accepted")));
                }
                Test.TestTrue(TEXT("stage accepted"),
                              CheckRoomCommand(Test, Room, TEXT("host"),
                                               Operation(TEXT("stage"), 0, Stage->GetPathName()),
                                               TEXT("accepted")));
                auto Content = USpectatorRoom::InspectContent(Config, Authority.Game->BattleVersion);
                Test.TestFalse(TEXT("content inspection reports required revisions"), Content.IsEmpty());
                const FString IconFile = FPackageName::LongPackageNameToFilename(
                    Icon->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
                TArray<uint8> OriginalIcon;
                Test.TestTrue(TEXT("read original dependency artifact"),
                              FFileHelper::LoadFileToArray(OriginalIcon, *IconFile));
                Icon->LODGroup = TEXTUREGROUP_UI;
                Test.TestTrue(TEXT("change actual dependency artifact"), SaveFixture(Icon));
                const auto Changed = USpectatorRoom::InspectContent(Config, Authority.Game->BattleVersion);
                Test.TestFalse(TEXT("referenced content change alters required revisions"),
                               Changed.OrderIndependentCompareEqual(Content));
                Room->Authenticate(TEXT("fighter0"), TEXT("secret"), Changed);
                Assignment[0] = Room->Observe(TEXT("fighter0")).Assignment;
                auto DependencyReady = Operation(TEXT("ready"));
                DependencyReady.Assignment = Assignment[0];
                Test.TestTrue(TEXT("changed referenced package blocks fighter readiness"),
                              CheckRoomCommand(Test, Room, TEXT("fighter0"), DependencyReady,
                                               TEXT("content unavailable")));
                Test.TestTrue(TEXT("restore actual dependency package"),
                              FFileHelper::SaveArrayToFile(OriginalIcon, *IconFile));
                for (const auto &Required : Content)
                    for (bool Missing : {false, true})
                    {
                        auto Broken = Content;
                        if (Missing)
                            Broken.Remove(Required.Key);
                        else
                            Broken[Required.Key] = TEXT("different-revision");
                        Room->Authenticate(TEXT("fighter0"), TEXT("secret"), Broken);
                        Assignment[0] = Room->Observe(TEXT("fighter0")).Assignment;
                        auto Ready = Operation(TEXT("ready"));
                        Ready.Assignment = Assignment[0];
                        Test.TestTrue(TEXT("invalid fighter content blocks ready"),
                                      CheckRoomCommand(Test, Room, TEXT("fighter0"), Ready,
                                                       TEXT("content unavailable")));
                        Test.TestTrue(TEXT("invalid content blocks start"),
                                      CheckRoomCommand(Test, Room, TEXT("host"), Operation(TEXT("start")),
                                                       TEXT("fighters not ready")));
                        Test.TestTrue(TEXT("organizer controls remain healthy"),
                                      CheckRoomCommand(Test, Room, TEXT("host"), Operation(TEXT("lock")),
                                                       TEXT("accepted")));
                        Room->Command(TEXT("host"), Operation(TEXT("unlock")));
                    }
                for (int32 Seat = 0; Seat < 2; ++Seat)
                {
                    const FString Id = FString::Printf(TEXT("fighter%d"), Seat);
                    Room->Authenticate(Id, TEXT("secret"), Content);
                    Assignment[Seat] = Room->Observe(Id).Assignment;
                    auto Ready = Operation(TEXT("ready"));
                    Ready.Assignment = Assignment[Seat];
                    if (!Test.TestTrue(TEXT("healthy fighter ready"),
                                       CheckRoomCommand(Test, Room, Id, Ready, TEXT("accepted"))))
                        return false;
                }
                Authority.Game->BattleData = Config;
                Authority.Game->IsTraining = true;
                Authority.Begin();
                Authority.AssociateRoomBattle();
                if (!Test.TestFalse(TEXT("native setup remains inactive before actual start dispatch"),
                                    Room->IsActive()))
                    return false;
                Room->Command(TEXT("host"), Operation(TEXT("start")));
                Scenario.AwaitStart(
                    Room, AuthorityOwner, TEXT("fighter0"), TEXT(""), *Clock,
                    TEXT("start establishes initialized active match"),
                    [=, &Test](FRoomScenarioCommand &Scenario) mutable {
                        auto &Authority = *AuthorityOwner;
                        auto &Tick = *Clock;
                        if (!RefreshCurrentFighterAssignments(Test, Room, TEXT("fighter0"), TEXT("fighter1"),
                                                              Assignment[0], Assignment[1]))
                            return false;
                        struct FActiveFrames
                        {
                            TSharedRef<FRoomBattleWorld> Control = MakeShared<FRoomBattleWorld>();
                            FRandomStream SpectatorRandom;
                            int32 Cursor[2] = {-1, -1}, Mode[2] = {0, 0};
                            FRoomDelivery Delivery;
                            FString Match;
                            int32 InitialX = 0, InitialHealth = 0, InputBits = 0;
                            TArray<TSharedPtr<FJsonValue>> OfflineFrames;
                            bool SawMovement = false, SawDamage = false;
                            explicit FActiveFrames(int32 Delay) : SpectatorRandom(24001 + Delay)
                            {
                            }
                        };
                        auto FrameSequenceState = MakeShared<FActiveFrames>(Delay);
                        FrameSequenceState->Control->Game->BattleData = Config;
                        FrameSequenceState->Control->Begin();
                        FrameSequenceState->Delivery = Room->Observe(TEXT("viewer"));
                        if (!Test.TestEqual(TEXT("live setup preserves original room clock"), Tick,
                                            Scenario.SuccessfulStartTick()))
                            return false;
                        Tick += Delay;
                        FrameSequenceState->Delivery = Room->Observe(TEXT("viewer"));
                        auto &Delivery = FrameSequenceState->Delivery;
                        Test.TestEqual(TEXT("zero releases"), Delivery.Frame, 0);
                        FrameSequenceState->Match = Scenario.SuccessfulMatch();
                        if (!Test.TestEqual(TEXT("frame-zero release retains successful match identity"),
                                            Delivery.Match, FrameSequenceState->Match))
                            return false;
                        FrameSequenceState->InitialX = Authority.Battle->GetMainPlayer(true)->PosX;
                        FrameSequenceState->InitialHealth = Authority.Battle->GetMainPlayer(false)->CurrentHealth;
                        Scenario.RunFrames(
                            Room, AuthorityOwner, 240,
                            [=, &Test](int32 Frame) mutable {
                                const FString &Match = FrameSequenceState->Match;
                                if (!RefreshCurrentFighterAssignments(Test, Room, TEXT("fighter0"),
                                                                      TEXT("fighter1"), Assignment[0],
                                                                      Assignment[1]))
                                    return false;

                                const int32 InputBits =
                                    INP_Right | (Frame % 20 == 0 ? INP_A : 0) |
                                    (ColdOnly && ((Frame >= 1 && Frame <= 8) || Frame == 30 ||
                                                  (Frame >= 150 && Frame <= 156))
                                         ? INP_B
                                         : 0);
                                FrameSequenceState->InputBits = InputBits;
                                for (int32 Seat = 0; Seat < 2; ++Seat)
                                {
                                    auto Input = Operation(TEXT("input"), Frame,
                                                           FString::FromInt(Seat == 0 ? InputBits : 0));
                                    Input.Match = Match;
                                    Input.Assignment = Assignment[Seat];
                                    if (!Test.TestTrue(
                                            TEXT("current owner input accepted"),
                                            CheckRoomCommand(Test, Room,
                                                             FString::Printf(TEXT("fighter%d"), Seat), Input,
                                                             TEXT("accepted"))))
                                        return false;
                                }
                                if (Frame % 17 == 0)
                                {
                                    auto Attack = Operation(TEXT("input"), Frame, FString::FromInt(INP_A));
                                    Attack.Match = Match;
                                    Attack.Assignment = Assignment[0];
                                    Test.TestTrue(TEXT("spectator cannot inject even with known assignment"),
                                                  CheckRoomCommand(Test, Room, TEXT("viewer"), Attack,
                                                                   TEXT("input rejected")));
                                    Attack.Match = TEXT("stale-match");
                                    Test.TestTrue(TEXT("stale match input rejected"),
                                                  CheckRoomCommand(Test, Room, TEXT("fighter0"), Attack,
                                                                   TEXT("input rejected")));
                                }

                                return true;
                            },
                            [=, &Test](int32 Frame) mutable {
                                auto &Authority = *AuthorityOwner;
                                auto &Tick = *Clock;
                                auto &Control = *FrameSequenceState->Control;
                                auto &SpectatorRandom = FrameSequenceState->SpectatorRandom;
                                auto &Cursor = FrameSequenceState->Cursor;
                                auto &Mode = FrameSequenceState->Mode;
                                auto &Delivery = FrameSequenceState->Delivery;
                                const auto &Match = FrameSequenceState->Match;
                                const int32 InitialX = FrameSequenceState->InitialX,
                                            InitialHealth = FrameSequenceState->InitialHealth;
                                const int32 InputBits = FrameSequenceState->InputBits;
                                auto &OfflineFrames = FrameSequenceState->OfflineFrames;
                                auto &SawMovement = FrameSequenceState->SawMovement;
                                auto &SawDamage = FrameSequenceState->SawDamage;

                                Control.Battle->UpdateGameState(InputBits, 0, false);
                                if (ColdOnly)
                                {
                                    auto Row = MakeShared<FJsonObject>();
                                    Row->SetNumberField(TEXT("frame"), Frame);
                                    Row->SetNumberField(TEXT("x"),
                                                        Authority.Battle->GetMainPlayer(true)->PosX);
                                    Row->SetNumberField(
                                        TEXT("health"),
                                        Authority.Battle->GetMainPlayer(false)->CurrentHealth);
                                    auto Semantic = MakeShared<FJsonObject>();
                                    Semantic->SetNumberField(TEXT("frame"), Frame);
                                    Semantic->SetNumberField(TEXT("timer"),
                                                             Authority.Battle->BattleState.RoundTimer);
                                    TArray<TSharedPtr<FJsonValue>> SemanticPlayers, SemanticObjects;
                                    for (int32 Side = 0; Side < 2; ++Side)
                                    {
                                        const auto *Player = Authority.Battle->GetMainPlayer(Side == 0);
                                        const auto *Expected = Control.Battle->GetMainPlayer(Side == 0);
                                        auto P = MakeShared<FJsonObject>();
                                        P->SetNumberField(TEXT("x"), Player->PosX);
                                        P->SetNumberField(TEXT("y"), Player->PosY);
                                        P->SetNumberField(TEXT("vx"), Player->SpeedX);
                                        P->SetNumberField(TEXT("vy"), Player->SpeedY);
                                        P->SetNumberField(TEXT("health"), Player->CurrentHealth);
                                        P->SetNumberField(TEXT("meter"),
                                                          Authority.Battle->BattleState.Meter[Side]);
                                        P->SetNumberField(TEXT("inputs"), Player->Inputs);
                                        P->SetNumberField(TEXT("phase"), Player->ActionTime);
                                        P->SetStringField(TEXT("cel"), Player->GetCelName().ToString());
                                        SemanticPlayers.Add(MakeShared<FJsonValueObject>(P));
                                        Test.TestTrue(TEXT("both original fighters agree with independent "
                                                           "control before "
                                                           "cold ledger acceptance"),
                                                      Player->PosX == Expected->PosX &&
                                                          Player->PosY == Expected->PosY &&
                                                          Player->SpeedX == Expected->SpeedX &&
                                                          Player->SpeedY == Expected->SpeedY &&
                                                          Player->CurrentHealth == Expected->CurrentHealth &&
                                                          Player->Inputs == Expected->Inputs &&
                                                          Player->ActionTime == Expected->ActionTime &&
                                                          Player->GetCelName() == Expected->GetCelName());
                                    }
                                    TArray<FString> ExpectedObjects, RecordedObjects;
                                    for (const auto *Object : Control.Battle->Objects)
                                        if (Object->IsActive)
                                            ExpectedObjects.Add(DescribeActiveObject(Object, Control.Battle));
                                    for (const auto *Object : Authority.Battle->Objects)
                                        if (Object->IsActive)
                                            RecordedObjects.Add(
                                                DescribeActiveObject(Object, Authority.Battle));
                                    ExpectedObjects.Sort();
                                    RecordedObjects.Sort();
                                    Test.TestEqual(
                                        TEXT("cold recorder has the independent active projectile count"),
                                        RecordedObjects.Num(), ExpectedObjects.Num());
                                    Test.TestTrue(
                                        TEXT("cold recorder active projectile lifetime matches control"),
                                        RecordedObjects == ExpectedObjects);
                                    Test.TestTrue(
                                        TEXT("cold recorder projectile phase matches independent control"),
                                        RecordedObjects == ExpectedObjects);
                                    for (const FString &Description : RecordedObjects)
                                        SemanticObjects.Add(MakeShared<FJsonValueString>(Description));
                                    Semantic->SetArrayField(TEXT("players"), SemanticPlayers);
                                    Semantic->SetArrayField(TEXT("objects"), SemanticObjects);
                                    Row->SetObjectField(TEXT("battle_state"), Semantic);
                                    Row->SetNumberField(TEXT("p2x"),
                                                        Authority.Battle->GetMainPlayer(false)->PosX);
                                    Row->SetNumberField(TEXT("p1health"),
                                                        Authority.Battle->GetMainPlayer(true)->CurrentHealth);
                                    Row->SetNumberField(TEXT("input1"),
                                                        Authority.Battle->GetMainPlayer(true)->Inputs);
                                    Row->SetNumberField(TEXT("input2"),
                                                        Authority.Battle->GetMainPlayer(false)->Inputs);
                                    Row->SetNumberField(TEXT("timer"),
                                                        Authority.Battle->BattleState.RoundTimer);
                                    Row->SetNumberField(
                                        TEXT("rng"), Authority.Battle->BattleState.RandomManager.GetSeed());
                                    Row->SetNumberField(TEXT("meter1"),
                                                        Authority.Battle->BattleState.Meter[0]);
                                    Row->SetNumberField(TEXT("meter2"),
                                                        Authority.Battle->BattleState.Meter[1]);
                                    Row->SetNumberField(TEXT("phase"),
                                                        int32(Authority.Battle->BattleState.BattlePhase));
                                    OfflineFrames.Add(MakeShared<FJsonValueObject>(Row));
                                }
                                SawMovement |= Authority.Battle->GetMainPlayer(true)->PosX != InitialX;
                                SawDamage |=
                                    Authority.Battle->GetMainPlayer(false)->CurrentHealth < InitialHealth;
                                if (Frame == 96)
                                {
                                    Test.TestTrue(TEXT("prejoin actual movement occurred"), SawMovement);
                                    Test.TestTrue(TEXT("prejoin actual damage occurred"), SawDamage);
                                }
                                Test.TestEqual(TEXT("spectator schedule preserves fighter position"),
                                               Authority.Battle->GetMainPlayer(true)->PosX,
                                               Control.Battle->GetMainPlayer(true)->PosX);
                                Test.TestEqual(TEXT("spectator schedule preserves health"),
                                               Authority.Battle->GetMainPlayer(false)->CurrentHealth,
                                               Control.Battle->GetMainPlayer(false)->CurrentHealth);
                                const auto Before = Room->Observe(TEXT("viewer"));
                                Test.TestEqual(TEXT("new confirmation withheld"), Before.Edge,
                                               Delay == 0 ? Frame : Frame - 1);
                                Tick += Delay;
                                Delivery = Room->Observe(TEXT("viewer"));
                                Test.TestEqual(TEXT("release at exact tick"), Delivery.Edge, Frame);
                                Test.TestEqual(TEXT("released input pair"), Delivery.Gameplay.Input1,
                                               InputBits);
                                for (int32 ViewerIndex = 0; ViewerIndex < 2; ++ViewerIndex)
                                {
                                    const int32 JoinFrame = ViewerIndex == 0 ? 32 : 96;
                                    if (Frame < JoinFrame)
                                        continue;
                                    const FString Id = FString::Printf(TEXT("independent%d"), ViewerIndex);
                                    if (Frame == JoinFrame)
                                    {
                                        Room->Authenticate(Id, TEXT("secret"), Content);
                                        Cursor[ViewerIndex] = Frame;
                                    }
                                    const int32 Action = SpectatorRandom.RandRange(0, 4);
                                    if (Mode[ViewerIndex] == 0)
                                        Cursor[ViewerIndex] = Frame;
                                    if (Action == 0)
                                    {
                                        auto Request =
                                            Operation(TEXT("seek"), SpectatorRandom.RandRange(0, Frame));
                                        Request.Match = Match;
                                        const auto Sought = Room->Command(Id, Request);
                                        Test.TestEqual(TEXT("seek receipt selects requested native frame"),
                                                       Sought.Frame, Request.Number);
                                        Room->Command(Id, Operation(TEXT("pause")));
                                        Cursor[ViewerIndex] = Request.Number;
                                        Mode[ViewerIndex] = 1;
                                    }
                                    if (Action == 1)
                                    {
                                        Room->Command(Id, Operation(TEXT("pause")));
                                        Mode[ViewerIndex] = 1;
                                    }
                                    if (Action == 2)
                                    {
                                        Room->Command(Id, Operation(TEXT("resume")));
                                        Mode[ViewerIndex] = 2;
                                    }
                                    if (Action == 3)
                                    {
                                        Room->Command(Id, Operation(TEXT("live")));
                                        Mode[ViewerIndex] = 0;
                                        Cursor[ViewerIndex] = Frame;
                                    }
                                    Room->PlaybackTick(Id);
                                    if (Mode[ViewerIndex] == 2)
                                        Cursor[ViewerIndex] = FMath::Min(Cursor[ViewerIndex] + 1, Frame);
                                    Test.TestEqual(TEXT("independent seeded viewer cursor"),
                                                   Room->Observe(Id).Frame, Cursor[ViewerIndex]);
                                }

                                return true;
                            },
                            [=, &Test](FRoomScenarioCommand &AfterFrames) mutable {
                                FRoomScenarioCommand::FStep Completion = [=, &Test](FRoomScenarioCommand &) mutable {
                                    auto &Authority = *AuthorityOwner;
                                    auto &Tick = *Clock;
                                    auto &Delivery = FrameSequenceState->Delivery;
                                    const auto &Match = FrameSequenceState->Match;
                                    auto &OfflineFrames = FrameSequenceState->OfflineFrames;
                                    FString FixturePath;

                                    const int32 FighterX = Authority.Battle->GetMainPlayer(true)->PosX;
                                    if (ColdOnly)
                                    {
                                        Room->Command(TEXT("viewer"), Operation(TEXT("live")));
                                        Delivery = Room->Observe(TEXT("viewer"));
                                    }
                                    FRoomBattleWorld Viewer;
                                    Viewer.Game->BattleData = Config;
                                    Viewer.Begin();
                                    Test.TestTrue(TEXT("apply real released rollback state"),
                                                  USpectatorRoom::PlayFrame(Viewer.Battle, Delivery.Gameplay));
                                    Test.TestEqual(TEXT("separate world gameplay position"),
                                                   Viewer.Battle->GetMainPlayer(true)->PosX, FighterX);
                                    Test.TestEqual(TEXT("separate world health"),
                                                   Viewer.Battle->GetMainPlayer(false)->CurrentHealth,
                                                   Authority.Battle->GetMainPlayer(false)->CurrentHealth);
                                    auto Seek = Operation(TEXT("seek"), 0);
                                    Seek.Match = Match;
                                    Delivery = Room->Command(TEXT("viewer"), Seek);
                                    Test.TestEqual(TEXT("prejoin frame zero retained"), Delivery.Frame, 0);
                                    auto Corrupt = Delivery.Gameplay;
                                    if (Corrupt.State.Num())
                                        Corrupt.State[0] ^= 1;
                                    Test.TestFalse(TEXT("corrupt received state rejected"),
                                                   USpectatorRoom::PlayFrame(Viewer.Battle, Corrupt));
                                    auto Pause = Operation(TEXT("combat-pause"));
                                    Room->Command(TEXT("host"), Pause);
                                    Tick += 1000;
                                Room->Depart(TEXT("fighter0"));
                                    auto Export = Operation(TEXT("export"));
                                    Export.Match = Match;
                                    if (Delay > 0)
                                    {
                                        Tick += Delay - 1;
                                        Test.TestTrue(TEXT("fresh interruption outcome delayed"),
                                                      CheckRoomCommand(Test, Room, TEXT("viewer"), Export,
                                                                       TEXT("pending")));
                                    }
                                    Test.TestEqual(TEXT("old frame remains available"),
                                                   Room->Observe(TEXT("viewer")).Frame, 0);
                                    if (Delay > 0)
                                        ++Tick;
                                    auto Complete = Room->Command(TEXT("viewer"), Export);
                                    Test.TestTrue(TEXT("interrupted export releases"),
                                                  RoomResponseHasEffect(Complete, TEXT("exported")));
                                    FRoomMatch Replay;
                                    Test.TestTrue(TEXT("read complete artifact"),
                                                  USpectatorRoom::ReadReplay(Complete.Replay, Replay));
                                    Test.TestEqual(TEXT("initial random seed survives artifact decoding"),
                                                   Replay.Configuration.Random.GetSeed(), uint32(9009));
                                    Test.TestEqual(TEXT("complete prejoin history"), Replay.Frames.Num(), 241);
                                    if (!CheckReplayPublicFrames(Test, Replay))
                                        return false;
                                    Test.TestTrue(TEXT("interrupted outcome"),
                                                  !Replay.Outcome.TrimStartAndEnd().IsEmpty());
                                    if (ColdOnly)
                                    {
                                        // This standalone fixture has no local-player login to
                                        // recreate its initially spawned controllers after travel.
                                        // Author a recipient in the completed current world, using
                                        // the normal controller/component construction lifecycle.
                                        auto *RecipientWorld = Authority.Game->GetWorld();
                                        auto *Controller = RecipientWorld
                                            ? RecipientWorld->SpawnActor<ANightSkyPlayerController>()
                                            : nullptr;
                                        ON_SCOPE_EXIT
                                        {
                                            if (IsValid(Controller))
                                                Controller->Destroy();
                                        };
                                        auto *Connection =
                                            Controller ? Controller->FindComponentByClass<URoomConnection>()
                                                       : nullptr;
                                        if (!Test.TestNotNull(TEXT("normal local export recipient"), Connection))
                                            return false;
                                        const auto BeforeSavedIds = Room->SavedReplays();
                                        Connection->Deliver(Complete);
                                        FString SavedIdentity;
                                        if (!Test.TestTrue(TEXT("export durably saved through public delivery"),
                                                           RoomReplayTestCatalog::Record(Room, Complete.Replay,
                                                                                         BeforeSavedIds,
                                                                                         &SavedIdentity)))
                                            return false;
                                        FixturePath = FPaths::ConvertRelativePathToFull(
                                            FPaths::ProjectSavedDir() / TEXT("Automation/RoomOfflineFixtures") /
                                            (Replay.Id + TEXT(".json")));
                                        IFileManager::Get().MakeDirectory(*FPaths::GetPath(FixturePath), true);
                                        auto Fixture = MakeShared<FJsonObject>();
                                        Fixture->SetStringField(TEXT("match"), Replay.Id);
                                        Fixture->SetStringField(TEXT("outcome"), Replay.Outcome);
                                        Fixture->SetNumberField(TEXT("recorder_pid"),
                                                                FPlatformProcess::GetCurrentProcessId());
                                        Fixture->SetNumberField(TEXT("seed"),
                                                                Replay.Configuration.Random.GetSeed());
                                        Fixture->SetArrayField(TEXT("frames"), OfflineFrames);
                                        FString Json;
                                        FJsonSerializer::Serialize(Fixture, TJsonWriterFactory<>::Create(&Json));
                                        if (!Test.TestTrue(TEXT("independent native frame ledger saved"),
                                                           FFileHelper::SaveStringToFile(Json, *FixturePath)))
                                            return false;
                                    }
                                    auto Truncated = Complete.Replay;
                                    if (Truncated.Num())
                                        Truncated.Pop();
                                    Test.TestFalse(TEXT("truncated artifact rejected"),
                                                   USpectatorRoom::ReadReplay(Truncated, Replay));
                                    if (ColdOnly && !Render)
                                    {
                                        const FString Arguments =
                                            FString::Printf(TEXT("--fixture \"%s\""), *FixturePath);
                                        ADD_LATENT_AUTOMATION_COMMAND(FRoomPythonCommand(
                                            &Test, TEXT("room_offline_scenario.py"), Arguments, 180.));
                                    }
                                    return true;

                                };
                                if (ColdOnly)
                                {
                                    auto PresentationSequence = MakeShared<FRoomPresentationSequence>(
                                        Test, Room, AuthorityOwner, FrameSequenceState->Control, Config,
                                        FrameSequenceState->Delivery, FrameSequenceState->Match,
                                        FrameSequenceState->InitialX, FrameSequenceState->InitialHealth, Render, MoveTemp(Completion));
                                    return PresentationSequence->Begin(AfterFrames);
                                }
                                AfterFrames.Continue(MoveTemp(Completion));
                                return true;
                            });
                        return true;
                    },
                    [=, &Test](const FString &, int64 StartTick) {
                        if (!Test.TestEqual(TEXT("immediate frame-zero witness retains original start clock"),
                                            *Clock, StartTick))
                            return false;
                        Room->Authenticate(TEXT("viewer"), TEXT("secret"), Content);
                        const auto Delivery = Room->Observe(TEXT("viewer"));
                        Test.TestEqual(TEXT("frame zero boundary"), Delivery.Frame, Delay == 0 ? 0 : -1);
                        Test.TestEqual(TEXT("zero bytes obey delay"), Delivery.Gameplay.State.IsEmpty(),
                                       Delay > 0);
                        return true;
                    });
                return true;
            }));
        }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomBattleDelay, "NightSky.Room.RealBattleDelayedHistory",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomBattleDelay::RunTest(const FString &)
{
    return RunRoomBattleDelay(*this, false);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomFreshOffline, "NightSky.Room.FreshOfflineReplay",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomFreshOffline::RunTest(const FString &)
{
    return RunRoomBattleDelay(*this, true);
}

#include "NightSkyEngine/Network/RoomPanel.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomWidgets, "NightSky.Room.WidgetControls",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

struct FRoomWidgetLifetime
{
    // Keep the fixture world alive until the destructor releases its panel and Slate content.
    TSharedPtr<FRoomBattleWorld> WorldOwner;
    URoomPanel *Panel = nullptr;
    RoomWidgetTestAccess::FPanelHost Input;
    TArray<TSharedPtr<FJsonValue>> DisplayObservations;
    ~FRoomWidgetLifetime()
    {
        Input.Reset();
        if (Panel)
        {
            Panel->RemoveFromParent();
            Panel->RemoveFromRoot();
        }
    }
    void Create(UGameInstance *Game, URoomConnection *Connection)
    {
        Input.Reset();
        if (Panel)
            Panel->RemoveFromRoot();
        Panel = CreateWidget<URoomPanel>(Game, URoomPanel::StaticClass());
        if (Panel)
        {
            Panel->AddToRoot();
            Panel->Connection = Connection;
            Input.Mount(Panel);
        }
    }
    UWidget *Status() const
    {
        return Panel->GetWidgetFromName(TEXT("RoomStatus"));
    }
};

// Exercise the keyboard adapter before room commands depend on its text entry.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomWidgetKeyboardInput, "NightSky.Verifier.RoomWidgetKeyboardInput",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomWidgetKeyboardInput::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FRoomScenarioCommand(this, [this](FRoomScenarioCommand &) {
        auto NativeOwner = MakeShared<FRoomBattleWorld>();
        auto &Native = *NativeOwner;
        FURL URL;
        URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        Native.World->SetGameMode(URL);
        Native.World->InitializeActorsForPlay(URL);
        Native.World->BeginPlay();
        auto UI = MakeShared<FRoomWidgetLifetime>();
        UI->WorldOwner = NativeOwner;
        UI->Create(Native.Game, nullptr);
        if (!TestNotNull(TEXT("keyboard input panel created"), UI->Panel))
            return false;
        if (!TestTrue(TEXT("keyboard input window is mounted"), UI->Input.GetWindow().IsValid()))
            return false;
        // A small test-owned viewport requires ordinary navigation to lower controls.
        UI->Input.GetWindow()->Resize(FVector2D(640, 320));
        for (const TCHAR *Name : {TEXT("Identity"), TEXT("Secret")})
        {
            auto *Field = UI->Panel->GetWidgetFromName(Name);
            if (!TestNotNull(FString(TEXT("keyboard input field ")) + Name, Field))
                return false;
            for (const FString Text : {FString(), FString(TEXT("first")), FString(TEXT("replacement")),
                                       FString(), FString(TEXT("after-clear"))})
            {
                if (!TestTrue(TEXT("keyboard entry dispatches on empty and populated fields"),
                              UI->Input.Enter(Field, Text)))
                    return false;
                if (!TestEqual(TEXT("keyboard entry replaces the complete field value"),
                               RoomWidgetTestAccess::GetText(Field).ToString(), Text))
                    return false;
            }
        }
        auto *Bottom = UI->Panel->GetWidgetFromName(TEXT("Action_play-replay"));
        if (!TestNotNull(TEXT("lower keyboard validation control exists"), Bottom) ||
            !TestTrue(TEXT("ordinary scrolling reveals a control below the viewport"), UI->Input.Reveal(Bottom)))
            return false;
        auto *Identity = UI->Panel->GetWidgetFromName(TEXT("Identity"));
        if (!TestTrue(TEXT("keyboard input returns to a control above the viewport"),
                      UI->Input.Enter(Identity, TEXT("after-scroll"))))
            return false;
        TestEqual(TEXT("scrolling preserves ordinary keyboard replacement"),
                  RoomWidgetTestAccess::GetText(Identity).ToString(), FString(TEXT("after-scroll")));
        return true;
    }));
    return true;
}

bool FRoomWidgets::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FRoomScenarioCommand(this, [this](FRoomScenarioCommand &Scenario) {
        auto NativeOwner = MakeShared<FRoomBattleWorld>();
        auto &Native = *NativeOwner;
        FURL URL;
        URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        Native.World->SetGameMode(URL);
        Native.World->InitializeActorsForPlay(URL);
        Native.World->BeginPlay();
        auto *Controller = Native.World->SpawnActor<ANightSkyPlayerController>();
        auto *Connection = Controller->FindComponentByClass<URoomConnection>();
        auto UI = MakeShared<FRoomWidgetLifetime>();
        UI->WorldOwner = NativeOwner;
        UI->Create(Native.Game, Connection);
        if (!TestNotNull(TEXT("native room panel created"), UI->Panel))
            return false;
        auto CaptureDisplay = [this, UI](const TCHAR *Field, bool Expected) {
            if (!TestTrue(TEXT("status is visible through ordinary scrolling before capture"),
                          UI->Input.Reveal(UI->Status())))
                return;
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("field"), Field);
            FString Image;
            TestTrue(TEXT("actual mounted room widget pixels captured"),
                     RoomWidgetCapture::SaveWindow(UI->Input.GetWindow(), Image));
            Row->SetStringField(TEXT("image"), Image);
            Row->SetBoolField(TEXT("expected"), Expected);
            UI->DisplayObservations.Add(MakeShared<FJsonValueObject>(Row));
        };
        auto CaptureValue = [this, UI](const TCHAR *Field, const TSharedPtr<FJsonValue> &Expected) {
            UWidget *Observed = FCString::Strcmp(Field, TEXT("secret_masked")) == 0
                                    ? UI->Panel->GetWidgetFromName(TEXT("Secret")) : UI->Status();
            if (!TestTrue(TEXT("observed field is visible through ordinary scrolling before capture"),
                          UI->Input.Reveal(Observed)))
                return;
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("field"), Field);
            FString Image;
            TestTrue(TEXT("actual mounted status pixels captured"),
                     RoomWidgetCapture::SaveWindow(UI->Input.GetWindow(), Image));
            Row->SetStringField(TEXT("image"), Image);
            Row->SetField(TEXT("expected"), Expected);
            UI->DisplayObservations.Add(MakeShared<FJsonValueObject>(Row));
        };
        auto CaptureRole = [CaptureValue](const TCHAR *Role) {
            CaptureValue(TEXT("member_role"), MakeShared<FJsonValueString>(Role));
        };
        auto CaptureViewer = [CaptureValue](const FRoomDelivery &Delivery) {
            CaptureValue(TEXT("room_identity"), MakeShared<FJsonValueString>(Delivery.Room));
            CaptureValue(TEXT("match_identity"), MakeShared<FJsonValueString>(Delivery.Match));
            CaptureValue(TEXT("playback_mode"), MakeShared<FJsonValueString>(Delivery.Mode));
            CaptureValue(TEXT("cursor"), MakeShared<FJsonValueNumber>(Delivery.Frame));
            CaptureValue(TEXT("released_edge"), MakeShared<FJsonValueNumber>(Delivery.Edge));
            CaptureValue(TEXT("history_start"), MakeShared<FJsonValueNumber>(0));
            CaptureValue(TEXT("buffering"), MakeShared<FJsonValueBoolean>(Delivery.Frame < 0));
            CaptureValue(TEXT("recovery_pending"), MakeShared<FJsonValueBoolean>(Delivery.Recovering));
        };
        auto *SecretField = UI->Panel->GetWidgetFromName(TEXT("Secret"));
        if (!TestNotNull(TEXT("password control exists"), SecretField))
            return false;
        // Password masking is verified from actual pixels after entering the secret.
        auto Enter = [this, UI](const TCHAR *Name, const FString &Text) {
            auto *Field = UI->Panel->GetWidgetFromName(Name);
            if (!TestNotNull(FString(TEXT("visible input ")) + Name, Field))
                return false;
            return TestTrue(FString(TEXT("editable input receives keyboard text ")) + Name,
                            UI->Input.Enter(Field, Text));
        };
        auto Click = [this, UI](const TCHAR *Name) {
            auto *Button = UI->Panel->GetWidgetFromName(FName(*(TEXT("Action_") + FString(Name))));
            if (!TestNotNull(FString(TEXT("visible action ")) + Name, Button))
                return false;
            return TestTrue(FString(TEXT("action receives pointer click ")) + Name, UI->Input.Click(Button));
        };
        // Negative and redundant actions may be disabled, hidden, or omitted. Positive actions use Click above.
        auto AttemptClick = [this, UI](const TCHAR *Name) {
            auto *Button = UI->Panel->GetWidgetFromName(FName(*(TEXT("Action_") + FString(Name))));
            const auto Result = Button ? UI->Input.AttemptClick(Button)
                                       : RoomWidgetTestAccess::EAttemptResult::Unavailable;
            TestTrue(FString(TEXT("optional action can be attempted or is unavailable ")) + Name,
                     Result != RoomWidgetTestAccess::EAttemptResult::Failed);
            return Result;
        };
        auto SameReplayIds = [](TArray<FString> First, TArray<FString> Second) {
            First.Sort();
            Second.Sort();
            return First == Second;
        };
        const FBattleData WidgetDefaults = Native.Game->BattleData;
        if (!Enter(TEXT("Identity"), TEXT("host")) || !Enter(TEXT("Secret"), TEXT("secret")) ||
            !Enter(TEXT("Slot"), Slot()) || !Enter(TEXT("Delay"), TEXT("120")))
            return false;
        CaptureValue(TEXT("secret_masked"), MakeShared<FJsonValueBoolean>(true));
        if (!Click(TEXT("create")))
            return false;
        auto *Room = Native.Game->GetSubsystem<USpectatorRoom>();
        auto Clock = MakeShared<int64>(0);
        Room->SetClock([Clock]() { return *Clock; });
        if (!TestTrue(TEXT("create button creates persistent room"), Room->IsEnabled()))
            return false;
        if (!Click(TEXT("authenticate")))
            return false;
        TestFalse(TEXT("login button acknowledges authenticated membership"),
                  Connection->LastDelivery.Membership.IsEmpty());
        const FString OrganizerMembership = Connection->LastDelivery.Membership;
        CaptureDisplay(TEXT("organizer_role"), true);
        CaptureRole(TEXT("organizer"));
        CaptureValue(TEXT("room_identity"), MakeShared<FJsonValueString>(Connection->LastDelivery.Room));
        if (!Click(TEXT("lock")))
            return false;
        TestTrue(TEXT("lock button changes authority and returned presentation"),
                 Connection->LastDelivery.Locked);
        CaptureDisplay(TEXT("selection_locked"), true);
        Click(TEXT("unlock"));
        TestFalse(TEXT("unlock button restores selection controls"), Room->Observe(TEXT("host")).Locked);
        CaptureDisplay(TEXT("selection_locked"), false);
        Click(TEXT("retry-content"));
        TestEqual(TEXT("content retry preserves authenticated organizer membership"),
                  Connection->LastDelivery.Membership, OrganizerMembership);
        Click(TEXT("list-replays"));
        CaptureDisplay(TEXT("replay_listing"), !Room->SavedReplays().IsEmpty());
        // Drive the remaining room operations through the actual native buttons.
        auto *Chara = FixtureAsset<UPrimaryCharaData>(TEXT("WidgetCharacter") +
                                                      FGuid::NewGuid().ToString(EGuidFormats::Digits));
        Chara->PlayerClass = AReplayFixtureFighter::StaticClass();
        auto *Stage = FixtureAsset<UPrimaryStageData>(TEXT("WidgetStage") +
                                                      FGuid::NewGuid().ToString(EGuidFormats::Digits));
        Stage->StageURL = TEXT("/Engine/Maps/Entry?game=/Script/NightSkyEngine.ReplayFixtureRoomGameMode");
        if (!TestTrue(TEXT("widget native content saved"), SaveFixture(Chara) && SaveFixture(Stage)))
            return false;
        FBattleData WidgetConfiguration = WidgetDefaults;
        WidgetConfiguration.PlayerListP1 = {Chara};
        WidgetConfiguration.PlayerListP2 = {Chara};
        WidgetConfiguration.Stage = Stage;
        Native.OwnConfiguration(WidgetConfiguration);
        auto *FighterController = Native.World->SpawnActor<ANightSkyPlayerController>();
        auto *ViewerController = Native.World->SpawnActor<ANightSkyPlayerController>();
        auto *Fighter = FighterController->FindComponentByClass<URoomConnection>();
        auto *Viewer = ViewerController->FindComponentByClass<URoomConnection>();
        auto Switch = [UI](URoomConnection *Target) {
            UI->Panel->Connection->OnDelivery.RemoveDynamic(UI->Panel, &URoomPanel::UpdateDelivery);
            UI->Panel->Connection = Target;
            Target->OnDelivery.AddDynamic(UI->Panel, &URoomPanel::UpdateDelivery);
            UI->Panel->UpdateDelivery(Target->LastDelivery);
        };
        Switch(Fighter);
        Enter(TEXT("Identity"), TEXT("fighter"));
        Click(TEXT("authenticate"));
        CaptureRole(TEXT("spectator"));
        Click(TEXT("queue"));
        TestTrue(TEXT("fighter queue button acknowledged"),
                 RoomResponseHasEffect(Fighter->LastDelivery, TEXT("accepted")));
        Switch(Viewer);
        Enter(TEXT("Identity"), TEXT("viewer"));
        Click(TEXT("authenticate"));
        Click(TEXT("queue"));
        Click(TEXT("withdraw"));
        TestTrue(TEXT("withdraw button acknowledged"),
                 RoomResponseHasEffect(Viewer->LastDelivery, TEXT("accepted")));
        Click(TEXT("queue"));
        Switch(Connection);
        Enter(TEXT("Value"), TEXT("viewer"));
        Enter(TEXT("Frame"), TEXT("0"));
        Click(TEXT("offer"));
        TestTrue(TEXT("offer button acknowledged"),
                 RoomResponseHasEffect(Connection->LastDelivery, TEXT("accepted")));
        Switch(Viewer);
        AttemptClick(TEXT("queue"));
        Click(TEXT("decline"));
        TestEqual(TEXT("decline button rejects offered seat without transfer"), Viewer->LastDelivery.Role,
                  FString(TEXT("spectator")));
        Switch(Connection);
        Enter(TEXT("Value"), TEXT("fighter"));
        Enter(TEXT("Frame"), TEXT("1"));
        Click(TEXT("offer"));
        Switch(Fighter);
        AttemptClick(TEXT("queue"));
        Click(TEXT("accept"));
        TestEqual(TEXT("accept button transfers fighter seat"), Fighter->LastDelivery.Role,
                  FString(TEXT("fighter")));
        CaptureRole(TEXT("fighter"));
        CaptureValue(TEXT("assignment_identity"), MakeShared<FJsonValueString>(Fighter->LastDelivery.Assignment));
        FString FighterAssignment = Fighter->LastDelivery.Assignment;
        Enter(TEXT("Value"), Chara->GetPathName());
        Click(TEXT("character"));
        TestTrue(TEXT("fighter character widget accepted"),
                 RoomResponseHasEffect(Fighter->LastDelivery, TEXT("accepted")));
        Switch(Connection);
        Click(TEXT("queue"));
        Enter(TEXT("Value"), TEXT("host"));
        Enter(TEXT("Frame"), TEXT("0"));
        Click(TEXT("offer"));
        Click(TEXT("accept"));
        FString HostAssignment = Connection->LastDelivery.Assignment;
        Enter(TEXT("Value"), Chara->GetPathName());
        Click(TEXT("character"));
        Enter(TEXT("Value"), Stage->GetPathName());
        Click(TEXT("stage"));
        TestEqual(TEXT("stage widget selects actual package"), Connection->LastDelivery.SelectedStage,
                  Stage->GetPathName());
        Click(TEXT("lock"));
        const auto BeforeLockedAttempt = Room->Observe(TEXT("host"));
        const auto LockedAttempt = AttemptClick(TEXT("character"));
        if (LockedAttempt == RoomWidgetTestAccess::EAttemptResult::Dispatched)
            TestTrue(TEXT("dispatched locked widget change is rejected"),
                     RoomResponseHasEffect(Connection->LastDelivery, TEXT("locked")));
        const auto AfterLockedAttempt = Room->Observe(TEXT("host"));
        TestTrue(TEXT("locked widget attempt preserves characters and lock"),
                 AfterLockedAttempt.Locked &&
                 AfterLockedAttempt.SelectedCharacters == BeforeLockedAttempt.SelectedCharacters);
        TestEqual(TEXT("locked widget attempt preserves stage"), AfterLockedAttempt.SelectedStage,
                  BeforeLockedAttempt.SelectedStage);
        Click(TEXT("unlock"));
        Native.Game->BattleData = WidgetConfiguration;
        Enter(TEXT("Identity"), TEXT("host"));
        Click(TEXT("retry-content"));
        Click(TEXT("ready"));
        TestTrue(TEXT("host ready button accepted"),
                 RoomResponseHasEffect(Connection->LastDelivery, TEXT("accepted")));
        Switch(Fighter);
        AttemptClick(TEXT("ready"));
        Enter(TEXT("Identity"), TEXT("fighter"));
        Click(TEXT("retry-content"));
        Click(TEXT("ready"));
        TestTrue(TEXT("second fighter ready button accepted"),
                 RoomResponseHasEffect(Fighter->LastDelivery, TEXT("accepted")));
        Switch(Connection);
        const FString HostRole = Connection->LastDelivery.Role;
        const FString FighterRole = Fighter->LastDelivery.Role;
        const FString ViewerRole = Viewer->LastDelivery.Role;
        const TWeakObjectPtr<URoomConnection> HostHandle(Connection), FighterHandle(Fighter),
            ViewerHandle(Viewer);
        const TWeakObjectPtr<UWorld> OriginalWorld(Native.World);
        const FString FighterMembership = Fighter->LastDelivery.Membership;
        const FString ViewerMembership = Viewer->LastDelivery.Membership;
        struct FConnections
        {
            TWeakObjectPtr<URoomConnection> Host, Fighter, Viewer;
            TWeakObjectPtr<UWorld> World;
        };
        auto Connections = MakeShared<FConnections>();
        Connections->Host = HostHandle;
        Connections->Fighter = FighterHandle;
        Connections->Viewer = ViewerHandle;
        Connections->World = OriginalWorld;
        auto EnsureWidgetConnections = [=, this]() mutable {
            auto &Native = *NativeOwner;
            auto *Connection = Connections->Host.Get();
            auto *Fighter = Connections->Fighter.Get();
            auto *Viewer = Connections->Viewer.Get();
            if (Connections->World.Get() != Native.World || !Connection || !Fighter || !Viewer)
            {
                // These are test-owned local controls. Reacquire surviving
                // connections, or reconnect through the ordinary widget actions
                // when travel destroyed a fixture controller.
                auto Reacquire = [&](const FString &Membership) {
                    for (TActorIterator<ANightSkyPlayerController> It(Native.World); It; ++It)
                        if (auto *Candidate = It->FindComponentByClass<URoomConnection>())
                            if (Candidate->LastDelivery.Membership == Membership)
                                return Candidate;
                    auto *Controller = Native.World->SpawnActor<ANightSkyPlayerController>();
                    return Controller ? Controller->FindComponentByClass<URoomConnection>() : nullptr;
                };
                Connection = Reacquire(OrganizerMembership);
                Fighter = Reacquire(FighterMembership);
                Viewer = Reacquire(ViewerMembership);
                if (!TestNotNull(TEXT("host widget connection available after preparation"), Connection) ||
                    !TestNotNull(TEXT("fighter widget connection available after preparation"), Fighter) ||
                    !TestNotNull(TEXT("viewer widget connection available after preparation"), Viewer))
                    return false;
                UI->Create(Native.Game, Connection);
                if (!TestNotNull(TEXT("widget controls recreated after preparation travel"), UI->Panel))
                    return false;
                Enter(TEXT("Secret"), TEXT("secret"));
                for (const FString Identity :
                     {FString(TEXT("host")), FString(TEXT("fighter")), FString(TEXT("viewer"))})
                {
                    Switch(Identity == TEXT("host")      ? Connection
                           : Identity == TEXT("fighter") ? Fighter
                                                         : Viewer);
                    Enter(TEXT("Identity"), Identity);
                    Click(TEXT("authenticate"));
                }
                TestFalse(TEXT("host reauthentication has current acknowledged membership"),
                          Connection->LastDelivery.Membership.IsEmpty());
                TestFalse(TEXT("fighter reauthentication has current acknowledged membership"),
                          Fighter->LastDelivery.Membership.IsEmpty());
                TestFalse(TEXT("viewer reauthentication has current acknowledged membership"),
                          Viewer->LastDelivery.Membership.IsEmpty());
                TestEqual(TEXT("host reauthentication restores reserved role"), Connection->LastDelivery.Role,
                          HostRole);
                TestEqual(TEXT("fighter reauthentication restores reserved role"), Fighter->LastDelivery.Role,
                          FighterRole);
                TestEqual(TEXT("viewer reauthentication restores reserved role"), Viewer->LastDelivery.Role,
                          ViewerRole);
                TestFalse(TEXT("host reauthentication preserves current owned seat"),
                          Connection->LastDelivery.Assignment.IsEmpty());
                TestFalse(TEXT("fighter reauthentication preserves current owned seat"),
                          Fighter->LastDelivery.Assignment.IsEmpty());
                Switch(Connection);
            }
            Connections->Host = Connection;
            Connections->Fighter = Fighter;
            Connections->Viewer = Viewer;
            Connections->World = Native.World;
            return true;
        };
        Native.Battle = Native.World->SpawnActor<AReplayFixtureBattle>();
        Native.AssociateRoomBattle();
        Native.World = Native.Game->GetWorld();
        if (!EnsureWidgetConnections())
            return false;
        if (!TestFalse(TEXT("widget setup remains inactive before actual start dispatch"), Room->IsActive()))
            return false;
        Click(TEXT("start"));
        Scenario.AwaitStart(
            Room, NativeOwner, TEXT("host"), TEXT(""), *Clock,
            TEXT("start widget creates actual active match"),
            [=, this](FRoomScenarioCommand &Scenario) mutable {
                if (!EnsureWidgetConnections())
                    return false;
                if (!RefreshCurrentFighterAssignments(*this, Room, TEXT("host"), TEXT("fighter"),
                                                      HostAssignment, FighterAssignment))
                    return false;
                const FString BattleMatch = Scenario.SuccessfulMatch();
                if (!TestEqual(TEXT("widget reconnect retains successful match identity"),
                               Room->Observe(TEXT("host")).Match, BattleMatch) ||
                    !TestEqual(TEXT("widget live setup preserves original room clock"), *Clock,
                               Scenario.SuccessfulStartTick()))
                    return false;
                auto BeforeFrameOwner = MakeShared<int32>(0);
                auto WidgetDamage = MakeShared<bool>(false);
                Scenario.RunFrames(
                    Room, NativeOwner, 1,
                    [=, this](int32) {
                        Click(TEXT("combat-pause"));
                        TestTrue(TEXT("pause button pauses authoritative combat"),
                                 Room->Observe(TEXT("host")).Paused);
                        CaptureValue(TEXT("combat_paused"), MakeShared<FJsonValueBoolean>(true));
                        *BeforeFrameOwner = NativeOwner->Battle->BattleState.FrameNumber;
                        return true;
                    },
                    [=, this](int32) mutable {
                        if (!EnsureWidgetConnections())
                            return false;
                        TestEqual(TEXT("paused native battle frame stays frozen"),
                                  NativeOwner->Battle->BattleState.FrameNumber, *BeforeFrameOwner);
                        Click(TEXT("combat-resume"));
                        TestFalse(TEXT("resume button resumes authoritative combat"),
                                  Room->Observe(TEXT("host")).Paused);
                        CaptureValue(TEXT("combat_paused"), MakeShared<FJsonValueBoolean>(false));
                        return true;
                    },
                    [=, this](FRoomScenarioCommand &Scenario) mutable {
                        Scenario.RunFrames(
                            Room, NativeOwner, 40,
                            [=, this](int32 Frame) mutable {
                                auto &Tick = *Clock;
                                if (!EnsureWidgetConnections() ||
                                    !RefreshCurrentFighterAssignments(*this, Room, TEXT("host"),
                                                                      TEXT("fighter"), HostAssignment,
                                                                      FighterAssignment))
                                    return false;
                                Tick = Frame;
                                auto First =
                                    Operation(TEXT("input"), Frame, FString::FromInt(INP_Right | INP_A));
                                First.Match = BattleMatch;
                                First.Assignment = HostAssignment;
                                auto Second = Operation(TEXT("input"), Frame, TEXT("0"));
                                Second.Match = BattleMatch;
                                Second.Assignment = FighterAssignment;
                                Room->Command(TEXT("host"), First);
                                Room->Command(TEXT("fighter"), Second);
                                return true;
                            },
                            [=](int32) {
                                *WidgetDamage |=
                                    NativeOwner->Battle->GetMainPlayer(false)->CurrentHealth < 10000;
                                return true;
                            },
                            [=, this](FRoomScenarioCommand &) mutable {
                                if (!EnsureWidgetConnections())
                                    return false;
                                auto &Native = *NativeOwner;
                                auto &Tick = *Clock;
                                auto *Connection = Connections->Host.Get();
                                auto *Fighter = Connections->Fighter.Get();
                                auto *Viewer = Connections->Viewer.Get();
                                TestTrue(TEXT("widget-started native fight causes actual damage"),
                                         *WidgetDamage);
                                // The same native fight has 40 confirmed pairs and
                                // delay 120. Its initialized frame is still buffered.
                                Switch(Viewer);
                                Enter(TEXT("Identity"), TEXT("viewer"));
                                Click(TEXT("retry-content"));
                                Enter(TEXT("Match"), BattleMatch);
                                Click(TEXT("select-match"));
                                CaptureRole(TEXT("spectator"));
                                CaptureValue(TEXT("buffering"), MakeShared<FJsonValueBoolean>(true));
                                Tick = 160;
                                Switch(Viewer);
                                Enter(TEXT("Identity"), TEXT("viewer"));
                                AttemptClick(TEXT("queue"));
                                Click(TEXT("retry-content"));
                                Enter(TEXT("Match"), BattleMatch);
                                Click(TEXT("select-match"));
                                Click(TEXT("pause"));
                                Enter(TEXT("Frame"), TEXT("20"));
                                Click(TEXT("seek"));
                                TestEqual(TEXT("seek widget reconstructs requested frame"),
                                          Viewer->LastDelivery.Frame, 20);
                                CaptureViewer(Viewer->LastDelivery);
                                CaptureValue(TEXT("integrity_error"), MakeShared<FJsonValueBoolean>(false));
                                Click(TEXT("resume"));
                                TestEqual(TEXT("resume widget enters playing mode"),
                                          Viewer->LastDelivery.Mode, FString(TEXT("playing")));
                                CaptureValue(TEXT("playback_mode"), MakeShared<FJsonValueString>(TEXT("playing")));
                                Click(TEXT("live"));
                                TestEqual(TEXT("live widget reaches released edge"),
                                          Viewer->LastDelivery.Frame, 40);
                                CaptureViewer(Viewer->LastDelivery);
                                const auto BeforePendingExports = Room->SavedReplays();
                                const auto PendingAttempt = AttemptClick(TEXT("export"));
                                if (PendingAttempt == RoomWidgetTestAccess::EAttemptResult::Dispatched)
                                    TestTrue(TEXT("dispatched active export reports pending"),
                                             RoomResponseHasEffect(Viewer->LastDelivery, TEXT("pending")));
                                TestTrue(TEXT("active export attempt creates no complete replay"),
                                         SameReplayIds(BeforePendingExports, Room->SavedReplays()));
                                CaptureValue(TEXT("export_pending"), MakeShared<FJsonValueBoolean>(true));
                                Switch(Fighter);
                                Click(TEXT("leave"));
                                Tick = 280;
                                Switch(Viewer);
                                const auto BeforeExports = Room->SavedReplays();
                                Click(TEXT("export"));
                                TestTrue(TEXT("terminal export widget saves complete artifact"),
                                         RoomResponseHasEffect(Viewer->LastDelivery, TEXT("exported")));
                                const auto ExportedIds = Room->SavedReplays();
                                FString ExportedIdentity;
                                for (const auto &Id : ExportedIds)
                                    if (!BeforeExports.Contains(Id))
                                    {
                                        ExportedIdentity = Id;
                                        break;
                                    }
                                if (!TestFalse(TEXT("new export has discoverable replay identity"),
                                               ExportedIdentity.IsEmpty()))
                                    return false;
                                Click(TEXT("list-replays"));
                                CaptureDisplay(TEXT("replay_listing"), true);
                                Switch(Connection);
                                Click(TEXT("leave"));
                                TestTrue(TEXT("leave button revokes presence"),
                                         Room->Observe(TEXT("host")).Assignment.IsEmpty());
                                Switch(Viewer);
                                Enter(TEXT("Value"), ExportedIdentity);
                                Click(TEXT("play-replay"));
                                if (!TestTrue(TEXT("play replay widget configures ordinary replay workflow"),
                                              Native.Game->IsReplay))
                                    return false;
                                CaptureDisplay(TEXT("replay_playing"), true);
                                int32 ReplayP1 = 0, ReplayP2 = 0;
                                Native.Game->PlayReplayToGameState(0, ReplayP1, ReplayP2);
                                TestEqual(
                                    TEXT(
                                        "ordinary replay exposes original opening input after widget launch"),
                                    ReplayP1, int32(INP_Right | INP_A));
                                TestEqual(TEXT("ordinary replay exposes second owner input"), ReplayP2, 0);
                                Switch(Connection);
                                const bool WasReplay = Native.Game->IsReplay;
                                const auto BeforeInvalidExports = Room->SavedReplays();
                                if (!Enter(TEXT("Value"), TEXT("../outside")))
                                    return false;
                                AttemptClick(TEXT("play-replay"));
                                TestTrue(TEXT("unsafe replay attempt preserves saved artifacts"),
                                         SameReplayIds(BeforeInvalidExports, Room->SavedReplays()));
                                TestEqual(TEXT("unsafe replay attempt preserves replay mode"),
                                          Native.Game->IsReplay, WasReplay);
                                CaptureValue(TEXT("replay_request_unavailable"), MakeShared<FJsonValueBoolean>(true));
                                const FString DisplayPath = FPaths::ConvertRelativePathToFull(
                                    FPaths::ProjectSavedDir() / TEXT("Automation") /
                                    (TEXT("RoomUIDisplays-") +
                                     FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json")));
                                FString DisplayJson;
                                FJsonSerializer::Serialize(UI->DisplayObservations,
                                                           TJsonWriterFactory<>::Create(&DisplayJson));
                                IFileManager::Get().MakeDirectory(*FPaths::GetPath(DisplayPath), true);
                                if (!TestTrue(
                                        TEXT("captured actual display pixels for independent semantic reading"),
                                        FFileHelper::SaveStringToFile(DisplayJson, *DisplayPath)))
                                    return false;
                                ADD_LATENT_AUTOMATION_COMMAND(FRoomPythonCommand(
                                    this, TEXT("room_ui_review.py"),
                                    FString::Printf(TEXT("--observations \"%s\""), *DisplayPath), 115.));
                                UI->Panel->RemoveFromParent();
                                return true;
                            });
                        return true;
                    },
                    false, false);
                return true;
            });
        return true;
    }));
    return true;
}
// Public successful-start semantics, independent of travel/storage sequence.
class FRoomZeroInputStartCommand : public IAutomationLatentCommand
{
public:
    explicit FRoomZeroInputStartCommand(FAutomationTestBase *InTest) : Test(InTest) {}
    bool Update() override
    {
        if (!Live)
        {
            if (Case == 8) return true;
            const int32 Delays[] = {0, 1, 120, 600};
            Delay = Delays[Case % 4];
            Tick = 100;
            Live = MakeUnique<FRoomBattleWorld>();
            Configuration = Live->Game->BattleData;
            Room = Live->Game->GetSubsystem<USpectatorRoom>();
            Room->SetClock([this]() { return Tick; });
            DurableSlot = Slot();
            if (!Test->TestTrue(TEXT("zero-input room created"),
                                Room->Create(DurableSlot, TEXT("host"), TEXT("secret"), Delay))) return true;
            auto *Character = FixtureAsset<UPrimaryCharaData>(TEXT("ZeroInputCharacter") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
            Character->PlayerClass = AReplayFixtureFighter::StaticClass();
            auto *Stage = FixtureAsset<UPrimaryStageData>(TEXT("ZeroInputStage") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
            Stage->StageURL = TEXT("/Engine/Maps/Entry?game=/Script/NightSkyEngine.ReplayFixtureRoomGameMode");
            if (!Test->TestTrue(TEXT("zero-input installed content saved"), SaveFixture(Character) && SaveFixture(Stage))) return true;
            Configuration.PlayerListP1 = {Character};
            Configuration.PlayerListP2 = {Character};
            Configuration.Stage = Stage;
            ZeroInputAssets.Empty();
            ZeroInputAssets.Emplace(Character);
            ZeroInputAssets.Emplace(Stage);
            Content = USpectatorRoom::InspectContent(Configuration, Live->Game->BattleVersion);
            for (int32 Seat = 0; Seat < 2; ++Seat)
            {
                const FString Identity = FString::Printf(TEXT("f%d"), Seat);
                if (!Test->TestTrue(TEXT("zero-input fighter authenticated"), Room->Authenticate(Identity, TEXT("secret"), Content))) return true;
                Room->Command(Identity, Operation(TEXT("queue")));
                Room->Command(TEXT("host"), Operation(TEXT("offer"), Seat, Identity));
                auto Accept = Operation(TEXT("accept"));
                Accept.Assignment = Room->Observe(Identity).Offer;
                const auto Accepted = Room->Command(Identity, Accept);
                if (!Test->TestFalse(TEXT("zero-input assigned fighter owns a seat"), Accepted.Assignment.IsEmpty())) return true;
                auto Select = Operation(TEXT("character"), 0, Character->GetPathName());
                Select.Assignment = Accepted.Assignment;
                Room->Command(Identity, Select);
            }
            Room->Command(TEXT("host"), Operation(TEXT("stage"), 0, Stage->GetPathName()));
            for (const FString& Identity : {FString(TEXT("f0")), FString(TEXT("f1"))})
            {
                Room->Authenticate(Identity, TEXT("secret"), Content);
                auto Ready = Operation(TEXT("ready"));
                Ready.Assignment = Room->Observe(Identity).Assignment;
                Room->Command(Identity, Ready);
            }
            Room->Authenticate(TEXT("viewer"), TEXT("secret"), Content);
            // Supply a valid initialized native battle; do not require any particular
            // relation between preparation, travel and the successful acknowledgement.
            Live->Game->BattleData = Configuration;
            Live->Game->IsTraining = true;
            Live->Begin();
            Live->AssociateRoomBattle();
            Room->Command(TEXT("host"), Operation(TEXT("start")));
            WaitStarted = LastPump = FPlatformTime::Seconds();
            Test->AddInfo(FString::Printf(TEXT("zero-input start case=%d delay=%d interruption=%s"),
                                         Case, Delay, Case < 4 ? TEXT("departure") : TEXT("recovery")));
            // Interrupt an already successful start in this update, before
            // yielding a normal engine frame that could advance the native battle.
        }
        // Pending preparation is allowed. Observe public success, never status words.
        auto Fighter = Room->Observe(TEXT("f0"));
        if (!Room->IsActive() || Fighter.Match.IsEmpty())
        {
            if (FPlatformTime::Seconds() - WaitStarted >= 30.)
            {
                Test->AddError(TEXT("zero-input start did not establish an active match within the bounded preparation window"));
                return true;
            }
            const double Now = FPlatformTime::Seconds();
            // Advance ordinary travel and bind its completed authored fixture.
            // Stop at the first public success, before a further native tick.
            Live->AdvanceOwnedWorld(float(Now - LastPump), [this, &Fighter]() {
                Fighter = Room->Observe(TEXT("f0"));
                return Room->IsActive() && !Fighter.Match.IsEmpty();
            });
            LastPump = Now;
            if (!Room->IsActive() || Fighter.Match.IsEmpty())
                return false;
        }
        const FString Match = Fighter.Match;
        Tick = 101;
        if (Case < 4)
            Room->Command(TEXT("f0"), Operation(TEXT("leave")));
        else
        {
            Live.Reset();
            Recovery = MakeUnique<FRoomBattleWorld>();
            Room = Recovery->Game->GetSubsystem<USpectatorRoom>();
            Room->SetClock([this]() { return Tick; });
            if (!Test->TestTrue(TEXT("zero-input successful start survives authority recovery"), Room->Recover(DurableSlot))) return true;
        }
        Test->TestFalse(TEXT("zero-input interruption stops active combat"), Room->IsActive());
        if (!Test->TestTrue(TEXT("zero-input viewer authenticates after interruption"), Room->Authenticate(TEXT("viewer"), TEXT("secret"), Content))) return true;
        auto Select = Operation(TEXT("select-match")); Select.Match = Match;
        Room->Command(TEXT("viewer"), Select);
        auto Export = Operation(TEXT("export")); Export.Match = Match;
        if (Delay > 1)
        {
            Tick = 100 + Delay - 1;
            const auto Before = Room->Observe(TEXT("viewer"));
            Test->TestTrue(TEXT("zero-input frame and outcome withheld before original frame-zero release"),
                           Before.Frame < 0 && Before.Gameplay.State.IsEmpty() && Before.Outcome.IsEmpty());
            Test->TestTrue(TEXT("zero-input export pending before frame-zero release"), Room->Command(TEXT("viewer"), Export).Replay.IsEmpty());
        }
        if (Delay > 0)
        {
            Tick = 100 + Delay;
            auto Seek = Operation(TEXT("seek"), 0); Seek.Match = Match;
            const auto Released = Room->Command(TEXT("viewer"), Seek);
            Test->TestEqual(TEXT("zero-input interrupted frame releases at original start plus delay"), Released.Frame, 0);
            Test->TestFalse(TEXT("zero-input initialized state is present"), Released.Gameplay.State.IsEmpty());
            Test->TestTrue(TEXT("zero-input outcome retains its later interruption release"), Released.Outcome.IsEmpty());
            Test->TestTrue(TEXT("zero-input export pending until outcome release"), Room->Command(TEXT("viewer"), Export).Replay.IsEmpty());
        }
        Tick = 101 + Delay;
        const auto Exported = Room->Command(TEXT("viewer"), Export);
        FRoomMatch Saved;
        if (!Test->TestTrue(TEXT("zero-input interrupted history exports after both release boundaries"),
                            USpectatorRoom::ReadReplay(Exported.Replay, Saved))) return true;
        Test->TestEqual(TEXT("zero-input match identity survives interruption"), Saved.Id, Match);
        Test->TestEqual(TEXT("zero-input original start timestamp survives interruption"), Saved.StartTick, int64(100));
        Test->TestEqual(TEXT("zero-input outcome uses actual interruption time"), Saved.OutcomeTick, int64(101));
        Test->TestTrue(TEXT("zero-input export preserves authored configuration"),
                       ReplayFixtureConfigurationMatches(Saved.Configuration, Configuration));
        Test->TestTrue(TEXT("zero-input export preserves authored content revisions"),
                       Saved.Content.OrderIndependentCompareEqual(Content));
        Test->TestTrue(TEXT("zero-input export preserves authored training mode"), Saved.Training);
        Test->TestFalse(TEXT("zero-input interrupted outcome exists"), Saved.Outcome.IsEmpty());
        if (!Test->TestEqual(TEXT("zero-input history contains exactly initialized frame zero"), Saved.Frames.Num(), 1)) return true;
        Test->TestEqual(TEXT("zero-input frame confirmation remains original start"), Saved.Frames[0].ConfirmedTick, int64(100));
        FString DecodedMatch, Description; TArray<int32> InputsA, InputsB;
        if (!Test->TestTrue(TEXT("zero-input exported frame decodes through public interface"),
                            USpectatorRoom::DecodeConfirmedInputs(Saved.Frames[0], DecodedMatch, InputsA, InputsB, Description))) return true;
        Test->TestEqual(TEXT("zero-input frame identifies original match"), DecodedMatch, Match);
        Test->TestTrue(TEXT("zero-input confirmed input histories are empty"), InputsA.IsEmpty() && InputsB.IsEmpty());
        FRoomBattleWorld Playback;
        Playback.Game->BattleData = Saved.Configuration;
        Playback.Game->IsTraining = Saved.Training;
        Playback.Begin();
        if (!Test->TestTrue(TEXT("zero-input exported initialized state independently plays"),
                            USpectatorRoom::PlayFrame(Playback.Battle, Saved.Frames[0]))) return true;
        Test->TestEqual(TEXT("zero-input independent playback stays on initialized frame"), Playback.Battle->BattleState.FrameNumber, 0);
        Test->TestEqual(TEXT("zero-input independent playback retains opening position"), Playback.Battle->GetMainPlayer(true)->PosX, -150000);
        Test->TestEqual(TEXT("zero-input independent playback retains opening health"), Playback.Battle->GetMainPlayer(false)->CurrentHealth, 10000);
        Recovery.Reset(); Live.Reset(); Room = nullptr; ++Case;
        return false;
    }
private:
    FAutomationTestBase *Test;
    TUniquePtr<FRoomBattleWorld> Live, Recovery;
    USpectatorRoom *Room = nullptr;
    int32 Case = 0, Delay = 0;
    int64 Tick = 100;
    double WaitStarted = 0., LastPump = 0.;
    FString DurableSlot;
    FBattleData Configuration;
    TArray<TStrongObjectPtr<UObject>> ZeroInputAssets;
    TMap<FString, FString> Content;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomZeroInputStart,
                                 "NightSky.Room.ZeroInputSuccessfulStartInterruption",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRoomZeroInputStart::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FRoomZeroInputStartCommand(this));
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomCompleted, "NightSky.Room.CompletedOutcome",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

class FRoomCompletedScenario : public TSharedFromThis<FRoomCompletedScenario>
{
  public:
    explicit FRoomCompletedScenario(FAutomationTestBase *InTest) : Test(InTest)
    {
    }
    bool Prepare(FRoomScenarioCommand &Scenario)
    {

        auto &World = *InitialWorld;
        World.Game->IsTraining = false;
        World.Game->BattleData.BattleFormat = EBattleFormat::Rounds;
        World.Game->BattleData.StartRoundTimer = 1;
        World.Game->BattleData.RoundCount = 1;
        World.Game->BattleData.TimeUntilRoundStart = 0;
        World.Game->BattleData.Random.Reseed(9009);
        Config = World.Game->BattleData;
        Room = World.Game->GetSubsystem<USpectatorRoom>();

        Room->SetClock([this]() { return Tick; });
        DurableSlot = Slot();
        if (!Test->TestTrue(TEXT("create competitive room"),
                            Room->Create(DurableSlot, TEXT("host"), TEXT("secret"), 120)))
            return false;
        Chara = FixtureAsset<UPrimaryCharaData>(TEXT("CompletionCharacter") +
                                                FGuid::NewGuid().ToString(EGuidFormats::Digits));
        Chara->PlayerClass = AReplayFixtureFighter::StaticClass();
        Stage = FixtureAsset<UPrimaryStageData>(TEXT("CompletionStage") +
                                                FGuid::NewGuid().ToString(EGuidFormats::Digits));
        Stage->StageURL = TEXT("/Engine/Maps/Entry?game=/Script/NightSkyEngine.ReplayFixtureRoomGameMode");
        if (!Test->TestTrue(TEXT("competitive native packages saved"),
                            SaveFixture(Chara) && SaveFixture(Stage)))
            return false;
        Config.PlayerListP1 = {Chara};
        Config.PlayerListP2 = {Chara};
        Config.Stage = Stage;
        World.OwnConfiguration(Config);
        for (int32 Seat = 0; Seat < 2; ++Seat)
        {
            const FString Id = FString::Printf(TEXT("f%d"), Seat);
            Room->Authenticate(Id, TEXT("secret"), {});
            Room->Command(Id, Operation(TEXT("queue")));
            Room->Command(TEXT("host"), Operation(TEXT("offer"), Seat, Id));
            auto Accept = Operation(TEXT("accept"));
            Accept.Assignment = Room->Observe(Id).Offer;
            Assignments[Seat] = Room->Command(Id, Accept).Assignment;
            auto Select = Operation(TEXT("character"), 0, Chara->GetPathName());
            Select.Assignment = Assignments[Seat];
            Room->Command(Id, Select);
        }
        Room->Command(TEXT("host"), Operation(TEXT("stage"), 0, Stage->GetPathName()));
        Content = USpectatorRoom::InspectContent(Config, World.Game->BattleVersion);
        for (int32 Seat = 0; Seat < 2; ++Seat)
        {
            const FString Id = FString::Printf(TEXT("f%d"), Seat);
            Room->Authenticate(Id, TEXT("secret"), Content);
            Assignments[Seat] = Room->Observe(Id).Assignment;
            auto Ready = Operation(TEXT("ready"));
            Ready.Assignment = Assignments[Seat];
            Room->Command(Id, Ready);
        }
        World.Game->BattleData = Config;
        World.Game->IsTraining = false;
        World.Begin();
        World.AssociateRoomBattle();
        if (!Test->TestFalse(TEXT("competitive setup remains inactive before actual start dispatch"),
                             Room->IsActive()))
            return false;
        Room->Command(TEXT("host"), Operation(TEXT("start")));
        Scenario.AwaitStart(
            Room, InitialWorld.ToSharedRef(), TEXT("f0"), TEXT(""), Tick,
            TEXT("competitive start establishes initialized match"),
            [Self = AsShared()](FRoomScenarioCommand &Next) { return Self->AfterInitial(Next); });
        return true;
    }

  private:
    bool AfterInitial(FRoomScenarioCommand &Scenario)
    {
        auto &World = *InitialWorld;
        if (!RefreshCurrentFighterAssignments(*Test, Room, TEXT("f0"), TEXT("f1"), Assignments[0],
                                              Assignments[1]))
            return false;
        Room->Authenticate(TEXT("viewer"), TEXT("secret"), Content);
        Match = Scenario.SuccessfulMatch();
        if (!Test->TestEqual(TEXT("competitive live setup preserves original room clock"), Tick,
                             Scenario.SuccessfulStartTick()))
            return false;
        InitialNativeFrame = World.Battle->BattleState.FrameNumber;
        InitialNativeTimer = World.Battle->BattleState.RoundTimer;
        InitialNativePhase = int32(World.Battle->BattleState.BattlePhase);
        InitialTraining = World.Game->IsTraining;
        Damage = false;
        Last = 0;
        DecidingFrame = INDEX_NONE;
        DecidingTick = -1;
        if (!Test->TestEqual(TEXT("competitive fixture uses authored rounds format"),
                             int32(World.Battle->BattleState.BattleFormat), int32(EBattleFormat::Rounds)) ||
            !Test->TestFalse(TEXT("competitive decision fixture excludes training mode"), World.Game->IsTraining) ||
            !Test->TestEqual(TEXT("competitive fixture begins with no first-side wins"),
                             World.Battle->BattleState.P1RoundsWon, 0) ||
            !Test->TestEqual(TEXT("competitive fixture begins with no second-side wins"),
                             World.Battle->BattleState.P2RoundsWon, 0))
            return false;
        Scenario.RunFrames(
            Room, InitialWorld.ToSharedRef(), 600,
            [Self = AsShared()](int32 Frame) { return Self->SubmitInitialFrame(Frame); },
            [Self = AsShared()](int32 Frame) { return Self->InspectInitialFrame(Frame); },
            [Self = AsShared()](FRoomScenarioCommand &Next) { return Self->AfterInitialFrames(Next); }, true);
        return true;
    }
    bool SubmitInitialFrame(int32 Frame)
    {
        if (!RefreshCurrentFighterAssignments(*Test, Room, TEXT("f0"), TEXT("f1"), Assignments[0],
                                              Assignments[1]))
            return false;

        Tick = Frame;
        for (int32 Seat = 0; Seat < 2; ++Seat)
        {
            auto Input = Operation(TEXT("input"), Frame, FString::FromInt(Seat == 0 ? INP_Right | INP_A : 0));
            Input.Match = Match;
            Input.Assignment = Assignments[Seat];
            if (!Test->TestTrue(TEXT("competitive owner input accepted"),
                                CheckRoomCommand(*Test, Room, FString::Printf(TEXT("f%d"), Seat), Input,
                                                 TEXT("accepted"))))
                return false;
        }
        return true;
    }
    bool InspectInitialFrame(int32 Frame)
    {
        auto &World = *InitialWorld;

        Last = Frame;
        LastConfirmedTick = Tick;
        Damage |= World.Battle->GetMainPlayer(false)->CurrentHealth < 10000;
        // Observe native scores after each confirmed pair. The authored rounds
        // match is decided once a side reaches its target and leads, or a tied
        // sudden-death round takes both sides past the target. Win animations and
        // the room's active flag do not define this first deciding frame.
        const auto &State = World.Battle->BattleState;
        const bool FirstWins = State.P1RoundsWon >= Config.RoundCount &&
                               State.P1RoundsWon > State.P2RoundsWon;
        const bool SecondWins = State.P2RoundsWon >= Config.RoundCount &&
                                State.P2RoundsWon > State.P1RoundsWon;
        const bool Draw = State.P1RoundsWon > Config.RoundCount &&
                          State.P2RoundsWon > Config.RoundCount;
        if (DecidingFrame == INDEX_NONE && (FirstWins || SecondWins || Draw))
        {
            DecidingFrame = Frame;
            DecidingTick = Tick;
            Test->AddInfo(FString::Printf(
                TEXT("native match decision frame=%d tick=%lld scores=%d/%d timer=%d health=%d/%d"),
                DecidingFrame, DecidingTick, int32(State.P1RoundsWon), int32(State.P2RoundsWon),
                int32(State.RoundTimer), World.Battle->GetMainPlayer(true)->CurrentHealth,
                World.Battle->GetMainPlayer(false)->CurrentHealth));
        }
        const auto Viewer = Room->Observe(TEXT("viewer"));
        if (DecidingTick < 0 || Tick < DecidingTick + 120)
            return Test->TestTrue(TEXT("normal outcome remains hidden before independent decision release"),
                                  Viewer.Outcome.IsEmpty());
        return Test->TestFalse(TEXT("normal outcome releases from decision even during terminal presentation"),
                               Viewer.Outcome.IsEmpty());
    }
    bool AfterInitialFrames(FRoomScenarioCommand &Scenario)
    {
        auto &World = *InitialWorld;

        if (!Test->TestFalse(TEXT("actual round timer and winner lifecycle finish match"), Room->IsActive()))
        {
            Test->AddInfo(FString::Printf(
                TEXT("completion failure post-live-setup initial native frame=%d timer=%d phase=%d "
                     "training=%d; configured round-seconds=%d round-count=%d round-start-delay=%d"),
                InitialNativeFrame, InitialNativeTimer, InitialNativePhase, int32(InitialTraining),
                int32(Config.StartRoundTimer), int32(Config.RoundCount), int32(Config.TimeUntilRoundStart)));
            Test->AddInfo(FString::Printf(
                TEXT("completion failure after input pairs=%d native frame=%d timer=%d phase=%d training=%d "
                     "runner=%d rounds-won=%d/%d health=%d/%d damage-observed=%d; current config "
                     "round-seconds=%d round-count=%d round-start-delay=%d; battle-in-current-world=%d"),
                Last, int32(World.Battle->BattleState.FrameNumber),
                int32(World.Battle->BattleState.RoundTimer), int32(World.Battle->BattleState.BattlePhase),
                int32(World.Game->IsTraining), int32(World.Game->FighterRunner),
                int32(World.Battle->BattleState.P1RoundsWon), int32(World.Battle->BattleState.P2RoundsWon),
                World.Battle->GetMainPlayer(true)->CurrentHealth,
                World.Battle->GetMainPlayer(false)->CurrentHealth, int32(Damage),
                int32(World.Game->BattleData.StartRoundTimer), int32(World.Game->BattleData.RoundCount),
                int32(World.Game->BattleData.TimeUntilRoundStart),
                int32(World.Battle->GetWorld() == World.Game->GetWorld())));
            return false;
        }
        Test->TestTrue(TEXT("real combat distinguishes winner before timeout"), Damage);
        if (!Test->TestTrue(TEXT("confirmed native gameplay establishes an independent match decision"),
                            DecidingFrame > 0 && DecidingTick >= 0))
            return false;
        auto Export = Operation(TEXT("export"));
        Export.Match = Match;
        const int64 OutcomeReleaseTick = DecidingTick + 120;
        const int64 TerminalReleaseTick = LastConfirmedTick + 120;
        if (Tick < OutcomeReleaseTick)
        {
            Tick = OutcomeReleaseTick - 1;
            Test->TestTrue(TEXT("normal outcome hidden until independent deciding frame release"),
                           Room->Observe(TEXT("viewer")).Outcome.IsEmpty());
            Test->TestTrue(TEXT("normal export pending before outcome release"),
                           CheckRoomCommand(*Test, Room, TEXT("viewer"), Export, TEXT("pending")));
            ++Tick;
        }
        Test->TestFalse(TEXT("normal outcome available at independently anchored release"),
                        Room->Observe(TEXT("viewer")).Outcome.IsEmpty());
        // A match may retain later presentation frames. Outcome release does not
        // make an export complete until its actual terminal frame also releases.
        if (Tick < TerminalReleaseTick)
        {
            Tick = TerminalReleaseTick - 1;
            Test->TestTrue(TEXT("normal export pending before terminal frame release"),
                           CheckRoomCommand(*Test, Room, TEXT("viewer"), Export, TEXT("pending")));
        }
        Tick = TerminalReleaseTick;
        const auto Complete = Room->Command(TEXT("viewer"), Export);
        Test->TestTrue(TEXT("normal artifact releases"), RoomResponseHasEffect(Complete, TEXT("exported")));
        FRoomMatch Replay;
        if (!Test->TestTrue(TEXT("normal artifact is complete"),
                            USpectatorRoom::ReadReplay(Complete.Replay, Replay)))
            return false;
        if (!Test->TestEqual(TEXT("competitive artifact retains successful match identity"), Replay.Id, Match))
            return false;
        Test->TestTrue(TEXT("completion exports a nonempty outcome"),
                       !Replay.Outcome.TrimStartAndEnd().IsEmpty());
        CompletedOutcome = Replay.Outcome;
        Test->TestTrue(TEXT("independent native battle confirms the winning side"),
                       World.Battle->BattleState.P1RoundsWon > World.Battle->BattleState.P2RoundsWon);
        Test->TestEqual(TEXT("normal outcome uses independently witnessed deciding confirmation"),
                        Replay.OutcomeTick, DecidingTick);
        Test->TestEqual(TEXT("terminal frame retains its own confirmed timestamp"),
                        Replay.Frames.Last().ConfirmedTick, LastConfirmedTick);
        Test->TestEqual(TEXT("all competitive frames including any terminal presentation are retained"),
                        Replay.Frames.Num(), Last + 1);
        if (!CheckReplayPublicFrames(*Test, Replay))
            return false;
        // Successive real worlds recover the same authority store. Each active predecessor
        // becomes interrupted through recovery, while the viewer keeps its selected timeline.
        PreviousMatch = Match;
        const FString MapLeaf = TEXT("RetainedMap") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString MapName = TEXT("/Game/RoomFixture/") + MapLeaf;
        auto *MapPackage = CreatePackage(*MapName);
        auto *MapWorld = UWorld::CreateWorld(EWorldType::Inactive, false, FName(*MapLeaf), MapPackage, false,
                                             ERHIFeatureLevel::Num, nullptr, true);
        MapWorld->SetFlags(RF_Public | RF_Standalone);
        auto *StageMarker =
            MapWorld->SpawnActor<AStaticMeshActor>(StageMarkerLocation, FRotator::ZeroRotator);
        if (!Test->TestNotNull(TEXT("alternate stage has authored scenery"), StageMarker))
            return false;
        StageMarker->GetStaticMeshComponent()->SetStaticMesh(
            LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        StageMarker->SetActorScale3D(StageMarkerScale);
        FSavePackageArgs MapArgs;
        MapArgs.TopLevelFlags = RF_Public | RF_Standalone;
        const FString MapFile =
            FPackageName::LongPackageNameToFilename(MapName, FPackageName::GetMapPackageExtension());
        if (!Test->TestTrue(TEXT("save a distinct real retained stage map"),
                            UPackage::SavePackage(MapPackage, MapWorld, *MapFile, MapArgs)))
            return false;
        OtherStage = FixtureAsset<UPrimaryStageData>(TEXT("OtherStage") +
                                                     FGuid::NewGuid().ToString(EGuidFormats::Digits));
        OtherStage->StageURL = MapName + TEXT("?game=/Script/NightSkyEngine.ReplayFixtureRoomGameMode");
        if (!Test->TestTrue(TEXT("save alternate stage selection"), SaveFixture(OtherStage)))
            return false;
        Heavy = FixtureAsset<UPrimaryCharaData>(TEXT("RetainedHeavy") +
                                                FGuid::NewGuid().ToString(EGuidFormats::Digits));
        Heavy->PlayerClass = AReplayFixtureHeavyFighter::StaticClass();
        if (!Test->TestTrue(TEXT("save a distinguishable alternate character configuration"),
                            SaveFixture(Heavy)))
            return false;
        FBattleData RetainedAssets = Config;
        RetainedAssets.Stage = OtherStage;
        RetainedAssets.PlayerListP1 = {Heavy};
        InitialWorld->OwnConfiguration(RetainedAssets);
        // The completed predecessor releases its store before another authority recovers it.
        RetainedFixtureAssets = MoveTemp(InitialWorld->FixtureAssets);
        InitialWorld.Reset();
        Room = nullptr;
        return PrepareSuccessor(Scenario);
    }
    bool PrepareSuccessor(FRoomScenarioCommand &Scenario)
    {
        NextWorld = MakeShared<FRoomBattleWorld>();
        auto &Next = *NextWorld;
        NextConfiguration = Config;
        NextConfiguration.Stage = Generation % 2 == 1 ? OtherStage : Stage;
        NextConfiguration.PlayerListP1 = {Generation % 2 == 1 ? Heavy : Chara};
        Next.OwnConfiguration(NextConfiguration);
        NextRoom = Next.Game->GetSubsystem<USpectatorRoom>();
        Tick += 240;
        NextRoom->SetClock([this]() { return Tick; });
        if (!Test->TestTrue(TEXT("successor authority recovers acknowledged history"),
                            NextRoom->Recover(DurableSlot)))
            return false;
        auto *SelectedStage = Generation % 2 == 1 ? OtherStage : Stage;
        Test->TestTrue(TEXT("organizer changes actual stage between matches"),
                       CheckRoomCommand(*Test, NextRoom, TEXT("host"),
                                        Operation(TEXT("stage"), 0, SelectedStage->GetPathName()),
                                        TEXT("accepted")));
        auto *SelectedCharacter = Generation % 2 == 1 ? Heavy : Chara;
        auto ChangeCharacter = Operation(TEXT("character"), 0, SelectedCharacter->GetPathName());
        ChangeCharacter.Assignment = NextRoom->Observe(TEXT("f0")).Assignment;
        Test->TestTrue(TEXT("current fighter selects actual alternate character between matches"),
                       CheckRoomCommand(*Test, NextRoom, TEXT("f0"), ChangeCharacter, TEXT("accepted")));
        NextContent = USpectatorRoom::InspectContent(NextConfiguration, Next.Game->BattleVersion);
        for (int32 Seat = 0; Seat < 2; ++Seat)
        {
            const FString Id = FString::Printf(TEXT("f%d"), Seat);
            Test->TestTrue(TEXT("reserved fighter reauthenticates for successor"),
                           NextRoom->Authenticate(Id, TEXT("secret"), NextContent));
            Assignments[Seat] = NextRoom->Observe(Id).Assignment;
            auto Ready = Operation(TEXT("ready"));
            Ready.Assignment = Assignments[Seat];
            Ready.Match = PreviousMatch;
            Test->TestTrue(TEXT("successor fighter readiness"),
                           CheckRoomCommand(*Test, NextRoom, Id, Ready, TEXT("accepted")));
        }
        Next.Game->BattleData = NextConfiguration;
        Next.Game->IsTraining = true;
        Next.Begin();
        Next.AssociateRoomBattle();
        if (!Test->TestFalse(TEXT("successor setup remains inactive before actual start dispatch"),
                             NextRoom->IsActive()))
            return false;
        NextRoom->Command(TEXT("host"), Operation(TEXT("start")));
        Scenario.AwaitStart(
            NextRoom, NextWorld.ToSharedRef(), TEXT("f0"), PreviousMatch, Tick,
            TEXT("successor starts through organizer control"),
            [Self = AsShared()](FRoomScenarioCommand &Next) { return Self->AfterSuccessor(Next); });
        return true;
    }
    bool AfterSuccessor(FRoomScenarioCommand &Scenario)
    {
        auto &Next = *NextWorld;
        if (!RefreshCurrentFighterAssignments(*Test, NextRoom, TEXT("f0"), TEXT("f1"), Assignments[0],
                                              Assignments[1]))
            return false;
        const FString Current = Scenario.SuccessfulMatch();
        if (!Test->TestEqual(TEXT("successor live setup retains successful match identity"),
                             NextRoom->Observe(TEXT("f0")).Match, Current) ||
            !Test->TestEqual(TEXT("successor live setup preserves original room clock"), Tick,
                             Scenario.SuccessfulStartTick()))
            return false;
        Test->TestTrue(TEXT("successor gets a new match identity"), Current != PreviousMatch);
        Successors.Add(Current);
        SuccessorConfigurations.Add(NextConfiguration);
        SuccessorContent.Add(NextContent);
        SuccessorStartTicks.Add(Scenario.SuccessfulStartTick());
        SuccessorTraining.Add(true); // Authored by each FRoomBattleWorld constructor, before public callbacks.
        SuccessorInitialFrames.Add(Next.Battle->BattleState.FrameNumber);
        SuccessorInitialTimers.Add(Next.Battle->BattleState.RoundTimer);
        SuccessorMaxHealth.Add(Next.Battle->GetMainPlayer(true)->MaxHealth);
        SuccessorDamage = false;
        ActiveSuccessorMatch = Current;
        Scenario.RunFrames(
            NextRoom, NextWorld.ToSharedRef(), 40,
            [Self = AsShared()](int32 Frame) { return Self->SubmitSuccessorFrame(Frame); },
            [Self = AsShared()](int32 Frame) { return Self->InspectSuccessorFrame(Frame); },
            [Self = AsShared()](FRoomScenarioCommand &Next) { return Self->AfterSuccessorFrames(Next); });
        return true;
    }
    bool SubmitSuccessorFrame(int32 Frame)
    {
        const FString &Current = ActiveSuccessorMatch;
        if (!RefreshCurrentFighterAssignments(*Test, NextRoom, TEXT("f0"), TEXT("f1"), Assignments[0],
                                              Assignments[1]))
            return false;

        ++Tick;
        for (int32 Seat = 0; Seat < 2; ++Seat)
        {
            auto Input = Operation(TEXT("input"), Frame, FString::FromInt(Seat == 0 ? INP_Right | INP_A : 0));
            Input.Match = Current;
            Input.Assignment = Assignments[Seat];
            Test->TestTrue(TEXT("successor accepts only its current input stream"),
                           CheckRoomCommand(*Test, NextRoom, FString::Printf(TEXT("f%d"), Seat), Input,
                                            TEXT("accepted")));
        }
        return true;
    }
    bool InspectSuccessorFrame(int32)
    {
        auto &Next = *NextWorld;

        SuccessorDamage |= Next.Battle->GetMainPlayer(false)->CurrentHealth < 10000;
        return true;
    }
    bool AfterSuccessorFrames(FRoomScenarioCommand &Scenario)
    {
        auto &Next = *NextWorld;
        const FString &Current = ActiveSuccessorMatch;

        SuccessorHealth.Add(Next.Battle->GetMainPlayer(false)->CurrentHealth);
        SuccessorX.Add(Next.Battle->GetMainPlayer(true)->PosX);
        Test->TestEqual(TEXT("alternate character has distinct native health configuration"),
                        Next.Battle->GetMainPlayer(true)->MaxHealth, Generation % 2 == 1 ? 12000 : 10000);
        Test->TestTrue(TEXT("legal combat after stage/character change causes damage"), SuccessorDamage);
        Test->TestTrue(TEXT("legal combat after stage/character change causes movement"),
                       Next.Battle->GetMainPlayer(true)->PosX != -150000);
        Test->TestEqual(TEXT("rematch never changes viewer timeline"),
                        NextRoom->Observe(TEXT("viewer")).Match, Match);
        PreviousMatch = Current;
        if (Generation > 0 && !CaptureRetainedReference())
            return false;
        NextWorld.Reset();
        if (++Generation < 3)
            return PrepareSuccessor(Scenario);
        return Finish(Scenario);
    }
    bool CaptureRetainedReference()
    {
        if (!Test->TestFalse(TEXT("ordinary retained-stage reference requires a real RHI"), GUsingNullRHI))
            return false;
        // Begin() can use a standalone fixture world without map travel. Author
        // the same simple alternate-stage scenery in this reference world only.
        // This runs after combat and does not inspect candidate presentation.
        if (Generation == 1)
        {
            bool HasAuthoredMarker = false;
            for (TActorIterator<AStaticMeshActor> It(NextWorld->World); It; ++It)
                if (It->GetActorLocation().Equals(StageMarkerLocation, 1.e-4) &&
                    It->GetActorScale3D().Equals(StageMarkerScale, 1.e-4) &&
                    It->GetStaticMeshComponent()->GetStaticMesh() &&
                    It->GetStaticMeshComponent()->GetStaticMesh()->GetPathName() == TEXT("/Engine/BasicShapes/Cube.Cube"))
                    HasAuthoredMarker = true;
            if (!HasAuthoredMarker)
            {
                auto *Marker = NextWorld->World->SpawnActor<AStaticMeshActor>(StageMarkerLocation, FRotator::ZeroRotator);
                if (!Test->TestNotNull(TEXT("ordinary alternate-stage reference scenery exists"), Marker))
                    return false;
                Marker->GetStaticMeshComponent()->SetStaticMesh(
                    LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
                Marker->SetActorScale3D(StageMarkerScale);
            }
        }
        auto *Capture = NextWorld->World->SpawnActor<ASceneCapture2D>();
        if (!Test->TestNotNull(TEXT("ordinary retained-stage reference capture exists"), Capture))
            return false;
        auto *Camera = NextWorld->Battle->CameraActor;
        if (!Test->TestNotNull(TEXT("ordinary retained-stage reference camera exists"), Camera))
            return false;
        auto *Component = Capture->GetCaptureComponent2D();
        auto *Target = NewObject<UTextureRenderTarget2D>(Capture);
        Target->InitAutoFormat(960, 540);
        Component->TextureTarget = Target;
        Component->bCaptureEveryFrame = false;
        Component->bCaptureOnMovement = false;
        Component->CaptureSource = SCS_FinalColorLDR;
        Component->FOVAngle = Camera->GetCameraComponent()->FieldOfView;
        Capture->SetActorTransform(Camera->GetActorTransform());
        Component->CaptureScene();
        FlushRenderingCommands();
        return RetainedEvidence.AddSample(*Test, FString::Printf(TEXT("reference%d"), Generation), Target);
    }

    bool Finish(FRoomScenarioCommand &Scenario)
    {
        FinalAuthority = MakeShared<FRoomBattleWorld>();
        FinalRoom = FinalAuthority->Game->GetSubsystem<USpectatorRoom>();
        RetainedDeadline = FPlatformTime::Seconds() + 30.;
        Tick += 240;
        FinalRoom->SetClock([&]() { return Tick; });
        if (!Test->TestTrue(TEXT("last active successor becomes durably interrupted"),
                            FinalRoom->Recover(DurableSlot)))
            return false;
        Tick += 120;
        Test->TestTrue(
            TEXT("expired selected timeline stops with unavailable status"),
            RoomResponseHasEffect(FinalRoom->Observe(TEXT("viewer")), TEXT("history unavailable")));
        auto Expired = Operation(TEXT("select-match"));
        Expired.Match = Match;
        Test->TestTrue(
            TEXT("expired selection is rejected"),
            CheckRoomCommand(*Test, FinalRoom, TEXT("viewer"), Expired, TEXT("history unavailable")));
        if (!Test->TestFalse(TEXT("retained-stage presentation requires a real RHI"), GUsingNullRHI))
            return false;
        return SelectRetained(Scenario);
    }
    bool SelectRetained(FRoomScenarioCommand &Scenario)
    {
        if (RetainedIndex >= Successors.Num())
        {
            RetainedEvidence.Require(TEXT("visible_gameplay"), TEXT("reference1"));
            RetainedEvidence.Require(TEXT("visible_gameplay"), TEXT("reference2"));
            RetainedEvidence.Require(TEXT("visible_gameplay"), TEXT("selected1"));
            RetainedEvidence.Require(TEXT("visible_gameplay"), TEXT("selected2"));
            RetainedEvidence.Require(TEXT("distinct_stages"), TEXT("reference1"), TEXT("reference2"));
            RetainedEvidence.Require(TEXT("same_stage"), TEXT("reference1"), TEXT("selected1"));
            RetainedEvidence.Require(TEXT("same_stage"), TEXT("reference2"), TEXT("selected2"));
            RetainedEvidence.Require(TEXT("same_gameplay"), TEXT("reference1"), TEXT("selected1"));
            RetainedEvidence.Require(TEXT("same_gameplay"), TEXT("reference2"), TEXT("selected2"));
            return RetainedEvidence.Finish(*Test);
        }

        const int32 I = RetainedIndex;
        FBattleData ViewingConfiguration = Config;
        ViewingConfiguration.Stage = I % 2 == 1 ? OtherStage : Stage;
        ViewingConfiguration.PlayerListP1 = {I % 2 == 1 ? Heavy : Config.PlayerListP1[0].Get()};
        FinalRoom->Authenticate(
            TEXT("viewer"), TEXT("secret"),
            USpectatorRoom::InspectContent(ViewingConfiguration, FinalAuthority->Game->BattleVersion));
        auto Select = Operation(TEXT("select-match"));
        Select.Match = Successors[I];
        const auto Delivery = FinalRoom->Command(TEXT("viewer"), Select);
        Test->TestTrue(TEXT("retention selection reports a diagnostic"),
                       !Delivery.Status.TrimStartAndEnd().IsEmpty());
        Test->TestEqual(TEXT("exactly two most recent terminal successors remain"),
                        Delivery.Match == Successors[I], I > 0);
        if (I == 0)
        {
            ++RetainedIndex;
            return SelectRetained(Scenario);
        }
        const int32 ExpectedX = SuccessorX[I], ExpectedHealth = SuccessorHealth[I],
                    ExpectedMaxHealth = SuccessorMaxHealth[I];
        Scenario.AwaitPresentation(FinalRoom, FinalAuthority.ToSharedRef(), Delivery,
            [=](ANightSkyGameState *Battle) {
                return Battle->GetMainPlayer(true)->PosX == ExpectedX &&
                       Battle->GetMainPlayer(false)->CurrentHealth == ExpectedHealth &&
                       Battle->GetMainPlayer(true)->MaxHealth == ExpectedMaxHealth;
            }, true, RetainedDeadline,
            TEXT("retained timeline selection loads its own matching battle"),
            [Self = AsShared()](FRoomScenarioCommand &Next) { return Self->InspectRetained(Next); });
        return true;
    }
    bool InspectRetained(FRoomScenarioCommand &Scenario)
    {
        const int32 I = RetainedIndex;
        auto *Presented = FinalRoom->PresentationBattle();
        if (!Test->TestNotNull(TEXT("matching successor battle exists"), Presented))
            return false;
        Test->TestEqual(TEXT("matching successor reconstructs exact retained frame"),
                        Presented->BattleState.FrameNumber, 40);
        FlushRenderingCommands();
        if (!RetainedEvidence.AddSample(*Test, FString::Printf(TEXT("selected%d"), I),
                                        FinalRoom->PresentationTexture()))
            return false;
        Test->TestEqual(TEXT("retained character configuration matches accepted selection"),
                        Presented->GetMainPlayer(true)->MaxHealth, I % 2 == 1 ? 12000 : 10000);
        Test->TestEqual(TEXT("retained position matches independent original authority"),
                        Presented->GetMainPlayer(true)->PosX, SuccessorX[I]);
        Test->TestEqual(TEXT("retained health matches independent original authority"),
                        Presented->GetMainPlayer(false)->CurrentHealth, SuccessorHealth[I]);
        auto ExportSuccessor = Operation(TEXT("export"));
        ExportSuccessor.Match = Successors[I];
        FRoomMatch Saved;
        const auto Artifact = FinalRoom->Command(TEXT("viewer"), ExportSuccessor);
        if (!Test->TestTrue(TEXT("retained successor exports complete history"),
                            USpectatorRoom::ReadReplay(Artifact.Replay, Saved)))
            return false;
        Test->TestEqual(TEXT("all successor frames including zero survive recovery"),
                        Saved.Frames.Num(), 41);
        Test->TestTrue(TEXT("recovery outcome distinguishes interruption from completed combat"),
                       !Saved.Outcome.TrimStartAndEnd().IsEmpty() &&
                           Saved.Outcome != CompletedOutcome);
        const FBattleData &Original = SuccessorConfigurations[I];
        Test->TestEqual(TEXT("successor export retains its own identity"), Saved.Id, Successors[I]);
        Test->TestEqual(TEXT("successor export retains match start"), Saved.StartTick,
                        SuccessorStartTicks[I]);
        Test->TestTrue(TEXT("successor export retains both original participants"),
                       Saved.Participants.Num() == 2 && Saved.Participants.Contains(TEXT("f0")) &&
                           Saved.Participants.Contains(TEXT("f1")));
        Test->TestTrue(TEXT("successor export retains exact authored content manifest"),
                       Saved.Content.OrderIndependentCompareEqual(SuccessorContent[I]));
        for (const auto &Revision : SuccessorContent[I])
            Test->TestEqual(TEXT("successor export retains each original content revision"),
                            Saved.Content.FindRef(Revision.Key), Revision.Value);
        Test->TestTrue(TEXT("successor export retains original stage and characters"),
                       FSoftObjectPath(Saved.Configuration.Stage) == FSoftObjectPath(Original.Stage) &&
                           Saved.Configuration.PlayerListP1 == Original.PlayerListP1 &&
                           Saved.Configuration.PlayerListP2 == Original.PlayerListP2);
        Test->TestTrue(TEXT("successor export retains original configuration"),
                       Saved.Configuration.Random.GetSeed() == Original.Random.GetSeed() &&
                           Saved.Configuration.ColorIndicesP1 == Original.ColorIndicesP1 &&
                           Saved.Configuration.ColorIndicesP2 == Original.ColorIndicesP2 &&
                           Saved.Configuration.BattleFormat == Original.BattleFormat &&
                           Saved.Configuration.StartRoundTimer == Original.StartRoundTimer &&
                           Saved.Configuration.RoundCount == Original.RoundCount &&
                           Saved.Configuration.TimeUntilRoundStart == Original.TimeUntilRoundStart &&
                           Saved.Configuration.MusicName == Original.MusicName);
        Test->TestEqual(TEXT("successor export retains original training mode"), Saved.Training,
                        SuccessorTraining[I]);
        FRoomBattleWorld ExportPlayback;
        ExportPlayback.Game->BattleData = Saved.Configuration;
        ExportPlayback.Game->IsTraining = Saved.Training;
        ExportPlayback.Begin();
        Test->TestTrue(TEXT("export opens matching native battle class"),
                       ExportPlayback.Battle->GetClass() == Saved.BattleClass);
        Test->AddInfo(FString::Printf(
            TEXT("successor export initialization generation=%d original-training=%d "
                 "saved-training=%d original-frame=%d playback-frame=%d original-timer=%d "
                 "playback-timer=%d original-max-health=%d playback-max-health=%d"),
            I, SuccessorTraining[I], Saved.Training, SuccessorInitialFrames[I],
            ExportPlayback.Battle->BattleState.FrameNumber, SuccessorInitialTimers[I],
            ExportPlayback.Battle->BattleState.RoundTimer, SuccessorMaxHealth[I],
            ExportPlayback.Battle->GetMainPlayer(true)->MaxHealth));
        if (!Test->TestTrue(TEXT("independent battle plays successor export"),
                            USpectatorRoom::PlayFrame(ExportPlayback.Battle, Saved.Frames.Last())))
            return false;
        Test->TestEqual(TEXT("export playback restores original character max health"),
                        ExportPlayback.Battle->GetMainPlayer(true)->MaxHealth, SuccessorMaxHealth[I]);
        Test->TestEqual(TEXT("export playback restores original position"),
                        ExportPlayback.Battle->GetMainPlayer(true)->PosX, SuccessorX[I]);
        Test->TestEqual(TEXT("export playback restores original victim health"),
                        ExportPlayback.Battle->GetMainPlayer(false)->CurrentHealth,
                        SuccessorHealth[I]);
        Test->TestTrue(TEXT("independent export playback uses the original stage"),
                       FSoftObjectPath(CastChecked<AReplayFixtureBattle>(ExportPlayback.Battle)->FixtureInitialConfiguration().Stage) == FSoftObjectPath(Original.Stage));
        ++RetainedIndex;
        return SelectRetained(Scenario);
    }
    TSharedPtr<FRoomBattleWorld> FinalAuthority;
    USpectatorRoom *FinalRoom = nullptr;
    int32 RetainedIndex = 0;
    double RetainedDeadline = 0.;
    FRoomRenderedEvidence RetainedEvidence;
    FAutomationTestBase *Test;
    TSharedPtr<FRoomBattleWorld> InitialWorld = MakeShared<FRoomBattleWorld>();
    TArray<TStrongObjectPtr<UObject>> RetainedFixtureAssets;
    TSharedPtr<FRoomBattleWorld> NextWorld;
    USpectatorRoom *Room = nullptr, *NextRoom = nullptr;
    int64 Tick = 0;
    int32 Generation = 0;
    int32 InitialNativeFrame = 0, InitialNativeTimer = 0, InitialNativePhase = 0, Last = 0;
    int32 DecidingFrame = INDEX_NONE;
    int64 DecidingTick = -1, LastConfirmedTick = 0;
    bool InitialTraining = false, Damage = false, SuccessorDamage = false;
    FString ActiveSuccessorMatch;
    FString DurableSlot, Assignments[2], Match, CompletedOutcome, PreviousMatch;
    UPrimaryCharaData *Chara = nullptr, *Heavy = nullptr;
    UPrimaryStageData *Stage = nullptr, *OtherStage = nullptr;
    FBattleData Config, NextConfiguration;
    TMap<FString, FString> Content, NextContent;
    const FVector StageMarkerLocation = FVector(0, -250, 100), StageMarkerScale = FVector(8, 1, 1);
    TArray<FString> Successors;
    TArray<int32> SuccessorHealth, SuccessorX, SuccessorMaxHealth;
    TArray<FBattleData> SuccessorConfigurations;
    TArray<TMap<FString, FString>> SuccessorContent;
    TArray<int64> SuccessorStartTicks;
    TArray<bool> SuccessorTraining;
    TArray<int32> SuccessorInitialFrames, SuccessorInitialTimers;
};

bool FRoomCompleted::RunTest(const FString &)
{
    auto State = MakeShared<FRoomCompletedScenario>(this);
    ADD_LATENT_AUTOMATION_COMMAND(FRoomScenarioCommand(
        this, [State](FRoomScenarioCommand &Scenario) { return State->Prepare(Scenario); }));
    return true;
}

#endif
#if WITH_DEV_AUTOMATION_TESTS
#include "RoomPythonAutomation.h"
#include "RoomRenderedEvidence.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomNetwork, "NightSky.Room.SixProcessAuthority",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomNetwork::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(
        FRoomPythonCommand(this, TEXT("room_network_scenario.py"), TEXT(""), 1245.));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomGGPOPair, "NightSky.Room.GGPOPair",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomGGPOPair::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FRoomPythonCommand(this, TEXT("room_ggpo_pair.py"), TEXT(""), 900.));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomRenderedDVR, "NightSky.Room.RenderedDVR",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomRenderedDVR::RunTest(const FString &)
{
    return RunRoomBattleDelay(*this, true, true);
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomCrashWindows, "NightSky.Room.AuthorityCrashWindows",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomCrashWindows::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FRoomPythonCommand(this, TEXT("room_crash_windows.py"), TEXT(""), 900.));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomEndpointWidgets, "NightSky.Room.EndpointWidgets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomEndpointWidgets::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(FRoomPythonCommand(this, TEXT("room_endpoint_widgets.py"), TEXT(""), 285.));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomLifecycle, "NightSky.Room.LifecycleAndSeatTransfer",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomLifecycle::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(
        FRoomPythonCommand(this, TEXT("room_lifecycle_scenario.py"), TEXT(""), 1545.));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomFuzzCampaign, "NightSky.Room.FuzzMetamorphicCampaign",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomFuzzCampaign::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(
        FRoomPythonCommand(this, TEXT("room_fuzz_campaign.py"), TEXT("--campaign-budget 480"), 525.));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomCombinedLifecycle, "NightSky.Room.CombinedLifecycleFuzz",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoomCombinedLifecycle::RunTest(const FString &)
{
    ADD_LATENT_AUTOMATION_COMMAND(
        FRoomPythonCommand(this, TEXT("room_lifecycle_scenario.py"),
                           TEXT("--combined --combined-per-phase 5 --driver-deadline 600"), 720.));
    return true;
}
#endif
