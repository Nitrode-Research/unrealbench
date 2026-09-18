#include "ReplayHlslFixture.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "ShaderCompiler.h"
#include "Misc/App.h"
#include "RenderCommandFence.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHIGlobals.h"
#include "RHIShaderPlatform.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_EDITOR
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "StaticMeshCompiler.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogReplayHlslFixture, Log, All);

namespace
{
constexpr int32 BurstCapacity = 8;
constexpr int32 CaptureSize = 128;
constexpr float Lifetime = 0.5f;
constexpr float ViewWidth = 1000.0f;

FName BurstParameter(int32 Index)
{
    return FName(*FString::Printf(TEXT("Burst%d"), Index));
}

#if WITH_EDITOR
template <typename T>
T* AddExpression(UMaterial* Material)
{
    T* Expression = NewObject<T>(Material, NAME_None, RF_Transient);
    Material->GetExpressionCollection().AddExpression(Expression);
    return Expression;
}

void ConnectInput(UMaterialExpressionCustom* Custom, FName Name,
                  UMaterialExpression* Expression, int32 Output = 0)
{
    FCustomInput Input;
    Input.InputName = Name;
    Input.Input.Connect(Output, Expression);
    Custom->Inputs.Add(Input);
}

bool HasRenderableShaders(const FMaterialResource* Resource)
{
    if (!Resource || !Resource->IsCompilationFinished() ||
        !Resource->IsGameThreadShaderMapComplete() || !Resource->GetCompileErrors().IsEmpty())
    {
        return false;
    }
    // UE 5.7's job cache can provide a complete frozen map without setting the
    // legacy finalized/success flags. Use the same content validity predicate
    // as the renderer; the tests also require actual non-default pixel output.
    const FMaterialShaderMap* Map = Resource->GetGameThreadShaderMap();
    return Map && Map->IsValidForRendering() && Map->GetShaderNum() > 0;
}
#endif
} // namespace

struct FReplayHlslFixture::FImpl
{
    struct FBurst
    {
        FVector2f Position = FVector2f::ZeroVector;
        float Age = Lifetime;
        float Seed = 0.0f;
    };

    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AStaticMeshActor> PlaneActor;
    TWeakObjectPtr<ASceneCapture2D> CaptureActor;
    TStrongObjectPtr<UStaticMesh> PlaneMesh;
    TStrongObjectPtr<UMaterial> Material;
    TStrongObjectPtr<UMaterialInstanceDynamic> Instance;
    TStrongObjectPtr<UTextureRenderTarget2D> Target;
    FBurst Bursts[BurstCapacity];
    uint32 NextSeed = 1;
    bool bInitialized = false;
    EShaderPlatform ShaderPlatform = SP_NumPlatforms;

    void Upload(int32 Index)
    {
        if (Instance.IsValid())
        {
            const FBurst& Burst = Bursts[Index];
            Instance->SetVectorParameterValue(BurstParameter(Index),
                FLinearColor(Burst.Position.X, Burst.Position.Y, Burst.Age, Burst.Seed));
        }
    }

    bool Ready() const
    {
#if WITH_EDITOR
        if (!bInitialized || !World.IsValid() || !PlaneActor.IsValid() ||
            !CaptureActor.IsValid() || !Material.IsValid() || !Instance.IsValid() ||
            !PlaneMesh.IsValid() || !Target.IsValid() || GUsingNullRHI)
        {
            return false;
        }
        const FMaterialResource* Resource = Material->GetMaterialResource(ShaderPlatform);
        return HasRenderableShaders(Resource) &&
               Target->GameThread_GetRenderTargetResource() != nullptr;
#else
        return false;
#endif
    }

    ~FImpl()
    {
        check(IsInGameThread());
        if (CaptureActor.IsValid())
        {
            CaptureActor->GetCaptureComponent2D()->TextureTarget = nullptr;
            CaptureActor->Destroy();
        }
        if (PlaneActor.IsValid())
        {
            PlaneActor->Destroy();
        }
        // Retain strongly held materials/target until actor render-state removals finish.
        FlushRenderingCommands();
    }
};

