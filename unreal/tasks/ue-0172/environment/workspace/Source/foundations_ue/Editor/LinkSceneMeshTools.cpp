#include "Editor/LinkAssetTools.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Rules/LinkSceneCoordinates.h"
#include "Components/PrimitiveComponent.h"
#include "NavModifierComponent.h"
#include "NavAreas/NavArea_Null.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "Materials/Material.h"

namespace
{
    FVector Vector(const TSharedPtr<FJsonValue>& Value)
    {
        const auto& A=Value->AsArray();
        return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());
    }
}
#endif

UStaticMesh* ULinkAssetTools::BuildSourceMesh(const FString& JsonFile, const FString& AssetPath, const TArray<double>& UnityMatrix, bool bCollision)
{
#if WITH_EDITOR
    FMatrix Linear;
    if (!LinkSceneCoordinates::Matrix(UnityMatrix,Linear) || !FPackageName::IsValidLongPackageName(AssetPath)) { return nullptr; }
    Linear.SetOrigin(FVector::ZeroVector);
    if (FMath::Abs(Linear.RotDeterminant())<1e-9) { return nullptr; }
    const FMatrix NormalMatrix=Linear.Inverse().GetTransposed();
    const bool bMirror=LinkSceneCoordinates::ReversesBakedWinding(Linear);
    // Unity's stored triangle cross product agrees with its vertex normal;
    // Unreal MeshDescription/Chaos expects the opposite ordering (verified
    // against /Engine/BasicShapes/Cube). This is a mesh-format conversion,
    // separate from the positive-determinant cyclic axis permutation.
    const bool bReverseTriangles=!bMirror;
    FString Text; TSharedPtr<FJsonObject> Data;
    if (!FFileHelper::LoadFileToString(Text,*JsonFile) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data)) { return nullptr; }
    const auto& Positions=Data->GetArrayField(TEXT("vertices"));
    const auto& Normals=Data->GetArrayField(TEXT("normals"));
    const auto& Tangents=Data->GetArrayField(TEXT("tangents"));
    const auto& Colors=Data->GetArrayField(TEXT("colors"));
    const auto& UVs=Data->GetArrayField(TEXT("uv"));
    const auto& Submeshes=Data->GetArrayField(TEXT("submeshes"));
    if (Positions.IsEmpty() || Normals.Num()!=Positions.Num() || Submeshes.IsEmpty()) { return nullptr; }
    FMeshDescription Description;
    FStaticMeshAttributes Attributes(Description); Attributes.Register();
    auto VertexPositions=Attributes.GetVertexPositions();
    auto InstanceNormals=Attributes.GetVertexInstanceNormals();
    auto InstanceTangents=Attributes.GetVertexInstanceTangents();
    auto InstanceSigns=Attributes.GetVertexInstanceBinormalSigns();
    auto InstanceColors=Attributes.GetVertexInstanceColors();
    auto InstanceUVs=Attributes.GetVertexInstanceUVs();
    int32 Channels=1;
    for (int32 Channel=0;Channel<UVs.Num();++Channel) { if (!UVs[Channel]->AsArray().IsEmpty()) { Channels=Channel+1; } }
    InstanceUVs.SetNumChannels(Channels);
    TArray<FVertexID> Vertices;
    for (const auto& Position : Positions)
    {
        const FVertexID ID=Description.CreateVertex(); Vertices.Add(ID);
        VertexPositions[ID]=FVector3f(FVector(Linear.TransformPosition(LinkSceneCoordinates::Position(Vector(Position)))));
    }
    auto* Package=CreatePackage(*AssetPath);
    const FName Name(*FPackageName::GetLongPackageAssetName(AssetPath));
    UStaticMesh* Mesh=FindObject<UStaticMesh>(Package,*Name.ToString());
    const bool bNew=!Mesh;
    if (!Mesh) { Mesh=NewObject<UStaticMesh>(Package,Name,RF_Public|RF_Standalone); }
    Mesh->Modify(); Mesh->GetStaticMaterials().Reset();
    for (int32 Section=0;Section<Submeshes.Num();++Section)
    {
        const auto Sub=Submeshes[Section]->AsObject();
        if (Sub->GetStringField(TEXT("topology"))!=TEXT("Triangles")) { return nullptr; }
        const auto& Indices=Sub->GetArrayField(TEXT("indices"));
        if (Indices.Num()%3) { return nullptr; }
        const FPolygonGroupID Group=Description.CreatePolygonGroup();
        const FName Slot(*FString::Printf(TEXT("SourceSlot%d"),Section));
        Attributes.GetPolygonGroupMaterialSlotNames()[Group]=Slot;
        Mesh->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface),Slot,Slot));
        for (int32 Triangle=0;Triangle<Indices.Num();Triangle+=3)
        {
            TArray<FVertexInstanceID> Corners;
            for (int32 Corner=0;Corner<3;++Corner)
            {
                const int32 Order=bReverseTriangles && Corner>0 ? 3-Corner : Corner;
                const int32 Index=int32(Indices[Triangle+Order]->AsNumber());
                if (!Vertices.IsValidIndex(Index)) { return nullptr; }
                const FVertexInstanceID Instance=Description.CreateVertexInstance(Vertices[Index]); Corners.Add(Instance);
                const FVector N=FVector(NormalMatrix.TransformVector(LinkSceneCoordinates::Direction(Vector(Normals[Index])))).GetSafeNormal();
                FVector T=FVector::CrossProduct(FMath::Abs(N.Z)<.9 ? FVector::UpVector : FVector::RightVector,N).GetSafeNormal();
                float Sign=1;
                if (Tangents.Num()==Positions.Num())
                {
                    T=FVector(Linear.TransformVector(LinkSceneCoordinates::Direction(Vector(Tangents[Index])))).GetSafeNormal();
                    Sign=float(Tangents[Index]->AsArray()[3]->AsNumber())*(bMirror?-1.f:1.f);
                }
                InstanceNormals[Instance]=FVector3f(N); InstanceTangents[Instance]=FVector3f(T); InstanceSigns[Instance]=Sign;
                FVector4f Color(1,1,1,1);
                if (Colors.Num()==Positions.Num())
                {
                    const auto& C=Colors[Index]->AsArray(); Color=FVector4f(C[0]->AsNumber(),C[1]->AsNumber(),C[2]->AsNumber(),C[3]->AsNumber());
                }
                InstanceColors[Instance]=Color;
                for (int32 Channel=0;Channel<Channels;++Channel)
                {
                    FVector2f UV=FVector2f::ZeroVector;
                    if (UVs.IsValidIndex(Channel) && UVs[Channel]->AsArray().Num()==Positions.Num())
                    {
                        const auto& Value=UVs[Channel]->AsArray()[Index]->AsArray();
                        // Preserve source UV values; palette/textures use explicit source sampling.
                        UV=FVector2f(Value[0]->AsNumber(),Value[1]->AsNumber());
                    }
                    InstanceUVs.Set(Instance,Channel,UV);
                }
            }
            Description.CreatePolygon(Group,Corners);
        }
    }
    Mesh->SetNumSourceModels(1);
    auto& Settings=Mesh->GetSourceModel(0).BuildSettings;
    Settings.bRecomputeNormals=false; Settings.bRecomputeTangents=false; Settings.bGenerateLightmapUVs=false;
    Settings.bUseFullPrecisionUVs=true; Settings.bUseHighPrecisionTangentBasis=true;
    Mesh->CreateBodySetup();
    Mesh->GetBodySetup()->CollisionTraceFlag=bCollision?CTF_UseComplexAsSimple:CTF_UseDefault;
    UStaticMesh::FBuildMeshDescriptionsParams Params; Params.bAllowCpuAccess=true;
    Params.bUseHashAsGuid=true;
    if (!Mesh->BuildFromMeshDescriptions({&Description},Params)) { return nullptr; }
    Mesh->GetBodySetup()->InvalidatePhysicsData(); Mesh->GetBodySetup()->CreatePhysicsMeshes();
    Mesh->MarkPackageDirty();
    if (bNew) { FAssetRegistryModule::AssetCreated(Mesh); }
    return Mesh;
