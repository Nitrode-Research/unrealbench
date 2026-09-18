// Device-independent observation of common one-shot audio. The rendered
// PresentationFence keeps the real-device positive control; this reward test
// watches the same presentation boundary through AAudioManager's native
// observer, which reports what the engine actually started or stopped and so
// works under -NoSound.
#include "NightSkyEngine/Battle/Actors/AudioManager.h"

namespace ReplayTests
{
class FCommonAudioJournal final : public ICommonAudioObserver
{
public:
    int32 Starts = 0;
    int32 Stops = 0;
    virtual void Started(int32, USoundBase*, float) override { ++Starts; }
    virtual void Stopped(int32) override { ++Stops; }
};

class FScopedAudioJournal
{
    TWeakObjectPtr<UWorld> World;
    TSharedPtr<FCommonAudioJournal> Journal;
    TArray<TStrongObjectPtr<AAudioManager>> Managers;
    FDelegateHandle SpawnHandle;
    void Bind(AAudioManager* Manager)
    {
        if (Manager)
        {
            Managers.Emplace(Manager);
            Manager->CommonAudioObserver = Journal;
        }
    }

public:
    explicit FScopedAudioJournal(FReplayBattleWorld& Session)
        : World(Session.World), Journal(MakeShared<FCommonAudioJournal>())
    {
        Bind(Session.Battle->AudioManager);
        // Follow manager replacement across a seek without requiring the
        // original manager's identity or a post-seek rebind.
        SpawnHandle = Session.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda(
            [this](AActor* Actor) { Bind(Cast<AAudioManager>(Actor)); }));
    }
    ~FScopedAudioJournal()
    {
        if (World.IsValid()) { World->RemoveOnActorSpawnedHandler(SpawnHandle); }
        for (const auto& Manager : Managers)
        {
            if (Manager->CommonAudioObserver == Journal) { Manager->CommonAudioObserver.Reset(); }
        }
        Journal.Reset();
    }
    FScopedAudioJournal(const FScopedAudioJournal&) = delete;
    FScopedAudioJournal& operator=(const FScopedAudioJournal&) = delete;
    FCommonAudioJournal* operator->() const { return Journal.Get(); }
};
} // namespace ReplayTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayHeadlessAudioFence,
    "UnrealBench.ReplayTakeover.HeadlessAudioFence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayHeadlessAudioFence::RunTest(const FString&)
{
    using namespace ReplayTests;
    for (bool Rewind : {false, true})
    {
        AddInfo(Rewind ? TEXT("backward headless audio catch-up")
                       : TEXT("forward headless audio catch-up"));
        auto* Source = CreateReplayTape(30);
        Source->InputsP1[0] |= INP_A | INP_B;
        FReplayBattleWorld Ordinary(Source), Seeked(Source);
        FScopedAudioJournal OrdinaryAudio(Ordinary);
        FScopedAudioJournal SeekedAudio(Seeked);
        for (int32 Frame = 0; Frame < 10; ++Frame)
        {
            Ordinary.Tick();
        }
        if (!TestTrue(TEXT("ordinary common audio reaches the native observer"), OrdinaryAudio->Starts > 0))
        {
            return false;
        }
        if (Rewind)
        {
            for (int32 Frame = 0; Frame < 30; ++Frame) { Seeked.Tick(); }
        }
        const int32 BeforeSeek = SeekedAudio->Starts;
        if (!TestTrue(TEXT("headless audio seek accepted"), Seeked.Game->SeekReplay(10)))
        {
            return false;
        }
        TestEqual(TEXT("seek emits no skipped audio"), SeekedAudio->Starts, BeforeSeek);
        TestTrue(TEXT("audio suppression preserves the destination"),
                 FReplaySnapshot::Read(Seeked) == FReplaySnapshot::Read(Ordinary));
        if (!TestTrue(TEXT("takeover after headless audio seek"), Seeked.Game->TakeOverReplay(0)))
        {
            return false;
        }
        for (int32 Frame = 0; Frame < 30; ++Frame)
        {
            Seeked.Tick();
        }
        // The journal only counts up, so one assertion after the loop says the
        // same thing as one per frame without thirty duplicate log lines.
        TestEqual(TEXT("quiet continuation emits no deferred skipped audio"),
                  SeekedAudio->Starts, BeforeSeek);
        const int32 BeforeNewAttack = SeekedAudio->Starts;
        for (int32 Frame = 0; Frame < 60; ++Frame)
        {
            Seeked.Tick(Frame < 20 ? INP_A : 0, 0);
        }
        TestTrue(TEXT("new attack resumes audio presentation"), SeekedAudio->Starts > BeforeNewAttack);
    }
    return true;
}
