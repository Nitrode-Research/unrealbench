#include "Editor/LinkAssetTools.h"
#include "Animation/BlendSpace.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"

bool ULinkAssetTools::FinalizeBlendSpace(UBlendSpace* BlendSpace)
{
#if WITH_EDITOR
    if (!BlendSpace) { return false; }
    BlendSpace->Modify();
    BlendSpace->ValidateSampleData();
    BlendSpace->ResampleData();
    BlendSpace->MarkPackageDirty();
    TArray<FBlendSampleData> Samples;
    int32 CachedIndex = INDEX_NONE;
    return BlendSpace->GetSamplesFromBlendInput(FVector(250,0,0),Samples,CachedIndex,true) && !Samples.IsEmpty();
#else
    return false;
#endif
}

bool ULinkAssetTools::CreateChainSocket(USkeletalMesh* Mesh, float AlongCalf)
{
#if WITH_EDITOR
    if (!Mesh || !FMath::IsFinite(AlongCalf) || AlongCalf < 0 || AlongCalf > 1) { return false; }
    const auto& Skeleton = Mesh->GetRefSkeleton();
    auto FindBone = [&](const TArray<FName>& Names) -> int32
    {
        for (const auto& Name : Names) { const int32 Index=Skeleton.FindBoneIndex(Name); if (Index!=INDEX_NONE) { return Index; } }
        return INDEX_NONE;
    };
    const int32 Calf = FindBone({TEXT("LowerLeg.L"),TEXT("LowerLeg_L"),TEXT("LowerLeg-L")});
    const int32 Foot = FindBone({TEXT("Foot.L"),TEXT("Foot_L"),TEXT("Foot-L")});
    if (Calf==INDEX_NONE || Foot==INDEX_NONE) { return false; }
    auto ComponentTransform = [&](int32 Index)
    {
        FTransform Transform=Skeleton.GetRefBonePose()[Index];
        for (int32 Parent=Skeleton.GetParentIndex(Index); Parent!=INDEX_NONE; Parent=Skeleton.GetParentIndex(Parent))
        {
            Transform=Transform*Skeleton.GetRefBonePose()[Parent];
        }
        return Transform;
    };
    const FTransform CalfTransform=ComponentTransform(Calf);
    const FVector Target=FMath::Lerp(CalfTransform.GetLocation(),ComponentTransform(Foot).GetLocation(),AlongCalf);
    Mesh->Modify();
    auto& Sockets=Mesh->GetMeshOnlySocketList();
    USkeletalMeshSocket* Socket=nullptr;
    for (const auto& Existing : Sockets) { if (Existing && Existing->SocketName==TEXT("ChainCuff")) { Socket=Existing; break; } }
    if (!Socket) { Socket=NewObject<USkeletalMeshSocket>(Mesh); Sockets.Add(Socket); }
    Socket->SocketName=TEXT("ChainCuff");
    Socket->BoneName=Skeleton.GetBoneName(Calf);
    Socket->RelativeLocation=CalfTransform.InverseTransformPosition(Target);
    Socket->RelativeRotation=FRotator::ZeroRotator;
    Socket->RelativeScale=FVector::OneVector;
    Mesh->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}
