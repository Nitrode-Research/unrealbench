#include "Gameplay/LinkAnimInstance.h"
#include "Gameplay/LinkCharacter.h"
#include "Gameplay/LinkAudio.h"
#include "Gameplay/LinkUnityPoseGrid.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
class FLinkAnimProxy : public FAnimInstanceProxy
{
public:
    explicit FLinkAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
    virtual void PreUpdate(UAnimInstance* Instance,float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(Instance,DeltaSeconds);
        auto* Source=CastChecked<ULinkAnimInstance>(Instance);
        Source->AdvanceSourceAnimation(DeltaSeconds);
        Frame=Source->GetSourceFrame();
        Grid=Source->GetPoseGrid();
        for(int32 Index=0;Index<4;++Index){Clips[Index]=Source->GetSourceClip(Index);}
    }
    virtual bool Evaluate(FPoseContext& Output) override
    {
        for(int32 Index=0;Index<4;++Index)
        {
            // Preserve native sequence precision whenever only one source role
            // contributes; the grid is needed for humanoid blends/transitions.
            if(Clips[Index]&&Frame.Weights[Index]>=1.f-ZERO_ANIMWEIGHT_THRESH)
            {
                Output.ResetToRefPose();FAnimationPoseData PoseData(Output);
                Clips[Index]->GetAnimationPose(PoseData,FAnimExtractContext(FMath::Frac(Frame.Phases[Index])*Clips[Index]->GetPlayLength(),false));
                return true;
            }
        }
        if(Grid&&Grid->Evaluate(Frame,Output)){return true;}
        TArray<FCompactPose,TInlineAllocator<4>> Poses;
        TArray<FBlendedCurve,TInlineAllocator<4>> Curves;
        TArray<UE::Anim::FStackAttributeContainer,TInlineAllocator<4>> Attributes;
        TArray<float,TInlineAllocator<4>> Weights;
        for(int32 Index=0;Index<4;++Index)
        {
            if(!Clips[Index]||Frame.Weights[Index]<=ZERO_ANIMWEIGHT_THRESH){continue;}
            FPoseContext Context(Output);Context.ResetToRefPose();
            FAnimationPoseData PoseData(Context);
            const double Time=FMath::Frac(Frame.Phases[Index])*Clips[Index]->GetPlayLength();
            Clips[Index]->GetAnimationPose(PoseData,FAnimExtractContext(Time,false));
            Poses.AddDefaulted();Poses.Last().MoveBonesFrom(Context.Pose);
            Curves.AddDefaulted();Curves.Last().MoveFrom(Context.Curve);
            Attributes.AddDefaulted();Attributes.Last().MoveFrom(Context.CustomAttributes);
            Weights.Add(Frame.Weights[Index]);
        }
        if(Poses.IsEmpty()){Output.ResetToRefPose();return true;}
        FAnimationPoseData Result(Output);
        FAnimationRuntime::BlendPosesTogether(Poses,Curves,Attributes,Weights,Result);
        return true;
    }
private:
    UAnimSequence* Clips[4]={nullptr,nullptr,nullptr,nullptr};
    ULinkUnityPoseGrid* Grid=nullptr;
    FLinkLocomotionFrame Frame;
};
}

FAnimInstanceProxy* ULinkAnimInstance::CreateAnimInstanceProxy(){return new FLinkAnimProxy(this);}
void ULinkAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy){delete Proxy;}
void ULinkAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    auto* Character=Cast<ALinkCharacter>(TryGetPawnOwner());
    if(!Character){return;}
    const FString Identity=Character->GetIdentity()==0?TEXT("LT"):TEXT("RT");
    PoseGrid=LoadObject<ULinkUnityPoseGrid>(nullptr,*FString::Printf(TEXT("/Game/Characters/%s/UnityPoseGrid_%s"),*Identity,*Identity));
    Clips.Reset();
    for(const TCHAR* Name:{TEXT("Idle"),TEXT("WalkInPlace"),TEXT("Walk"),TEXT("Run")})
    {
        Clips.Add(LoadObject<UAnimSequence>(nullptr,*FString::Printf(TEXT("/Game/Characters/%s/A_%s_%s"),*Identity,*Identity,Name)));
    }
    GConfig->GetFloat(TEXT("Foundations.Motion"),TEXT("WalkSpeed"),Locomotion.Config.NominalSpeed,GGameIni);
    GConfig->GetFloat(TEXT("Foundations.Motion"),TEXT("Smoothing"),Locomotion.Config.Smoothing,GGameIni);
    SettleToIdle();
}
bool ULinkAnimInstance::HasSourceClips() const{return Clips.Num()==4&&!Clips.Contains(nullptr)&&PoseGrid&&PoseGrid->IsValidData();}
UAnimSequence* ULinkAnimInstance::GetSourceClip(int32 Index) const{return Clips.IsValidIndex(Index)?Clips[Index].Get():nullptr;}
void ULinkAnimInstance::SettleToIdle(){Locomotion.Reset();SmoothedSpeed=0;Frame=Locomotion.Advance(0,0);}
void ULinkAnimInstance::AdvanceSourceAnimation(float DeltaSeconds)
{
#if WITH_DEV_AUTOMATION_TESTS
    if(bFixtureFrame){return;}
#endif
    auto* Character=Cast<ALinkCharacter>(TryGetPawnOwner());
    if(!Character||!HasSourceClips()){return;}
    const auto Input=Character->GetAnimationMotionInput();
    SmoothedSpeed=FLinkLocomotion::SmoothSpeed(SmoothedSpeed,Input.ActualSpeed,Input.DesiredSpeed,Input.RemainingDistance,Input.StopDistance,DeltaSeconds,Locomotion.Config);
    Frame=Locomotion.Advance(DeltaSeconds,SmoothedSpeed);
    for(const auto& Step:Frame.Steps)
    {
        Character->OnSourceAnimationStep(SmoothedSpeed);
    }
}
