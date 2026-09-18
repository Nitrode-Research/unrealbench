// Included by ReplayTakeoverTests.cpp so gameplay comparisons use its existing
// complete replay snapshots and real native battle/controller fixture.
#include "ReplayHlslFixture.h"
#include "NightSkyEngine/Battle/Actors/ParticleManager.h"
#include "UObject/StrongObjectPtr.h"

namespace ReplayTests
{
class FObservedHlslRenderer final : public ICommonParticleRenderer
{
public:
    explicit FObservedHlslRenderer(UWorld* World) : Visuals(World) {}
    FReplayHlslFixture Visuals;
    bool bSampleSpawns = false;
    int32 FailedSpawns = 0;
    TArray<FIntPoint> SpawnPixelSamples; // Readback size and lit RGB pixels.

    virtual bool Spawn(const FCommonParticleRequest& Request) override
    {
        if (!Visuals.Spawn(Request.Transform.GetLocation())) { ++FailedSpawns; }
        if (bSampleSpawns)
        {
            // An actual render/readback at the renderer boundary witnesses
            // bursts even if a later operation clears their scene components.
            const auto Pixels = Visuals.Capture();
            SpawnPixelSamples.Add(FIntPoint(Pixels.Num(), LitPixelCount(Pixels)));
        }
        return true;
    }
    virtual void Advance(float DeltaSeconds) override { Visuals.Advance(DeltaSeconds); }
    virtual void Clear() override { Visuals.Clear(); }
};

template <typename T> class FScopedCommonRenderer
{
    TWeakObjectPtr<UWorld> World;
    TSharedPtr<T> Renderer;
    TArray<TStrongObjectPtr<AParticleManager>> Managers;
    FDelegateHandle SpawnHandle;
    void Bind(AParticleManager* Manager)
    {
        if (Manager)
        {
            Managers.Emplace(Manager);
            Manager->CommonParticleRenderer = Renderer;
        }
    }
public:
    explicit FScopedCommonRenderer(FReplayBattleWorld& Session)
        : World(Session.World), Renderer(MakeShared<T>(Session.World))
    {
        Bind(Session.Battle->ParticleManager);
        // The current native manager has no BeginPlay effect work. This hook
        // follows manager replacement before later battle updates, without
        // requiring the original manager's identity or a post-seek rebind.
        SpawnHandle = Session.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda(
            [this](AActor* Actor) { Bind(Cast<AParticleManager>(Actor)); }));
    }
    ~FScopedCommonRenderer()
    {
        if (World.IsValid()) { World->RemoveOnActorSpawnedHandler(SpawnHandle); }
        for (const auto& Manager : Managers)
        {
            if (Manager->CommonParticleRenderer == Renderer) { Manager->CommonParticleRenderer.Reset(); }
        }
        // Release renderer resources at fixture scope exit, while the borrowed
        // world is still usable, including on a failing assertion/early return.
        Renderer.Reset();
    }
    FScopedCommonRenderer(const FScopedCommonRenderer&) = delete;
    FScopedCommonRenderer& operator=(const FScopedCommonRenderer&) = delete;
    T* operator->() const { return Renderer.Get(); }
    T& operator*() const { return *Renderer; }
};

FScopedCommonRenderer<FObservedHlslRenderer> InstallHlslRenderer(FReplayBattleWorld& Session)
{
    return FScopedCommonRenderer<FObservedHlslRenderer>(Session);
}

UReplaySaveInfo* HlslEffectTape()
{
    auto* Source = CreateReplayTape(160);
    for (int32 Index = 0; Index < Source->LengthInFrames; ++Index)
    {
        Source->InputsP1[Index] = Index < 20 ? INP_A : 0;
        Source->InputsP2[Index] = 0;
    }
    return Source;
}

