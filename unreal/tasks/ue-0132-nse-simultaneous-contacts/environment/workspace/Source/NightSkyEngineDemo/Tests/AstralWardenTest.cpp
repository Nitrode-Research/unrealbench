// Asset compatibility tests. The fighter uses the original native gameplay and animation rig.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/SkeletalMeshActor.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInterface.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "NightSkyEngine/Data/MaterialData.h"

namespace AstralWarden
{
const TCHAR* NewMeshPath=TEXT("/Game/NightSkyEngine/CharacterAssets/AstralWarden/Meshes/SK_AstralWarden");
const TCHAR* OriginalMeshPath=TEXT("/Game/ControlRig/Characters/Mannequins/Meshes/SKM_Manny");
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAstralWardenRigTest,"NightSkyEngine.Fighter.AstralWarden.RigAndReplacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAstralWardenRigTest::RunTest(const FString& Parameters)
{
    USkeletalMesh* Mesh=LoadObject<USkeletalMesh>(nullptr,AstralWarden::NewMeshPath);
    USkeletalMesh* Original=LoadObject<USkeletalMesh>(nullptr,AstralWarden::OriginalMeshPath);
    if (!TestNotNull(TEXT("New fighter loads"),Mesh)||!TestNotNull(TEXT("Original rig reference loads"),Original)) return false;
    TestEqual(TEXT("Exact original skeleton asset, no retargeting"),Mesh->GetSkeleton(),Original->GetSkeleton());
    const FReferenceSkeleton& A=Original->GetRefSkeleton(); const FReferenceSkeleton& B=Mesh->GetRefSkeleton();
    TestEqual(TEXT("All original bones retained"),B.GetRawBoneNum(),A.GetRawBoneNum());
    for(int32 I=0;I<A.GetRawBoneNum();++I)
    {
        const FName Name=A.GetBoneName(I);const int32 J=B.FindBoneIndex(Name);
        if (!TestTrue(FString::Printf(TEXT("Bone exists: %s"),*Name.ToString()),J!=INDEX_NONE)) continue;
        const int32 AP=A.GetParentIndex(I),BP=B.GetParentIndex(J);
        TestEqual(TEXT("Parent hierarchy preserved"),BP==INDEX_NONE?NAME_None:B.GetBoneName(BP),AP==INDEX_NONE?NAME_None:A.GetBoneName(AP));
        const FTransform& AT=A.GetRefBonePose()[I];const FTransform& BT=B.GetRefBonePose()[J];
        TestTrue(FString::Printf(TEXT("Rest translation: %s"),*Name.ToString()),AT.GetTranslation().Equals(BT.GetTranslation(),.05));
        TestTrue(FString::Printf(TEXT("Rest rotation: %s"),*Name.ToString()),AT.GetRotation().Equals(BT.GetRotation(),.001));
        TestTrue(FString::Printf(TEXT("Rest scale: %s"),*Name.ToString()),AT.GetScale3D().Equals(BT.GetScale3D(),.001));
    }
    TestEqual(TEXT("Original physics asset retained"),Mesh->GetPhysicsAsset(),Original->GetPhysicsAsset());
    TestEqual(TEXT("Original corrective post-process animation retained"),Mesh->GetPostProcessAnimBlueprint(),Original->GetPostProcessAnimBlueprint());
    for(const auto& Slot:Mesh->GetMaterials())
    {
        if(!TestNotNull(TEXT("Surface material assigned on skeletal asset"),Slot.MaterialInterface.Get()))continue;
        float Value=0;
        TestTrue(TEXT("Native projection/depth offset interface is available"),Slot.MaterialInterface->GetScalarParameterValue(FMaterialParameterInfo(TEXT("ScreenSpaceDepthOffset")),Value));
    }
    TestEqual(TEXT("Seven intentional surface slots"),Mesh->GetMaterials().Num(),7);
    TestTrue(TEXT("Three detail levels"),Mesh->GetLODNum()>=3);
    const auto* Data=Mesh->GetResourceForRendering();
    if(TestNotNull(TEXT("Renderable skeletal data"),Data))
    {
        uint32 Previous=MAX_uint32;
        for(const auto& LOD:Data->LODRenderData)
        {
            uint32 Triangles=0;for(const auto& Section:LOD.RenderSections) Triangles+=Section.NumTriangles;
            TestTrue(TEXT("Each LOD reduces triangles"),Triangles>0&&Triangles<Previous);Previous=Triangles;
        }
        TestTrue(TEXT("High detail stays under 140k triangles"),Data->LODRenderData[0].GetTotalFaces()<140000);
    }
    const FBoxSphereBounds Bounds=Mesh->GetImportedBounds();
    TestTrue(TEXT("Human-sized mesh, no centimetre/metre scaling error"),Bounds.BoxExtent.Z>85&&Bounds.BoxExtent.Z<105);
    UBlueprint* BP=LoadObject<UBlueprint>(nullptr,TEXT("/Game/NightSkyEngine/Blueprints/Characters/Manny/BP_Manny"));
    if(!TestNotNull(TEXT("Stable BP_Manny asset identity"),BP))return false;
    int32 Replaced=0;
    for(USCS_Node* Node:BP->SimpleConstructionScript->GetAllNodes())
    {
        if(auto* C=Cast<USkeletalMeshComponent>(Node->ComponentTemplate))
        {
            TestEqual(TEXT("Body and shadow both use replacement"),C->GetSkeletalMeshAsset(),Mesh);
            TestNotNull(TEXT("Existing animation blueprint remains assigned"),C->GetAnimClass());
            ++Replaced;
        }
    }
    TestEqual(TEXT("Both original skeletal components remain"),Replaced,2);
    for(const TCHAR* Path:{TEXT("/Game/NightSkyEngine/CharacterAssets/Mannequins/Materials/Color01/DA_MannyMaterials01"),TEXT("/Game/NightSkyEngine/CharacterAssets/Mannequins/Materials/Color02/DA_MannyMaterials02")})
    {
        auto* Materials=LoadObject<UMaterialData>(nullptr,Path);
        if(!TestNotNull(TEXT("Existing costume data loads"),Materials))continue;
        for(const auto& Row:Materials->MaterialStructs)
        {
            TestEqual(TEXT("All new body and shadow material slots covered"),Row.Material.Num(),7);
            for(auto* Material:Row.Material)TestNotNull(TEXT("No missing material"),Material);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAstralWardenAnimationTest,"NightSkyEngine.Fighter.AstralWarden.ExistingAnimationPoses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAstralWardenAnimationTest::RunTest(const FString& Parameters)
{
    USkeletalMesh* Mesh=LoadObject<USkeletalMesh>(nullptr,AstralWarden::NewMeshPath);
    USkeletalMesh* Original=LoadObject<USkeletalMesh>(nullptr,AstralWarden::OriginalMeshPath);
    if(!Mesh||!Original){AddError(TEXT("Missing fighter mesh"));return false;}
    UWorld* World=GEditor->GetEditorWorldContext().World();
    FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;
    auto* A=World->SpawnActor<ASkeletalMeshActor>(Spawn);auto* B=World->SpawnActor<ASkeletalMeshActor>(Spawn);
    auto* AC=A->GetSkeletalMeshComponent();auto* BC=B->GetSkeletalMeshComponent();
    AC->SetSkeletalMesh(Original);BC->SetSkeletalMesh(Mesh);
    AC->SetAnimationMode(EAnimationMode::AnimationSingleNode);BC->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.ScanPathsSynchronous({TEXT("/Game/NightSkyEngine/CharacterAssets/Mannequins/Animations")},true);
    TArray<FAssetData> Assets;Registry.GetAssetsByPath(TEXT("/Game/NightSkyEngine/CharacterAssets/Mannequins/Animations"),Assets,true);
    int32 Count=0;
    bool bObservedArticulation=false;
    AC->SetUpdateAnimationInEditor(true);BC->SetUpdateAnimationInEditor(true);
    AC->RefreshBoneTransforms();
    const FVector BindHand=AC->GetBoneLocation(TEXT("hand_l"),EBoneSpaces::ComponentSpace);
    for(const FAssetData& Asset:Assets)
    {
        UAnimSequence* Anim=Cast<UAnimSequence>(Asset.GetAsset());if(!Anim)continue;
        TestEqual(TEXT("Move uses shared skeleton"),Anim->GetSkeleton(),Mesh->GetSkeleton());
        AC->SetAnimation(Anim);BC->SetAnimation(Anim);
        for(float Fraction:{0.f,.37f,.8f})
        {
            for(auto* C:{AC,BC}){C->SetPosition(Anim->GetPlayLength()*Fraction,false);C->TickAnimation(0,false);C->RefreshBoneTransforms();C->UpdateComponentToWorld();}
            for(FName Bone:{FName(TEXT("head")),FName(TEXT("hand_l")),FName(TEXT("hand_r")),FName(TEXT("foot_l")),FName(TEXT("foot_r")),FName(TEXT("pelvis"))})
            {
                const FVector P=AC->GetBoneLocation(Bone,EBoneSpaces::ComponentSpace),Q=BC->GetBoneLocation(Bone,EBoneSpaces::ComponentSpace);
                TestTrue(FString::Printf(TEXT("%s @ %.2f: %s matches original animated pose"),*Anim->GetName(),Fraction,*Bone.ToString()),!Q.ContainsNaN()&&P.Equals(Q,.1));
            }
        }
        bObservedArticulation|=!AC->GetBoneLocation(TEXT("hand_l"),EBoneSpaces::ComponentSpace).Equals(BindHand,1.f);
        AddInfo(FString::Printf(TEXT("Clip %s keys=%d length=%.3f hand=%s"),*Anim->GetName(),Anim->GetNumberOfSampledKeys(),Anim->GetPlayLength(),*AC->GetBoneLocation(TEXT("hand_l"),EBoneSpaces::ComponentSpace).ToString()));
        ++Count;
    }
    World->DestroyActor(A);World->DestroyActor(B);
    TestTrue(TEXT("Actual fighting animation library was sampled"),Count>=20);
    TestTrue(TEXT("Animation evaluation moves joints away from bind pose"),bObservedArticulation);
    AddInfo(FString::Printf(TEXT("Compared %d existing clips at three times each against original Manny"),Count));
    return true;
}
#endif
