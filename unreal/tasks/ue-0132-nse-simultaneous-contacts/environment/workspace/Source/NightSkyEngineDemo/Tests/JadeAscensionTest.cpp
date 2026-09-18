// Original stage asset contracts. Editor-only dependencies never enter packaged runtime.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/AutomationCommon.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshResources.h"
#include "NightSkyEngine/Miscellaneous/NightSkyEditorSettings.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"
#include "NightSkyEngine/Data/PrimaryCharaData.h"
#include "NightSkyEngine/Data/PrimaryStageData.h"
#include "Camera/CameraActor.h"
#include "Engine/GameViewportClient.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HighResScreenshot.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeAscensionArenaTest,
    "NightSkyEngine.Arena.JadeAscension.CollisionAndClearance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeAscensionArenaTest::RunTest(const FString& Parameters)
{
    if (!FEditorFileUtils::LoadMap(TEXT("/Game/NightSkyEngine/Maps/TestMap/TestMap_PL"),false,false))
    {
        AddError(TEXT("Could not open saved arena map"));
        return false;
    }
    UWorld* World=GEditor->GetEditorWorldContext().World();
    if (!TestNotNull(TEXT("Saved combat map loads"),World)) return false;
    AStaticMeshActor* Floor=nullptr;
    int32 SceneryCount=0;
    const FBox SafeVolume(FVector(-1700,-350,1),FVector(1700,350,2600));
    for (TActorIterator<AActor> It(World);It;++It)
    {
        if (It->ActorHasTag(TEXT("JA_CombatFloor"))) Floor=Cast<AStaticMeshActor>(*It);
        if (!It->ActorHasTag(TEXT("JA_Scenery"))) continue;
        ++SceneryCount;
        auto* MeshActor=Cast<AStaticMeshActor>(*It);
        if (!TestNotNull(TEXT("Scenery remains ordinary static geometry"),MeshActor)) continue;
        auto* C=MeshActor->GetStaticMeshComponent();
        TestEqual(TEXT("Decorations cannot block fighters or camera"),C->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
        TestFalse(FString::Printf(TEXT("%s clears the fighting volume"),*It->GetActorLabel()),
            SafeVolume.Intersect(C->Bounds.GetBox()));
        if (It->GetActorLabel()==TEXT("JA_CelestialVista"))
            TestTrue(TEXT("Backdrop is behind the scene, not behind the camera"),C->Bounds.Origin.Y<-10000);
    }
    TestTrue(TEXT("At least forty background placements provide multiple depth layers"),SceneryCount>=40);
    if (!TestNotNull(TEXT("Existing Floor carries the combat surface"),Floor)) return false;
    TestEqual(TEXT("Existing Floor object identity retained"),Floor->GetFName(),FName(TEXT("Floor")));
    auto* C=Floor->GetStaticMeshComponent();
    TestEqual(TEXT("Floor blocks Unreal collision queries and physics"),C->GetCollisionEnabled(),ECollisionEnabled::QueryAndPhysics);
    TestTrue(TEXT("Floor reaches the original native stage edges plus margin"),C->Bounds.BoxExtent.X>=2000);
    TestTrue(TEXT("Floor is at least twelve metres deep"),C->Bounds.BoxExtent.Y>=600);
    TestTrue(TEXT("Deck top meets the native Z=0 fighting plane"),FMath::IsNearlyZero(C->Bounds.GetBox().Max.Z,.1));
    UBodySetup* Body=C->GetStaticMesh()->GetBodySetup();
    if (TestNotNull(TEXT("Floor has a collision body"),Body))
        TestEqual(TEXT("Simple collision uses one optimized solid box"),Body->AggGeom.BoxElems.Num(),1);
    FCollisionQueryParams SceneQuery;
    for (TActorIterator<AActor> It(World);It;++It)
        if (*It!=Floor && !It->ActorHasTag(TEXT("JA_Scenery"))) SceneQuery.AddIgnoredActor(*It);
    for (int32 X=-1376;X<=1376;X+=344)
    {
        for (int32 Y : {-250,0,250})
        {
            FHitResult Hit;
            const bool bHit=World->LineTraceSingleByChannel(Hit,FVector(X,Y,150),FVector(X,Y,-200),ECC_Visibility,SceneQuery);
            TestTrue(TEXT("Continuous ground across the full native fighting range"),bHit&&Hit.GetActor()==Floor);
            if (bHit) TestTrue(TEXT("Ground trace reaches the zero-height plane"),FMath::IsNearlyZero(Hit.ImpactPoint.Z,.2));
        }
    }
    // Head/air space stays free even at the corners; gameplay push/hit boxes remain native.
    FCollisionQueryParams Query=SceneQuery;Query.AddIgnoredActor(Floor);
    for (int32 Z : {100,250,500,1000})
    {
        FHitResult Hit;
        TestFalse(TEXT("Swept air corridor contains no scenic blockers"),World->SweepSingleByChannel(
            Hit,FVector(-1376,0,Z),FVector(1376,0,Z),FQuat::Identity,ECC_Visibility,
            FCollisionShape::MakeSphere(35),Query));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeAscensionMeshTest,
    "NightSkyEngine.Arena.JadeAscension.MeshBudgets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FJadeAscensionMeshTest::RunTest(const FString& Parameters)
{
    for (const TCHAR* Name : {TEXT("CombatTerrace"),TEXT("MoonGate"),TEXT("Pagoda"),TEXT("Balustrade"),
        TEXT("Lantern"),TEXT("LotusBrazier"),TEXT("SpiritObelisk"),TEXT("BlossomTree"),TEXT("FloatingCrag"),
        TEXT("PrayerBanner"),TEXT("GardenTerrace"),TEXT("SpiritHalo")})
    {
        const FString Path=FString::Printf(TEXT("/Game/NightSkyEngine/Stages/JadeAscension/Meshes/SM_JA_%s"),Name);
        UStaticMesh* Mesh=LoadObject<UStaticMesh>(nullptr,*Path);
        if (!TestNotNull(Path,Mesh)) continue;
        TestEqual(TEXT("Three conventional LODs"),Mesh->GetNumLODs(),3);
        if (const FStaticMeshRenderData* Data=Mesh->GetRenderData())
        {
            uint32 Previous=MAX_uint32;
            for (const FStaticMeshLODResources& LOD : Data->LODResources)
            {
                TestEqual(TEXT("One shared material section per LOD"),LOD.Sections.Num(),1);
                TestTrue(TEXT("LOD triangle count never increases"),LOD.GetNumTriangles()<=Previous);
                Previous=LOD.GetNumTriangles();
            }
            TestTrue(TEXT("Individual source meshes stay below 45k triangles"),Data->LODResources[0].GetNumTriangles()<45000);
            TestTrue(TEXT("Authored colour and emissive masks are retained"),Data->LODResources[0].VertexBuffers.ColorVertexBuffer.GetNumVertices()>0);
        }
        TestNotNull(TEXT("Mesh has a saved material"),Mesh->GetMaterial(0));
    }
    return true;
}

namespace JadeReview
{
TOptional<FBattleData> PreviousSettings;
class FObserveBattle final : public IAutomationLatentCommand
{
public:
    explicit FObserveBattle(FAutomationTestBase* InTest):Test(InTest),Start(FPlatformTime::Seconds()){}
    bool Update() override
    {
        UWorld* World=GEditor->PlayWorld;
        if (!World || World->GetTimeSeconds()<5)
        {
            if (FPlatformTime::Seconds()-Start<45) return false;
            Test->AddError(TEXT("PIE did not advance five seconds"));return true;
        }
        ANightSkyGameState* State=World->GetGameState<ANightSkyGameState>();
        if (!Test->TestNotNull(TEXT("Native fighting GameState runs in the updated stage"),State)) return true;
        for (bool Side : {true,false})
        {
            APlayerObject* Player=State->GetMainPlayer(Side);
            if (!Test->TestNotNull(TEXT("Shipped fighter spawned"),Player)) continue;
            const FVector Location=Player->GetActorLocation();
            Test->AddInfo(FString::Printf(TEXT("Fighter %s at %s"),*Player->GetName(),*Location.ToString()));
            Test->TestTrue(TEXT("Fighter stays over the clear deck"),FMath::Abs(Location.X)<1700&&FMath::Abs(Location.Y)<350);
            Test->TestTrue(TEXT("Standing fighter meets the deck"),FMath::IsNearlyZero(Location.Z,2));
        }
        if (Test->TestNotNull(TEXT("Native battle camera exists"),State->CameraActor))
            Test->AddInfo(FString::Printf(TEXT("Battle camera %s / %s"),*State->CameraActor->GetActorLocation().ToString(),*State->CameraActor->GetActorRotation().ToString()));
        if (GEngine->GameViewport && GEngine->GameViewport->Viewport)
        {
            const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots/JadeAscension"));
            IFileManager::Get().MakeDirectory(*Directory,true);
            GEngine->GameViewport->ConsoleCommand(FString::Printf(TEXT("HighResShot 1920x1080 filename=%s"),*(Directory/TEXT("pie-fight.png"))));
        }
        return true;
    }
private:
    FAutomationTestBase* Test;
    double Start;
};
class FRestoreSettings final : public IAutomationLatentCommand
{
public:
    bool Update() override
    {
        if (PreviousSettings.IsSet()) UNightSkyEditorSettings::Get()->BattleData=PreviousSettings.GetValue();
        PreviousSettings.Reset();return true;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJadeAscensionPIETest,
    "NightSkyEngine.Arena.JadeAscension.RenderedBattle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)
bool FJadeAscensionPIETest::RunTest(const FString& Parameters)
{
    if (!FEditorFileUtils::LoadMap(TEXT("/Game/NightSkyEngine/Maps/TestMap/TestMap_PL"),false,false)) return false;
    UPrimaryCharaData* Character=LoadObject<UPrimaryCharaData>(nullptr,TEXT("/Game/NightSkyEngine/Blueprints/Characters/Manny/CHR_Manny"));
    UPrimaryStageData* Stage=LoadObject<UPrimaryStageData>(nullptr,TEXT("/Game/NightSkyEngine/Maps/TestMap/STG_TestMap"));
    if (!TestNotNull(TEXT("Existing Manny fighter data"),Character)||!TestNotNull(TEXT("Existing stage data"),Stage)) return false;
    auto* Settings=UNightSkyEditorSettings::Get();
    JadeReview::PreviousSettings=Settings->BattleData;
    FBattleData& Data=Settings->BattleData;
    Data.bIsValid=true;Data.PlayerListP1={Character};Data.PlayerListP2={Character};
    Data.ColorIndicesP1={1};Data.ColorIndicesP2={2};Data.BattleFormat=EBattleFormat::Rounds;
    Data.Stage=Stage;Data.TimeUntilRoundStart=60;Data.StartRoundTimer=99;Data.RoundCount=2;
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(JadeReview::FObserveBattle(this));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(JadeReview::FRestoreSettings());
    return true;
}
#endif