#else
    return nullptr;
#endif
}

bool ULinkAssetTools::ApplySourceBoxTransform(AActor* Actor, const TArray<double>& UnityBoxMatrix)
{
#if WITH_EDITOR
    FMatrix Matrix;
    if (!Actor || !LinkSceneCoordinates::Matrix(UnityBoxMatrix,Matrix)) { return false; }
    const FTransform Transform(Matrix);
    if (!Transform.ToMatrixWithScale().Equals(Matrix,.001)) { return false; }
    FVector Center,Extent; Actor->GetActorBounds(false,Center,Extent);
    if (Extent.GetMin()<=0) { return false; }
    Actor->SetActorLocationAndRotation(Transform.GetLocation(),Transform.GetRotation());
    Actor->SetActorScale3D(Transform.GetScale3D()*FVector(50/Extent.X,50/Extent.Y,50/Extent.Z));
    return true;
#else
    return false;
#endif
}

FString ULinkAssetTools::InspectSourceMesh(UStaticMesh* Mesh)
{
#if WITH_EDITOR
    if (!Mesh || !Mesh->GetMeshDescription(0)) { return FString(); }
    const auto* Description=Mesh->GetMeshDescription(0);
    auto Object=MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("vertices"),Description->Vertices().Num());
    Object->SetNumberField(TEXT("triangles"),Description->Triangles().Num());
    Object->SetNumberField(TEXT("sections"),Description->PolygonGroups().Num());
    Object->SetNumberField(TEXT("physicsMeshes"),Mesh->GetBodySetup()?Mesh->GetBodySetup()->TriMeshGeometries.Num():-1);
    TArray<TSharedPtr<FJsonValue>> Sections;
    for(int32 I=0;I<Mesh->GetRenderData()->LODResources[0].Sections.Num();++I)
    {
        const auto& Section=Mesh->GetRenderData()->LODResources[0].Sections[I];
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("triangles"),Section.NumTriangles);
        Row->SetBoolField(TEXT("collision"),Section.bEnableCollision);Sections.Add(MakeShared<FJsonValueObject>(Row));
    }
    Object->SetArrayField(TEXT("renderSections"),Sections);
    if(Description->Triangles().Num()>0)
    {
        const auto T=*Description->Triangles().GetElementIDs().begin();
        const auto IDs=Description->GetTriangleVertexInstances(T);
        FStaticMeshConstAttributes A(*Description);
        const FVector3f P0=A.GetVertexPositions()[Description->GetVertexInstanceVertex(IDs[0])];
        const FVector3f P1=A.GetVertexPositions()[Description->GetVertexInstanceVertex(IDs[1])];
        const FVector3f P2=A.GetVertexPositions()[Description->GetVertexInstanceVertex(IDs[2])];
        Object->SetNumberField(TEXT("windingNormalDot"),FVector3f::DotProduct(FVector3f::CrossProduct(P1-P0,P2-P0).GetSafeNormal(),A.GetVertexInstanceNormals()[IDs[0]]));
    }
    FStaticMeshConstAttributes Attributes(*Description);
    Object->SetNumberField(TEXT("uvChannels"),Attributes.GetVertexInstanceUVs().GetNumChannels());
    TArray<TSharedPtr<FJsonValue>> Positions;
    for (const auto ID:Description->Vertices().GetElementIDs())
    {
        const FVector3f P=Attributes.GetVertexPositions()[ID];
        Positions.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)}));
    }
    Object->SetArrayField(TEXT("positions"),Positions);
    FString Text; FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Text)); return Text;
#else
    return FString();
#endif
}

void ULinkAssetTools::SetSourceNavigation(AActor* Actor, bool bRelevant, bool bExcluded)
{
#if WITH_EDITOR
    if (!Actor) { return; }
    TArray<UPrimitiveComponent*> Components; Actor->GetComponents(Components);
    for (auto* Component:Components) { Component->SetCanEverAffectNavigation(bRelevant); }
    if (bRelevant && bExcluded)
    {
        auto* Modifier=NewObject<UNavModifierComponent>(Actor,TEXT("SourceNavigationArea"));
        Actor->AddInstanceComponent(Modifier); Modifier->SetAreaClass(UNavArea_Null::StaticClass()); Modifier->RegisterComponent();
    }
#endif
}
