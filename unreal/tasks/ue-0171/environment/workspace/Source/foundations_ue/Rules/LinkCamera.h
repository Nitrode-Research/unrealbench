#pragma once
#include "CoreMinimal.h"

struct FLinkCameraTuning
{
    float MinWidth=1450, PositionSharpness=5, ZoomSharpness=3, FocusWeight=0.62f;
    float LeadTime=0.14f, Padding=300, Pitch=50, Yaw=45, Distance=2200;
    bool bSourceProjection=false;
    float VerticalHalfHeight=500, NearClip=30, FarClip=100000;
    float CameraWeight=60,FocusFixedStep=.005f,FocusFrequency=50,FocusDamping=50;
    // Camera-space right, up and forward damping times (99% decay), in seconds.
    FVector ResponseDamping=FVector(.1,.5,.1);
    float SoftZoneWidth=.8f,SoftZoneHeight=.8f;
    FVector SourceForward=FVector(0.526540816,0.627506971,-0.573576391);
    FVector SourceUp=FVector(0.368687838,0.439384997,0.819152057);
    static FLinkCameraTuning Load(bool bSourceScene=false);
    FRotator Rotation() const
    {
        return bSourceProjection ? FRotationMatrix::MakeFromXZ(SourceForward,SourceUp).Rotator() : FRotator(-Pitch,Yaw,0);
    }
};

/** Shared follow state. Source scenes use a fixed vertical lens; focus/damping parity is separate. */
struct FLinkCameraState
{
    FVector Aim=FVector::ZeroVector;
    float Width=1450;
    bool bInitialized=false;
    void StepSource(const FVector& Target,float Aspect,float DeltaTime,const FLinkCameraTuning& Tuning,bool bSnap=false);
    void Step(const FVector& Left, const FVector& Right, const FVector& LeftVelocity, const FVector& RightVelocity,
        int32 Focus, bool bTogether, float Aspect, float DeltaTime, const FLinkCameraTuning& Tuning, bool bSnap=false);
};