int32 HlslPixelDifference(const TArray<FColor>& A, const TArray<FColor>& B)
{
    if (A.Num() != B.Num()) { return MAX_int32; }
    int32 Different = 0;
    for (int32 Index = 0; Index < A.Num(); ++Index)
    {
        const FColor X = A[Index], Y = B[Index];
        if (FMath::Abs(int32(X.R) - int32(Y.R)) > 8 ||
            FMath::Abs(int32(X.G) - int32(Y.G)) > 8 ||
            FMath::Abs(int32(X.B) - int32(Y.B)) > 8) { ++Different; }
    }
    return Different;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayHlslParticles,
    "UnrealBenchIntegration.ReplayTakeover.HlslParticleFence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayHlslParticles::RunTest(const FString&)
{
    using namespace ReplayTests;
    const auto Capture = [this](FObservedHlslRenderer& Renderer, const FString& Label)
    {
        const auto Pixels = Renderer.Visuals.Capture();
        TestEqual(Label + TEXT(" readback size"), Pixels.Num(), 128 * 128);
        const int32 Lit = LitPixelCount(Pixels);
        AddInfo(FString::Printf(TEXT("HLSL_PIXELS %s lit=%d"), *Label, Lit));
        if (Pixels.Num() == 128 * 128)
        {
            TestTrue(Label + TEXT(" bitmap evidence"), FFileHelper::CreateBitmap(
                *(FPaths::ProjectSavedDir() / (TEXT("NSE122_HLSL_") + Label + TEXT(".bmp"))),
                128, 128, Pixels.GetData()));
        }
        return Pixels;
    };
    const auto RequireBlank = [this](const TArray<FColor>& Pixels, const TCHAR* Label)
    {
        TestTrue(Label, Pixels.Num() == 128 * 128 && LitPixelCount(Pixels) <= 2);
    };
    const auto RequireBurst = [this](const TArray<FColor>& Pixels, const TCHAR* Label)
    {
        const int32 Lit = LitPixelCount(Pixels);
        TestTrue(Label, Pixels.Num() == 128 * 128 && Lit >= 12 && Lit < 4096);
    };
    for (bool Rewind : {false, true})
    {
        const FString Prefix = Rewind ? TEXT("rewind_") : TEXT("forward_");
        auto* Source = HlslEffectTape();
        FReplayBattleWorld Ordinary(Source), Seeked(Source);
        auto OrdinaryFx = InstallHlslRenderer(Ordinary);
        auto SeekFx = InstallHlslRenderer(Seeked);
        if (!TestTrue(TEXT("ordinary HLSL material and scene are ready"), OrdinaryFx->Visuals.IsReady()) ||
            !TestTrue(TEXT("seek HLSL material and scene are ready"), SeekFx->Visuals.IsReady()))
        {
            return false;
        }
        RequireBlank(Capture(*OrdinaryFx, Prefix + TEXT("initial")), TEXT("effect-free image is black"));
        const int32 InitialHealth = Ordinary.Battle->GetMainPlayer(false)->CurrentHealth;
        bool SawHit = false;
        int32 FirstHitPosition = -1;
        for (int32 Frame = 0; Frame < 60; ++Frame)
        {
            Ordinary.Tick();
            if (!SawHit && Ordinary.Battle->GetMainPlayer(false)->CurrentHealth < InitialHealth)
            {
                SawHit = true;
                FirstHitPosition = Ordinary.Game->GetReplayPosition();
                TestEqual(TEXT("native attack deals fixture damage"),
                          InitialHealth - Ordinary.Battle->GetMainPlayer(false)->CurrentHealth, 500);
                RequireBurst(Capture(*OrdinaryFx, Prefix + TEXT("ordinary_hit")),
                             TEXT("ordinary native hit renders real HLSL sparks"));
            }
        }
        TestTrue(TEXT("ordinary native combat positive control"), SawHit);
        AddInfo(FString::Printf(TEXT("HLSL_FIRST_HIT %s position=%d"), *Prefix, FirstHitPosition));
        const auto OrdinaryDestination = Capture(*OrdinaryFx, Prefix + TEXT("ordinary_expired"));
        RequireBlank(OrdinaryDestination, TEXT("ordinary burst expires before destination 60"));
        if (Rewind)
        {
            // Reach a quiet later point through real ordinary playback. No
            // test-side clear is allowed to conceal pre-seek effect state.
            for (int32 Frame = 0; Frame < 120; ++Frame) { Seeked.Tick(); }
            RequireBlank(Capture(*SeekFx, Prefix + TEXT("before_seek")),
                         TEXT("ordinary effects have expired before backward seek"));
        }
        SeekFx->bSampleSpawns = true;
        const bool Accepted = Seeked.Game->SeekReplay(60);
        SeekFx->bSampleSpawns = false;
        TestTrue(TEXT("HLSL destination seek accepted"), Accepted);
        TestTrue(TEXT("HLSL seek preserves actual destination gameplay"),
                 FReplaySnapshot::Read(Seeked) == FReplaySnapshot::Read(Ordinary));
        TestEqual(TEXT("seek does not submit skipped HLSL bursts"), SeekFx->SpawnPixelSamples.Num(), 0);
        for (FIntPoint Sample : SeekFx->SpawnPixelSamples)
        {
            AddInfo(FString::Printf(TEXT("HLSL_SKIPPED_RENDER %s size=%d lit=%d"), *Prefix, Sample.X, Sample.Y));
            TestEqual(TEXT("skipped-render witness is an actual complete readback"), Sample.X, 128 * 128);
            TestEqual(TEXT("no sampled skipped HLSL output"), Sample.Y, 0);
        }
        const auto Destination = Capture(*SeekFx, Prefix + TEXT("destination"));
        RequireBlank(Destination, TEXT("skipped HLSL bursts are absent at destination"));
        TestTrue(TEXT("expired destination image matches ordinary playback"),
                 HlslPixelDifference(Destination, OrdinaryDestination) <= 2);

        if (!TestTrue(TEXT("HLSL branch takeover accepted"), Seeked.Game->TakeOverReplay(0))) { return false; }
        for (int32 Frame = 0; Frame < 40; ++Frame)
        {
            Seeked.Tick();
            // Every quiet live frame is captured; a late queued emission must
            // not escape because only the final resumed image was checked.
            const auto Pixels = SeekFx->Visuals.Capture();
            TestTrue(FString::Printf(TEXT("quiet resumed HLSL frame %d is blank"), Frame),
                     Pixels.Num() == 128 * 128 && LitPixelCount(Pixels) <= 2);
        }
        RequireBlank(Capture(*SeekFx, Prefix + TEXT("quiet_end")), TEXT("quiet continuation stays effect-free"));
        const int32 HealthBeforeNewAttack = Seeked.Battle->GetMainPlayer(false)->CurrentHealth;
        bool NewHit = false;
        for (int32 Frame = 0; Frame < 80; ++Frame)
        {
            Seeked.Tick(Frame < 20 ? INP_A : 0, 0);
            if (!NewHit && Seeked.Battle->GetMainPlayer(false)->CurrentHealth < HealthBeforeNewAttack)
            {
                NewHit = true;
                RequireBurst(Capture(*SeekFx, Prefix + TEXT("new_live_hit")),
                             TEXT("new live attack resumes HLSL output"));
            }
        }
        TestTrue(TEXT("fresh native hit positive control after quiet continuation"), NewHit);
        RequireBlank(Capture(*SeekFx, Prefix + TEXT("new_live_expired")), TEXT("resumed burst expires normally"));
        TestEqual(TEXT("ordinary renderer accepts every native spawn"), OrdinaryFx->FailedSpawns, 0);
        TestEqual(TEXT("seek renderer accepts every native spawn"), SeekFx->FailedSpawns, 0);
    }

    // A correct suppression implementation must also preserve normal effects
    // already alive when seek starts. This catches indiscriminate clearing and
    // tests age advancement without reconstructing skipped historical bursts.
    for (int32 Mode = 0; Mode < 4; ++Mode)
    {
        auto* Source = HlslEffectTape();
        FReplayBattleWorld Ordinary(Source), Seeked(Source);
        auto OrdinaryFx = InstallHlslRenderer(Ordinary);
        auto SeekFx = InstallHlslRenderer(Seeked);
        if (!TestTrue(TEXT("active-effect ordinary HLSL fixture ready"), OrdinaryFx->Visuals.IsReady()) ||
            !TestTrue(TEXT("active-effect seek HLSL fixture ready"), SeekFx->Visuals.IsReady())) { return false; }
        const int32 Health = Seeked.Battle->GetMainPlayer(false)->CurrentHealth;
        for (int32 Frame = 0; Frame < 20 && Seeked.Battle->GetMainPlayer(false)->CurrentHealth == Health; ++Frame)
        {
            Ordinary.Tick(); Seeked.Tick();
        }
        const FString Prefix = FString::Printf(TEXT("active_%d_"), Mode);
        const auto Birth = Capture(*SeekFx, Prefix + TEXT("birth"));
        RequireBurst(Birth, TEXT("newly born effect is visibly alive"));
        for (int32 Frame = 0; Frame < 3; ++Frame) { Ordinary.Tick(); Seeked.Tick(); }
        const auto Before = Capture(*SeekFx, Prefix + TEXT("before"));
        RequireBurst(Before, TEXT("pre-seek effect is visibly alive at a nonzero age"));
        TestTrue(TEXT("pre-seek age changes the birth pattern"), HlslPixelDifference(Birth, Before) > 8);
        const int32 Position = Seeked.Game->GetReplayPosition();
        TestTrue(TEXT("no-op seek accepted"), Seeked.Game->SeekReplay(Position));
        TestTrue(TEXT("no-op seek preserves live effect pixels and age"),
                 HlslPixelDifference(Before, Capture(*SeekFx, Prefix + TEXT("noop"))) <= 2);
        if (Mode == 3)
        {
            TestFalse(TEXT("invalid fresh replay source rejected"), Seeked.Game->BeginReplaySession(nullptr));
            TestTrue(TEXT("rejected fresh session preserves active HLSL presentation"),
                     HlslPixelDifference(Before, Capture(*SeekFx, Prefix + TEXT("rejected_session"))) <= 2);
            TestTrue(TEXT("fresh replay accepted in same world"), Seeked.Game->BeginReplaySession(HlslEffectTape()));
            TestEqual(TEXT("fresh replay starts at position zero"), Seeked.Game->GetReplayPosition(), 0);
            RequireBlank(Capture(*SeekFx, Prefix + TEXT("fresh_session")),
                         TEXT("fresh same-world session clears previous HLSL bursts"));
            for (int32 Frame = 0; Frame < 4; ++Frame) { Seeked.Tick(); }
            RequireBurst(Capture(*SeekFx, Prefix + TEXT("fresh_hit")),
                         TEXT("fresh same-world session can render new native hits"));
            continue;
        }
        const int32 Target = Mode == 0 ? Position + 3 : (Mode == 1 ? 60 : 0);
        if (Mode < 2)
        {
            for (int32 Frame = 0; Frame < 160 && Ordinary.Game->GetReplayPosition() < Target; ++Frame)
            {
                Ordinary.Tick();
            }
            TestEqual(TEXT("ordinary active-effect control reaches its destination"), Ordinary.Game->GetReplayPosition(), Target);
        }
        TestTrue(TEXT("seek with existing HLSL effect accepted"), Seeked.Game->SeekReplay(Target));
        const auto After = Capture(*SeekFx, Prefix + TEXT("after"));
        if (Mode == 0)
        {
            const auto Expected = Capture(*OrdinaryFx, Prefix + TEXT("expected"));
            RequireBurst(Expected, TEXT("ordinary short-forward effect remains visible"));
            TestTrue(TEXT("short forward seek ages an existing HLSL effect to the ordinary result"),
                     HlslPixelDifference(After, Expected) <= 2);
            TestTrue(TEXT("age positive control changes the rendered spark pattern"),
                     HlslPixelDifference(Before, Expected) > 8);
        }
        else
        {
            RequireBlank(After, Mode == 1 ? TEXT("forward seek expires an existing HLSL effect")
                                         : TEXT("rewind before birth removes future HLSL effects"));
        }
        if (Mode < 2)
        {
            TestTrue(TEXT("existing-effect seek preserves actual battle state"),
                     FReplaySnapshot::Read(Seeked) == FReplaySnapshot::Read(Ordinary));
        }
        else
        {
            TestEqual(TEXT("rewind restores pre-hit health"), Seeked.Battle->GetMainPlayer(false)->CurrentHealth, Health);
        }
    }
    return true;
}
