#include "Gameplay/LinkUnityPoseGrid.h"
#include "Animation/AnimInstanceProxy.h"
#include "BoneContainer.h"

namespace
{
FQuat UnityRotation(const FQuat& Value){return FQuat(-Value.X,Value.Z,Value.Y,Value.W);}
int64 SampleCount(const TArray<int32>& Dimensions)
{
    if(Dimensions.IsEmpty()||Dimensions.Num()>4){return 0;}
    int64 Count=1;for(int32 Size:Dimensions){if(Size<2||Size>256){return 0;}Count*=Size;}
    return Count;
}
}
bool ULinkUnityPoseGrid::IsValidData() const
{
    const int64 Stride=SourceBones.Num()*7;
    return Stride>0&&SourceRestRotations.Num()==SourceBones.Num()
        &&MovingDimensions.Num()==2&&EnteringDimensions.Num()==4&&ExitingDimensions.Num()==3
        &&SampleCount(MovingDimensions)*Stride==MovingPoses.Num()
        &&SampleCount(EnteringDimensions)*Stride==EnteringPoses.Num()
        &&SampleCount(ExitingDimensions)*Stride==ExitingPoses.Num();
}
bool ULinkUnityPoseGrid::Evaluate(const FLinkLocomotionFrame& Frame,FPoseContext& Output) const
{
    if(!IsValidData()||(Frame.bIdle&&!Frame.bTransition)){return false;}
    const TArray<int32>* Dimensions=&MovingDimensions;const TArray<float>* Data=&MovingPoses;
    float Coordinates[4]={FMath::Clamp(Frame.Speed,0.f,1.f),FMath::Frac(Frame.Phase),0,0};
    if(Frame.bTransition)
    {
        const float Alpha=FMath::Clamp(Frame.Transition,0.f,1.f);
        if(Frame.bIdle)
        {
            Dimensions=&EnteringDimensions;Data=&EnteringPoses;
            const FLinkLocomotionConfig Timing;
            const float Slow=Alpha*Timing.EnterSeconds/Timing.Lengths[1];
            const float Fast=Alpha*Timing.EnterSeconds/Timing.Lengths[3];
            Coordinates[1]=FMath::Frac(Frame.Phase);Coordinates[2]=Alpha;
            Coordinates[3]=Fast>Slow?FMath::Clamp((Frame.NextPhase-Slow)/(Fast-Slow),0.f,1.f):0.f;
        }
        else{Dimensions=&ExitingDimensions;Data=&ExitingPoses;Coordinates[2]=Alpha;}
    }
    int32 Lower[4]={0,0,0,0};float Fraction[4]={0,0,0,0};
    for(int32 Axis=0;Axis<Dimensions->Num();++Axis)
    {
        const float Point=Coordinates[Axis]*((*Dimensions)[Axis]-1);
        Lower[Axis]=FMath::Min(FMath::FloorToInt(Point),(*Dimensions)[Axis]-2);
        Fraction[Axis]=Point-Lower[Axis];
    }
    TArray<int32,TInlineAllocator<16>> Offsets;TArray<float,TInlineAllocator<16>> Weights;
    const int32 Stride=SourceBones.Num()*7;
    for(int32 Corner=0;Corner<(1<<Dimensions->Num());++Corner)
    {
        int32 Sample=0;float Weight=1;
        for(int32 Axis=0;Axis<Dimensions->Num();++Axis)
        {
            const bool Upper=(Corner&(1<<Axis))!=0;
            Sample=Sample*(*Dimensions)[Axis]+Lower[Axis]+int32(Upper);
            Weight*=Upper?Fraction[Axis]:1-Fraction[Axis];
        }
        if(Weight>0){Offsets.Add(Sample*Stride);Weights.Add(Weight);}
    }
    TArray<FVector,TInlineAllocator<40>> Positions;TArray<FQuat,TInlineAllocator<40>> Rotations;
    Positions.SetNum(SourceBones.Num());Rotations.SetNum(SourceBones.Num());
    for(int32 Bone=0;Bone<SourceBones.Num();++Bone)
    {
        FVector Position=FVector::ZeroVector;FQuat Rotation(0,0,0,0),Hemisphere=FQuat::Identity;
        for(int32 Corner=0;Corner<Offsets.Num();++Corner)
        {
            const float* Values=Data->GetData()+Offsets[Corner]+Bone*7;
            Position+=FVector(Values[0],Values[1],Values[2])*Weights[Corner];
            FQuat Current(Values[3],Values[4],Values[5],Values[6]);
            if(Corner==0){Hemisphere=Current;}
            if((Hemisphere|Current)<0){Current=Current*-1.f;}
            Rotation=Rotation+Current*Weights[Corner];
        }
        Positions[Bone]=FVector(-Position.X,Position.Z,Position.Y)*100;
        Rotations[Bone]=UnityRotation(Rotation.GetNormalized());
    }
    const auto& Reference=Output.Pose.GetBoneContainer().GetReferenceSkeleton();
    TArray<FTransform,TInlineAllocator<48>> Bind,Component,Local;
    Bind.SetNum(Reference.GetNum());Component.SetNum(Reference.GetNum());Local.SetNum(Reference.GetNum());
    for(int32 Bone=0;Bone<Reference.GetNum();++Bone)
    {
        const int32 Parent=Reference.GetParentIndex(Bone);const FTransform& Rest=Reference.GetRefBonePose()[Bone];
        Bind[Bone]=Parent!=INDEX_NONE?Rest*Bind[Parent]:Rest;
        const FString Name=Reference.GetBoneName(Bone).ToString();int32 Source=SourceBones.Find(FName(*Name));
        if(Source==INDEX_NONE){Source=SourceBones.Find(FName(*Name.Replace(TEXT("_"),TEXT("."))));}
        if(Source==INDEX_NONE)
        {
            Local[Bone]=Rest;Component[Bone]=Parent!=INDEX_NONE?Rest*Component[Parent]:Rest;
            continue;
        }
        const FQuat Correction=UnityRotation(SourceRestRotations[Source]).Inverse()*Bind[Bone].GetRotation();
        Component[Bone]=FTransform((Rotations[Source]*Correction).GetNormalized(),Positions[Source],Bind[Bone].GetScale3D());
        Local[Bone]=Parent!=INDEX_NONE?Component[Bone].GetRelativeTransform(Component[Parent]):Component[Bone];
    }
    Output.ResetToRefPose();
    for(FCompactPoseBoneIndex Bone:Output.Pose.ForEachBoneIndex())
    {
        const int32 SkeletonIndex=Output.Pose.GetBoneContainer().GetSkeletonPoseIndexFromCompactPoseIndex(Bone).GetInt();
        Output.Pose[Bone]=Local[SkeletonIndex];
    }
    return true;
}