FReplayHlslFixture::FReplayHlslFixture(UWorld* World) : Impl(MakeUnique<FImpl>())
{
    check(IsInGameThread());
    Impl->World = World;
#if WITH_EDITOR
    if (!IsValid(World) || !World->Scene || !FApp::CanEverRender() || GUsingNullRHI)
    {
        UE_LOG(LogReplayHlslFixture, Warning, TEXT("Fixture requires a live world and real RHI."));
        return;
    }
    Impl->PlaneMesh.Reset(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
    if (!Impl->PlaneMesh.IsValid())
    {
        UE_LOG(LogReplayHlslFixture, Warning, TEXT("Native engine plane could not be loaded."));
        return;
    }
    UStaticMesh* Meshes[] = {Impl->PlaneMesh.Get()};
    FStaticMeshCompilingManager::Get().FinishCompilation(MakeArrayView(Meshes));

    Impl->Material.Reset(NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient));
    UMaterial* Material = Impl->Material.Get();
    Impl->ShaderPlatform = GShaderPlatformForFeatureLevel[World->GetFeatureLevel()];
    Material->SetFeatureLevelToCompile(World->GetFeatureLevel(), true);
    Material->MaterialDomain = MD_Surface;
    Material->BlendMode = BLEND_Opaque;
    Material->SetShadingModel(MSM_Unlit);
    Material->TwoSided = true;
    Material->bTangentSpaceNormal = false;

    auto* Custom = AddExpression<UMaterialExpressionCustom>(Material);
    Custom->Description = TEXT("Deterministic procedural spark bursts");
    Custom->OutputType = CMOT_Float3;
    Custom->Inputs.Reset();
    auto* Position = AddExpression<UMaterialExpressionWorldPosition>(Material);
    Position->WorldPositionShaderOffset = WPT_ExcludeAllShaderOffsets;
    ConnectInput(Custom, TEXT("PixelWorld"), Position);
    for (int32 Index = 0; Index < BurstCapacity; ++Index)
    {
        auto* Parameter = AddExpression<UMaterialExpressionVectorParameter>(Material);
        Parameter->ParameterName = BurstParameter(Index);
        Parameter->DefaultValue = FLinearColor(0.0f, 0.0f, Lifetime, 0.0f);
        // Vector parameter output 0 is RGB (position X/Z, age); output 4 is alpha (seed).
        ConnectInput(Custom, FName(*FString::Printf(TEXT("Data%d"), Index)), Parameter);
        ConnectInput(Custom, FName(*FString::Printf(TEXT("Seed%d"), Index)), Parameter, 4);
    }
    // Authored here; no content asset, include mapping, wall-clock time, or random state.
    Custom->Code = TEXT(
        "float3 data[8] = {Data0, Data1, Data2, Data3, Data4, Data5, Data6, Data7};\n"
        "float seeds[8] = {Seed0, Seed1, Seed2, Seed3, Seed4, Seed5, Seed6, Seed7};\n"
        "float3 color = float3(0.0, 0.0, 0.0);\n"
        "[unroll] for (int b = 0; b < 8; ++b)\n"
        "{\n"
        "    float age = max(data[b].z, 0.0);\n"
        "    if (age >= 0.5) continue;\n"
        "    float2 p = PixelWorld.xz - data[b].xy;\n"
        "    float seed = seeds[b];\n"
        "    float energy = 1.0 - age / 0.5;\n"
        "    [unroll] for (int s = 0; s < 12; ++s)\n"
        "    {\n"
        "        float angle = 6.28318530718 * frac((s + 1.0) * 0.61803398875 + seed * 0.119);\n"
        "        float2 direction = float2(cos(angle), sin(angle));\n"
        "        float speed = 360.0 + 160.0 * frac((s + 1.0) * 0.41421356 + seed * 0.173);\n"
        "        float2 center = direction * (22.0 + age * speed);\n"
        "        center.y -= 180.0 * age * age;\n"
        "        float2 offset = p - center;\n"
        "        float along = abs(dot(offset, direction));\n"
        "        float across = abs(dot(offset, float2(-direction.y, direction.x)));\n"
        "        float spark = saturate(1.0 - along / 24.0) * saturate(1.0 - across / 12.0);\n"
        "        color += spark * energy * float3(1.0, 0.52, 0.12);\n"
        "    }\n"
        "}\n"
        "return saturate(color);\n");
    Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Custom);
    Material->PreEditChange(nullptr);
    Material->PostEditChange();
    Material->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Synchronous);
    FMaterialResource* Resource = Material->GetMaterialResource(Impl->ShaderPlatform);
    UE_LOG(LogReplayHlslFixture, Display,
        TEXT("Shader preparation: worldFeature=%d maxFeature=%d worldPlatform=%d maxPlatform=%d resource=%d finished=%d valid=%d skipped=%d"),
        int32(World->GetFeatureLevel()), int32(GMaxRHIFeatureLevel), int32(Impl->ShaderPlatform),
        int32(GMaxRHIShaderPlatform), Resource != nullptr,
        Resource && Resource->IsCompilationFinished(), Resource && Resource->HasValidGameThreadShaderMap(),
        GShaderCompilingManager && GShaderCompilingManager->IsShaderCompilationSkipped());
    if (Resource)
    {
        Resource->CacheShaders(Impl->ShaderPlatform, EMaterialShaderPrecompileMode::Synchronous);
        Resource->FinishCompilation();
    }
    if (GShaderCompilingManager) { GShaderCompilingManager->FinishAllCompilation(); }
    FlushRenderingCommands();
    UE_LOG(LogReplayHlslFixture, Display,
        TEXT("Shader completion: resource=%d finished=%d valid=%d complete=%d successful=%d"),
        Resource != nullptr, Resource && Resource->IsCompilationFinished(),
        Resource && Resource->HasValidGameThreadShaderMap(), Resource && Resource->IsGameThreadShaderMapComplete(),
        Resource && Resource->GetGameThreadShaderMap() && Resource->GetGameThreadShaderMap()->CompiledSuccessfully());
    if (!HasRenderableShaders(Resource))
    {
        UE_LOG(LogReplayHlslFixture, Warning, TEXT("Custom material compilation failed or no shader map is ready."));
        if (Resource)
        {
            for (const FString& Error : Resource->GetCompileErrors())
            {
                UE_LOG(LogReplayHlslFixture, Warning, TEXT("Material compiler: %s"), *Error);
            }
        }
        return;
    }

    Impl->Instance.Reset(UMaterialInstanceDynamic::Create(Material, GetTransientPackage()));
    Impl->Target.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient));
    Impl->Target->ClearColor = FLinearColor::Black;
    Impl->Target->InitCustomFormat(CaptureSize, CaptureSize, PF_FloatRGBA, true);
    Impl->Target->UpdateResourceImmediate(true);

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.ObjectFlags |= RF_Transient;
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Impl->PlaneActor = World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector,
        FRotator(0.0, 0.0, 90.0), SpawnParameters);
    Impl->CaptureActor = World->SpawnActor<ASceneCapture2D>(FVector(0.0, -1000.0, 0.0),
        FRotator(0.0, 90.0, 0.0), SpawnParameters);
    if (!Impl->PlaneActor.IsValid() || !Impl->CaptureActor.IsValid() || !Impl->Instance.IsValid())
    {
        UE_LOG(LogReplayHlslFixture, Warning, TEXT("Fixture actors or material instance could not be created."));
        return;
    }
    auto* Plane = Impl->PlaneActor->GetStaticMeshComponent();
    Plane->SetMobility(EComponentMobility::Movable);
    Plane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Plane->SetGenerateOverlapEvents(false);
    Plane->SetCastShadow(false);
    Plane->SetStaticMesh(Impl->PlaneMesh.Get());
    Plane->SetMaterial(0, Impl->Instance.Get());
    Impl->PlaneActor->SetActorScale3D(FVector(ViewWidth / 100.0f, ViewWidth / 100.0f, 1.0f));

    auto* Capture = Impl->CaptureActor->GetCaptureComponent2D();
    Capture->TextureTarget = Impl->Target.Get();
    Capture->ProjectionType = ECameraProjectionMode::Orthographic;
    Capture->OrthoWidth = ViewWidth;
    Capture->bAutoCalculateOrthoPlanes = false;
    Capture->bUpdateOrthoPlanes = false;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->bAlwaysPersistRenderingState = false;
    Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDRNoAlpha;
    Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->ShowOnlyComponent(Plane);
    Capture->ShowFlags.SetLighting(false);
    Capture->ShowFlags.SetFog(false);
    Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetPostProcessing(false);
    Capture->ShowFlags.SetTonemapper(false);
    Capture->ShowFlags.SetEyeAdaptation(false);
    Capture->ShowFlags.SetBloom(false);
    Capture->ShowFlags.SetAntiAliasing(false);
    Capture->ShowFlags.SetTemporalAA(false);
    Capture->ShowFlags.SetMotionBlur(false);
    Clear();
    World->SendAllEndOfFrameUpdates();
    FlushRenderingCommands();
    Impl->bInitialized = true;
    UE_LOG(LogReplayHlslFixture, Display, TEXT("Custom HLSL fixture ready: 8 slots, lifetime 0.5s, 128x128 XZ capture."));
