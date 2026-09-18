#pragma once
#include "CoreMinimal.h"

// Values captured from Unity 2022.3.6f1 PlayerAnimator and its imported clips.
// Clip order: separate Idle, WalkInPlace, Walking, Jog Forward.
struct FLinkLocomotionConfig
{
    float Threshold=0.1f, EnterSeconds=0.05f, ExitSeconds=0.2f;
    float NominalSpeed=500.f, Smoothing=10.f, AnticipationDistance=100.f;
    float Lengths[4]={1.033333420753479f,1.033333420753479f,1.033333420753479f,0.8166667222976685f};
    float EventSeconds[3][2]={{0.289333313703537f,0.8059999346733093f},{0.289333313703537f,0.8059999346733093f},{0.22866666316986084f,0.6369999647140503f}};
};

struct FLinkAnimationStep
{
    int32 Clip=0;
    float EventPhase=0, Weight=0;
};

struct FLinkLocomotionFrame
{
    bool bIdle=true, bTransition=false, bNextIdle=false;
    float Phase=0, NextPhase=0, Transition=0, Speed=0, Length=0, NextLength=0;
    float Weights[4]={1,0,0,0};
    float Phases[4]={0,0,0,0};
    TArray<FLinkAnimationStep,TInlineAllocator<4>> Steps;
};

class FLinkLocomotion
{
public:
    FLinkLocomotionConfig Config;
    static float SmoothSpeed(float Previous,float Actual,float Desired,float Remaining,float Stop,float Delta,const FLinkLocomotionConfig& Tuning)
    {
    // Restore the documented A gameplay contract.
    return {};
}
    static void BlendWeights(float Speed,float (&Weights)[3])
    {
    // Restore the documented A gameplay contract.
    
}
    void Reset(){
    // Restore the documented A gameplay contract.
    
}
    FLinkLocomotionFrame Advance(float Delta,float Speed)
    {
    // Restore the documented A gameplay contract.
    return {};
}
private:
    bool bIdle=true,bTransition=false,bNextIdle=false;
    float Phase=0,NextPhase=0,Transition=0;
};
