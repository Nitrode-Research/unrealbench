// UE-0161: direct pixel evidence of the source-owned material and descriptor path.
#include "AssetCompilingManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Presentation/RTSWorldOverlayActor.h"
#include "Presentation/RTSWorldOverlaySubsystem.h"
#include "RenderingThread.h"
#include "Units/RTSTeams.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace Task0161Render
{
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    FWorld() { if (World && GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
    ~FWorld() { if (World) { World->DestroyWorld(false); if (GEngine) GEngine->DestroyWorldContext(World); } }
};
FRTSWorldOverlayDescriptor Descriptor()
{
    FRTSWorldOverlayDescriptor D;
    D.Mode=ERTSWorldOverlayMode::PlacementFootprint; D.SourceKind=ERTSWorldOverlaySourceKind::Showcase;
    D.StableSourceId=161; D.HalfExtent=FVector2D(500,350); D.Radius=500;
    D.TeamId=RTSTeams::Player; D.Validity=ERTSWorldOverlayValidity::Valid;
    return D;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0161OverlayRefusals,"Task0169.Headless.Task0161.Overlay.RefusalsAndOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FTask0161OverlayRefusals::RunTest(const FString&)
{
    Task0161Render::FWorld Fixture;
    auto* O=Fixture.World ? Fixture.World->GetSubsystem<URTSWorldOverlaySubsystem>() : nullptr;
    if (!TestNotNull(TEXT("Overlay subsystem exists"),O)) return false;
    auto D=Task0161Render::Descriptor();
    const auto Good=O->Submit(D);
    TestTrue(TEXT("Valid descriptor accepted"),Good.bAccepted);
    auto Bad=D; Bad.StableSourceId=INDEX_NONE;
    TestFalse(TEXT("Missing source rejected"),O->Submit(Bad).bAccepted);
    Bad=D; Bad.TeamId=FGenericTeamId::NoTeam;
    TestFalse(TEXT("Missing faction rejected"),O->Submit(Bad).bAccepted);
    Bad=D; Bad.LifetimeSeconds=-0.1f;
    TestFalse(TEXT("Negative lifetime rejected"),O->Submit(Bad).bAccepted);
    Bad=D; Bad.HalfExtent.Y=0;
    TestFalse(TEXT("Degenerate rectangle rejected"),O->Submit(Bad).bAccepted);
    Bad=D; Bad.Mode=ERTSWorldOverlayMode::BuildArea; Bad.Radius=0;
    TestFalse(TEXT("Degenerate circle rejected"),O->Submit(Bad).bAccepted);
    TestEqual(TEXT("Rejected submissions do not delete valid existing source"),O->GetSnapshots().Num(),1);
    D.Mode=ERTSWorldOverlayMode::BuildArea;
    const auto Second=O->Submit(D);
    TestTrue(TEXT("Same source can own another mode"),Second.bAccepted && Second.Handle.Value!=Good.Handle.Value);
    D.Radius=730;
    TestEqual(TEXT("Updates reuse identity"),O->Submit(D).Handle.Value,Second.Handle.Value);
    const auto All=O->GetSnapshots();
    TestTrue(TEXT("Snapshots are stable handle order"),All.Num()==2 && All[0].Handle.Value<All[1].Handle.Value);
    TestEqual(TEXT("Removing owner removes both modes"),O->RemoveSource(D.SourceKind,D.StableSourceId),2);
    TestFalse(TEXT("Repeated handle removal is a no-op"),O->Remove(Good.Handle));
    TestEqual(TEXT("No stale snapshots survive"),O->GetSnapshots().Num(),0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTask0161OverlayPixels,"Task0169.Rendered.Task0161.Overlay.MaterialPixels",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FTask0161OverlayPixels::RunTest(const FString&)
{
    if (FParse::Param(FCommandLine::Get(),TEXT("NullRHI"))) { AddError(TEXT("Real RHI required for pixel contract")); return false; }
    Task0161Render::FWorld Fixture;
    if (!TestNotNull(TEXT("Render world exists"),Fixture.World)) return false;
    auto* Overlay=Fixture.World->SpawnActor<ARTSWorldOverlayActor>();
    auto* Camera=Fixture.World->SpawnActor<ASceneCapture2D>();
    if (!TestNotNull(TEXT("Actual overlay actor exists"),Overlay) || !TestNotNull(TEXT("Capture camera exists"),Camera)) return false;
    auto* Target=NewObject<UTextureRenderTarget2D>();
    Target->RenderTargetFormat=RTF_RGBA8; Target->ClearColor=FLinearColor::Black;
    Target->InitAutoFormat(512,512); Target->UpdateResourceImmediate(true);
    auto* Capture=Camera->GetCaptureComponent2D();
    Camera->SetActorLocation(FVector(0,0,2000)); Camera->SetActorRotation(FRotator(-90,0,0));
    Capture->ProjectionType=ECameraProjectionMode::Orthographic; Capture->OrthoWidth=1400;
    Capture->TextureTarget=Target; Capture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;
    Capture->bCaptureEveryFrame=false; Capture->bCaptureOnMovement=false;
    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->ShowOnlyActorComponents(Overlay);
    Capture->PostProcessSettings.bOverride_AutoExposureMethod=true;
    Capture->PostProcessSettings.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    Capture->PostProcessSettings.bOverride_AutoExposureBias=true;
    Capture->PostProcessSettings.AutoExposureBias=0;
    auto Render=[&](const FRTSWorldOverlayDescriptor& D,TArray<FColor>& Pixels)
    {
        if (!TestTrue(TEXT("Actor accepts render descriptor"),Overlay->ApplyDescriptor(D))) return false;
        FAssetCompilingManager::Get().FinishAllCompilation();
        Fixture.World->SendAllEndOfFrameUpdates();
        Capture->CaptureScene(); FlushRenderingCommands();
        return TestTrue(TEXT("Scene pixels can be read"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))
            && TestEqual(TEXT("All scene pixels present"),Pixels.Num(),512*512);
    };
    auto D=Task0161Render::Descriptor();
    TArray<FColor> Valid,Invalid,Small,Large;
    if (!Render(D,Valid)) return false;
    D.Validity=ERTSWorldOverlayValidity::Invalid;
    if (!Render(D,Invalid)) return false;
    int32 Green=0,Red=0,Changed=0;
    for (int32 I=0;I<Valid.Num();++I)
    {
        if (Valid[I].G>Valid[I].R+20 && Valid[I].G>40) ++Green;
        if (Invalid[I].R>Invalid[I].G+20 && Invalid[I].R>40) ++Red;
        if (Valid[I]!=Invalid[I]) ++Changed;
    }
    TestTrue(TEXT("Valid footprint renders green geometry"),Green>100);
    TestTrue(TEXT("Invalid footprint renders red geometry"),Red>100);
    TestTrue(TEXT("Validity updates actual pixels on the same actor"),Changed>100);
    D.Mode=ERTSWorldOverlayMode::TurretRange; D.Radius=250;
    if (!Render(D,Small)) return false;
    D.Radius=500;
    if (!Render(D,Large)) return false;
    auto Extent=[](const TArray<FColor>& Pixels)
    {
        int32 Left=512,Right=-1;
        for (int32 Y=0;Y<512;++Y) for (int32 X=0;X<512;++X)
        {
            const FColor C=Pixels[Y*512+X];
            if (FMath::Max3(C.R,C.G,C.B)>35) { Left=FMath::Min(Left,X); Right=FMath::Max(Right,X); }
        }
        return Right>=Left ? Right-Left+1 : 0;
    };
    const int32 SmallExtent=Extent(Small),LargeExtent=Extent(Large);
    TestTrue(TEXT("Turret range produces a nonempty ring"),SmallExtent>30);
    TestTrue(TEXT("Changing radius changes rendered diameter without new material"),LargeExtent>SmallExtent*1.6 && LargeExtent<SmallExtent*2.4);
    const FColor Center=Large[256*512+256];
    TestTrue(TEXT("Turret range preserves a hollow center"),FMath::Max3(Center.R,Center.G,Center.B)<35);
    Target->MarkAsGarbage();
    return !HasAnyErrors();
}
#endif