#else
    UE_LOG(LogReplayHlslFixture, Warning, TEXT("Fixture material construction requires WITH_EDITOR."));
#endif
}

FReplayHlslFixture::~FReplayHlslFixture() = default;

bool FReplayHlslFixture::IsReady() const
{
    check(IsInGameThread());
    return Impl->Ready();
}

bool FReplayHlslFixture::Spawn(const FVector& WorldLocation)
{
    check(IsInGameThread());
    if (!Impl->Ready() || !FMath::IsFinite(WorldLocation.X) ||
        !FMath::IsFinite(WorldLocation.Y) || !FMath::IsFinite(WorldLocation.Z))
    {
        return false;
    }
    for (int32 Index = 0; Index < BurstCapacity; ++Index)
    {
        FImpl::FBurst& Burst = Impl->Bursts[Index];
        if (Burst.Age >= Lifetime)
        {
            Burst.Position = FVector2f(float(WorldLocation.X), float(WorldLocation.Z));
            Burst.Age = 0.0f;
            Burst.Seed = float(Impl->NextSeed++ % 1024);
            Impl->Upload(Index);
            return true;
        }
    }
    return false;
}

void FReplayHlslFixture::Advance(float DeltaSeconds)
{
    check(IsInGameThread());
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0f)
    {
        return;
    }
    for (int32 Index = 0; Index < BurstCapacity; ++Index)
    {
        FImpl::FBurst& Burst = Impl->Bursts[Index];
        Burst.Age = FMath::Min(Lifetime, Burst.Age + DeltaSeconds);
        Impl->Upload(Index);
    }
}

void FReplayHlslFixture::Clear()
{
    check(IsInGameThread());
    for (int32 Index = 0; Index < BurstCapacity; ++Index)
    {
        Impl->Bursts[Index] = FImpl::FBurst();
        Impl->Upload(Index);
    }
    Impl->NextSeed = 1;
}

TArray<FColor> FReplayHlslFixture::Capture()
{
    check(IsInGameThread());
    TArray<FColor> Pixels;
    if (!Impl->Ready())
    {
        return Pixels;
    }
    Impl->World->SendAllEndOfFrameUpdates();
    Impl->CaptureActor->GetCaptureComponent2D()->CaptureScene();
    FRenderCommandFence Fence;
    Fence.BeginFence();
    Fence.Wait();
    FReadSurfaceDataFlags Flags(RCM_UNorm);
    Flags.SetLinearToGamma(false);
    // ReadPixels performs the GPU readback; no synthesized or CPU-drawn image is returned.
    if (!Impl->Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, Flags) ||
        Pixels.Num() != CaptureSize * CaptureSize)
    {
        UE_LOG(LogReplayHlslFixture, Warning, TEXT("Render-target readback failed."));
        Pixels.Reset();
    }
    return Pixels;
}
