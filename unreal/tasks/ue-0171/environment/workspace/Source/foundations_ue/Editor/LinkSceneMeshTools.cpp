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
    // TODO: restore the documented scene pipeline behavior.
    return nullptr;
}

bool ULinkAssetTools::ApplySourceBoxTransform(AActor* Actor, const TArray<double>& UnityBoxMatrix)
{
    // TODO: restore the documented scene pipeline behavior.
    return false;
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
    // TODO: restore the documented scene pipeline behavior.
    
}
