// Device-independent observations of the native common-effect contract. These
// reward checks do not claim to render HLSL; HlslParticleFence supplies the real
// material, scene capture, intermediate samples, and pixel positive controls.
namespace ReplayTests
{
class FCommonParticleJournal final : public ICommonParticleRenderer
{
public:
    explicit FCommonParticleJournal(UWorld*) {}
    int32 Requests = 0;
    TArray<float> Ages;
    virtual bool Spawn(const FCommonParticleRequest&) override
    {
        ++Requests;
        Ages.Add(0.0f);
        return true;
    }
    virtual void Advance(float DeltaSeconds) override
    {
        for (float& Age : Ages) { Age += DeltaSeconds; }
        Ages.RemoveAll([](float Age) { return Age >= 0.5f; });
    }
    virtual void Clear() override { Ages.Reset(); }
};

FScopedCommonRenderer<FCommonParticleJournal> InstallParticleJournal(FReplayBattleWorld& Session)
{
    return FScopedCommonRenderer<FCommonParticleJournal>(Session);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayCommonParticleFence,
    "UnrealBench.ReplayTakeover.CommonParticleFence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FReplayCommonParticleFence::RunTest(const FString&)
{
    using namespace ReplayTests;
    for (bool Rewind : {false, true})
    {
        auto* Source = HlslEffectTape();
        FReplayBattleWorld Ordinary(Source), Seeked(Source);
        auto OrdinaryFx = InstallParticleJournal(Ordinary);
        auto SeekFx = InstallParticleJournal(Seeked);
        const int32 InitialHealth = Ordinary.Battle->GetMainPlayer(false)->CurrentHealth;
        bool SawOrdinaryDamage = false;
        for (int32 Frame = 0; Frame < 60; ++Frame)
        {
            Ordinary.Tick();
            SawOrdinaryDamage |= Ordinary.Battle->GetMainPlayer(false)->CurrentHealth < InitialHealth;
        }
        TestTrue(TEXT("ordinary native hit reaches common renderer"), OrdinaryFx->Requests > 0);
        // Training restores health after recovery. Observe damage on the hit
        // frames, not only after the effect's full lifetime has elapsed.
        TestTrue(TEXT("native hit changes actual battle health"), SawOrdinaryDamage);
        TestEqual(TEXT("ordinary common effects expire"), OrdinaryFx->Ages.Num(), 0);
        if (Rewind)
        {
            for (int32 Frame = 0; Frame < 120; ++Frame) { Seeked.Tick(); }
        }
        const int32 BeforeSeek = SeekFx->Requests;
        TestTrue(TEXT("common-effect seek accepted"), Seeked.Game->SeekReplay(60));
        TestEqual(TEXT("seek emits no skipped common effects"), SeekFx->Requests, BeforeSeek);
        TestEqual(TEXT("seek leaves no expired common effects"), SeekFx->Ages.Num(), 0);
        TestTrue(TEXT("effect suppression preserves actual battle outcome"),
                 FReplaySnapshot::Read(Seeked) == FReplaySnapshot::Read(Ordinary));
        if (!TestTrue(TEXT("common-effect takeover accepted"), Seeked.Game->TakeOverReplay(0))) { return false; }
        for (int32 Frame = 0; Frame < 40; ++Frame)
        {
            Seeked.Tick();
            TestEqual(TEXT("quiet continuation emits no deferred common effects"), SeekFx->Requests, BeforeSeek);
            TestEqual(TEXT("quiet continuation contains no skipped common effects"), SeekFx->Ages.Num(), 0);
        }
        const int32 BeforeNewAttack = SeekFx->Requests;
        const int32 HealthBeforeNewAttack = Seeked.Battle->GetMainPlayer(false)->CurrentHealth;
        bool SawNewDamage = false;
        for (int32 Frame = 0; Frame < 80; ++Frame)
        {
            Seeked.Tick(Frame < 20 ? INP_A : 0, 0);
            SawNewDamage |= Seeked.Battle->GetMainPlayer(false)->CurrentHealth < HealthBeforeNewAttack;
        }
        TestTrue(TEXT("new native attack resumes common-effect delivery"), SeekFx->Requests > BeforeNewAttack);
        TestTrue(TEXT("new attack damages the actual defender"), SawNewDamage);
        TestEqual(TEXT("resumed common effects expire"), SeekFx->Ages.Num(), 0);
    }
    for (int32 TargetMode = 0; TargetMode < 4; ++TargetMode)
    {
        auto* Source = HlslEffectTape();
        FReplayBattleWorld Ordinary(Source), Seeked(Source);
        auto OrdinaryFx = InstallParticleJournal(Ordinary);
        auto SeekFx = InstallParticleJournal(Seeked);
        for (int32 Frame = 0; Frame < 20 && SeekFx->Requests == 0; ++Frame)
        {
            Ordinary.Tick(); Seeked.Tick();
        }
        if (!TestTrue(TEXT("pre-seek common effect exists"), SeekFx->Ages.Num() > 0)) { return false; }
        for (int32 Frame = 0; Frame < 3; ++Frame) { Ordinary.Tick(); Seeked.Tick(); }
        if (!TestTrue(TEXT("aged pre-seek common effect exists"), SeekFx->Ages.Num() > 0)) { return false; }
        TestTrue(TEXT("no-op control starts at nonzero effect age"), SeekFx->Ages[0] > 0.0f);
        const TArray<float> OriginalAges = SeekFx->Ages;
        const int32 OriginalRequests = SeekFx->Requests;
        const int32 Position = Seeked.Game->GetReplayPosition();
        TestFalse(TEXT("invalid seek rejected with active effects"), Seeked.Game->SeekReplay(-1));
        TestTrue(TEXT("invalid seek preserves active effect ages"), SeekFx->Ages == OriginalAges);
        TestTrue(TEXT("no-op seek with active effect accepted"), Seeked.Game->SeekReplay(Position));
        TestTrue(TEXT("no-op seek preserves active effect ages"), SeekFx->Ages == OriginalAges);
        TestEqual(TEXT("no-op seek does not re-emit active effects"), SeekFx->Requests, OriginalRequests);
        if (TargetMode == 3)
        {
            TestFalse(TEXT("fresh invalid source rejected without clearing effects"), Seeked.Game->BeginReplaySession(nullptr));
            TestTrue(TEXT("rejected fresh source preserves effects"), SeekFx->Ages == OriginalAges);
            TestTrue(TEXT("fresh same-world replay accepted"), Seeked.Game->BeginReplaySession(HlslEffectTape()));
            TestEqual(TEXT("fresh same-world replay clears old common effects"), SeekFx->Ages.Num(), 0);
            TestEqual(TEXT("fresh same-world replay starts at zero"), Seeked.Game->GetReplayPosition(), 0);
            const int32 BeforeFreshAttack = SeekFx->Requests;
            for (int32 Frame = 0; Frame < 20; ++Frame) { Seeked.Tick(); }
            TestTrue(TEXT("fresh replay still delivers native effects"), SeekFx->Requests > BeforeFreshAttack);
            continue;
        }
        const int32 Target = TargetMode == 0 ? Position + 3 : (TargetMode == 1 ? 60 : 0);
        if (TargetMode < 2)
        {
            for (int32 Frame = 0; Frame < 160 && Ordinary.Game->GetReplayPosition() < Target; ++Frame)
            {
                Ordinary.Tick();
            }
            TestEqual(TEXT("ordinary effect control reaches its destination"), Ordinary.Game->GetReplayPosition(), Target);
        }
        TestTrue(TEXT("existing common-effect seek accepted"), Seeked.Game->SeekReplay(Target));
        if (TargetMode == 0)
        {
            TestEqual(TEXT("short seek retains live effect inventory"), SeekFx->Ages.Num(), OrdinaryFx->Ages.Num());
            TestTrue(TEXT("ordinary short-forward effect remains alive"), OrdinaryFx->Ages.Num() > 0);
            for (int32 Index = 0; Index < FMath::Min(SeekFx->Ages.Num(), OrdinaryFx->Ages.Num()); ++Index)
            {
                TestTrue(TEXT("short seek advances existing effect age to destination"),
                         FMath::IsNearlyEqual(SeekFx->Ages[Index], OrdinaryFx->Ages[Index], 0.00001f));
            }
        }
        else
        {
            TestEqual(TargetMode == 1 ? TEXT("forward seek expires old effects")
                                      : TEXT("rewind clears effects from the old timeline"), SeekFx->Ages.Num(), 0);
        }
        if (TargetMode < 2)
        {
            TestTrue(TEXT("effect lifetime handling preserves gameplay"),
                     FReplaySnapshot::Read(Seeked) == FReplaySnapshot::Read(Ordinary));
        }
    }
    return true;
}
