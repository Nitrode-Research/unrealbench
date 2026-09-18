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
        const float Speed=Remaining>Stop+Tuning.AnticipationDistance?(Actual+Desired)*0.5f:Actual;
        return FMath::Lerp(Previous,Speed/Tuning.NominalSpeed,FMath::Clamp(Tuning.Smoothing*Delta,0.f,1.f));
    }
    static void BlendWeights(float Speed,float (&Weights)[3])
    {
        const float Value=FMath::Clamp(Speed,0.f,1.f);
        Weights[0]=FMath::Max(0.f,1.f-2.f*Value);
        Weights[2]=FMath::Max(0.f,2.f*Value-1.f);
        Weights[1]=1.f-Weights[0]-Weights[2];
    }
    void Reset(){bIdle=true;bTransition=false;bNextIdle=false;Phase=NextPhase=Transition=0;}
    FLinkLocomotionFrame Advance(float Delta,float Speed)
    {
        float MovingWeights[3];BlendWeights(Speed,MovingWeights);
        const float MovingLength=MovingWeights[0]*Config.Lengths[1]+MovingWeights[1]*Config.Lengths[2]+MovingWeights[2]*Config.Lengths[3];
        const bool HadMovingState=!bIdle||(bTransition&&!bNextIdle);
        const float PreviousMovingPhase=bIdle?NextPhase:Phase;
        float AdvancedMovingPhase=PreviousMovingPhase;
        if(FMath::IsFinite(Delta)&&Delta>0&&FMath::IsFinite(Speed))
        {
            Phase+=Delta/(bIdle?Config.Lengths[0]:MovingLength);
            if(!bIdle){AdvancedMovingPhase=Phase;}
            if(bTransition)
            {
                NextPhase+=Delta/(bNextIdle?Config.Lengths[0]:MovingLength);
                if(!bNextIdle){AdvancedMovingPhase=NextPhase;}
                // Unity accumulates normalized transition time. Preserving float
                // accumulation also preserves its endpoint frame at 30/60/120 Hz.
                Transition+=Delta/(bNextIdle?Config.ExitSeconds:Config.EnterSeconds);
                if(Transition>=1.f)
                {
                    bIdle=bNextIdle;bTransition=false;bNextIdle=false;
                    Phase=NextPhase;NextPhase=Transition=0;
                }
            }
            else if((bIdle&&Speed>Config.Threshold)||(!bIdle&&Speed<Config.Threshold))
            {
                // Conditions are evaluated after advancing the current state.
                // The destination starts at phase zero on this frame.
                bTransition=true;bNextIdle=!bIdle;NextPhase=Transition=0;
            }
        }
        FLinkLocomotionFrame Result;
        Result.bIdle=bIdle;Result.bTransition=bTransition;Result.bNextIdle=bNextIdle;
        Result.Phase=Phase;Result.NextPhase=NextPhase;Result.Transition=Transition;Result.Speed=Speed;
        Result.Length=bIdle?Config.Lengths[0]:MovingLength;
        Result.NextLength=bTransition?(bNextIdle?Config.Lengths[0]:MovingLength):0;
        const float MovingWeight=bTransition?(bIdle?Transition:1.f-Transition):(bIdle?0.f:1.f);
        Result.Weights[0]=1.f-MovingWeight;
        Result.Phases[0]=bIdle?Phase:NextPhase;
        for(int32 Clip=0;Clip<3;++Clip)
        {
            Result.Weights[Clip+1]=MovingWeights[Clip]*MovingWeight;
            Result.Phases[Clip+1]=bIdle?NextPhase:Phase;
            // The source callback accepts a step only above 50% final clip
            // weight, including transition weight. Equal blends are silent.
            if(HadMovingState&&Result.Weights[Clip+1]>0.5f)
            {
                for(int32 Cycle=FMath::FloorToInt(PreviousMovingPhase);Cycle<=FMath::FloorToInt(AdvancedMovingPhase);++Cycle)
                {
                    for(int32 Event=0;Event<2;++Event)
                    {
                        const float EventPhase=Config.EventSeconds[Clip][Event]/Config.Lengths[Clip+1];
                        const float Crossing=Cycle+EventPhase;
                        if(PreviousMovingPhase<Crossing&&AdvancedMovingPhase>=Crossing)
                        {
                            Result.Steps.Add({Clip+1,EventPhase,Result.Weights[Clip+1]});
                        }
                    }
                }
            }
        }
        return Result;
    }
private:
    bool bIdle=true,bTransition=false,bNextIdle=false;
    float Phase=0,NextPhase=0,Transition=0;
};
