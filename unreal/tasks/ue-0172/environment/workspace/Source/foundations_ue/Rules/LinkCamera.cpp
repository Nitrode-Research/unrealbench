#include "Rules/LinkCamera.h"
#include "Misc/ConfigCacheIni.h"

FLinkCameraTuning FLinkCameraTuning::Load(bool bSourceScene)
{
    FLinkCameraTuning Value;
#define READ_CAMERA(Name) GConfig->GetFloat(TEXT("Foundations.Camera"),TEXT(#Name),Value.Name,GGameIni)
    READ_CAMERA(MinWidth); READ_CAMERA(PositionSharpness); READ_CAMERA(ZoomSharpness); READ_CAMERA(FocusWeight);
    READ_CAMERA(LeadTime); READ_CAMERA(Padding); READ_CAMERA(Pitch); READ_CAMERA(Yaw); READ_CAMERA(Distance);
#undef READ_CAMERA
    Value.MinWidth=FMath::Max(500.f,Value.MinWidth);
    Value.FocusWeight=FMath::Clamp(Value.FocusWeight,0.5f,0.8f);
    Value.PositionSharpness=FMath::Max(0.1f,Value.PositionSharpness);
    Value.ZoomSharpness=FMath::Max(0.1f,Value.ZoomSharpness);
    Value.LeadTime=FMath::Clamp(Value.LeadTime,0.f,0.3f);
    Value.Padding=FMath::Max(200.f,Value.Padding);
    if (bSourceScene)
    {
        Value.bSourceProjection=true;
        Value.Distance=5000;
        Value.LeadTime=0;
        GConfig->GetVector(TEXT("Foundations.SourceCamera"),TEXT("Forward"),Value.SourceForward,GGameIni);
        GConfig->GetVector(TEXT("Foundations.SourceCamera"),TEXT("Up"),Value.SourceUp,GGameIni);
#define READ_SOURCE_CAMERA(Name) GConfig->GetFloat(TEXT("Foundations.SourceCamera"),TEXT(#Name),Value.Name,GGameIni)
        READ_SOURCE_CAMERA(VerticalHalfHeight); READ_SOURCE_CAMERA(Distance); READ_SOURCE_CAMERA(NearClip); READ_SOURCE_CAMERA(FarClip);
        READ_SOURCE_CAMERA(CameraWeight); READ_SOURCE_CAMERA(FocusFixedStep); READ_SOURCE_CAMERA(FocusFrequency); READ_SOURCE_CAMERA(FocusDamping);
        READ_SOURCE_CAMERA(SoftZoneWidth); READ_SOURCE_CAMERA(SoftZoneHeight);
        GConfig->GetVector(TEXT("Foundations.SourceCamera"),TEXT("ResponseDamping"),Value.ResponseDamping,GGameIni);
#undef READ_SOURCE_CAMERA
        const FLinkCameraTuning Defaults;
        if (Value.SourceForward.ContainsNaN() || Value.SourceUp.ContainsNaN() ||
            FVector::CrossProduct(Value.SourceForward,Value.SourceUp).SizeSquared()<0.001)
        { Value.SourceForward=Defaults.SourceForward; Value.SourceUp=Defaults.SourceUp; }
        if (!FMath::IsFinite(Value.VerticalHalfHeight) || Value.VerticalHalfHeight<=0) { Value.VerticalHalfHeight=500; }
        if (!FMath::IsFinite(Value.Distance) || Value.Distance<=0) { Value.Distance=5000; }
        if (!FMath::IsFinite(Value.NearClip) || Value.NearClip<0) { Value.NearClip=30; }
        if (!FMath::IsFinite(Value.FarClip) || Value.FarClip<=Value.NearClip) { Value.NearClip=30; Value.FarClip=100000; }
        Value.CameraWeight=FMath::IsFinite(Value.CameraWeight)?FMath::Clamp(Value.CameraWeight,0.f,100.f):60;
        if(!FMath::IsFinite(Value.FocusFixedStep)||Value.FocusFixedStep<=0){Value.FocusFixedStep=.005f;}
        if(!FMath::IsFinite(Value.FocusFrequency)||Value.FocusFrequency<=0){Value.FocusFrequency=50;}
        if(!FMath::IsFinite(Value.FocusDamping)||Value.FocusDamping<0){Value.FocusDamping=50;}
        if(Value.ResponseDamping.ContainsNaN()){Value.ResponseDamping=FVector(.1,.5,.1);}
        Value.ResponseDamping=Value.ResponseDamping.ComponentMax(FVector::ZeroVector);
    }
    return Value;
}

void FLinkCameraState::Step(const FVector& Left, const FVector& Right, const FVector& LeftVelocity, const FVector& RightVelocity,
    int32 Focus, bool bTogether, float Aspect, float DeltaTime, const FLinkCameraTuning& Tuning, bool bSnap)
{
    if(Tuning.bSourceProjection)
    {
        StepSource((Left+Right)*.5,Aspect,DeltaTime,Tuning,bSnap);
        return;
    }
    const float LeftWeight=bTogether || Focus==INDEX_NONE ? 0.5f : Focus==0 ? Tuning.FocusWeight : 1.f-Tuning.FocusWeight;
    const FVector Center=Left*LeftWeight+Right*(1.f-LeftWeight);
    FVector Lead=(LeftVelocity*LeftWeight+RightVelocity*(1.f-LeftWeight))*Tuning.LeadTime;
    Lead.Z=0;
    Lead=Lead.GetClampedToMaxSize(100);
    const FVector Target=Center+Lead;
    const bool bReset=bSnap || !bInitialized || FVector::DistSquared(Aim,Target)>FMath::Square(1500.0);
    const float PositionAlpha=1.f-FMath::Exp(-Tuning.PositionSharpness*FMath::Max(0.f,DeltaTime));
    Aim=bReset ? Target : FMath::Lerp(Aim,Target,PositionAlpha);
    const FVector ScreenRight=Tuning.Rotation().RotateVector(FVector::RightVector);
    const FVector ScreenUp=Tuning.Rotation().RotateVector(FVector::UpVector);
    float Horizontal=0, Vertical=0;
    for (const FVector& Point : {Left,Right})
    {
        Horizontal=FMath::Max(Horizontal,float(FMath::Abs(FVector::DotProduct(Point-Aim,ScreenRight))));
        Vertical=FMath::Max(Vertical,float(FMath::Abs(FVector::DotProduct(Point-Aim,ScreenUp))));
    }
    const float TargetWidth=FMath::Max3(Tuning.MinWidth,2*Horizontal+Tuning.Padding,(2*Vertical+Tuning.Padding)*FMath::Max(0.2f,Aspect));
    const float ZoomAlpha=1.f-FMath::Exp(-Tuning.ZoomSharpness*FMath::Max(0.f,DeltaTime));
    Width=bReset || TargetWidth>Width ? TargetWidth : FMath::Lerp(Width,TargetWidth,ZoomAlpha);
    bInitialized=true;
}

void FLinkCameraState::StepSource(const FVector& Target,float Aspect,float DeltaTime,const FLinkCameraTuning& Tuning,bool bSnap)
{
    Width=2*Tuning.VerticalHalfHeight*FMath::Max(.01f,Aspect);
    if(bSnap||!bInitialized||DeltaTime<0){Aim=Target;bInitialized=true;return;}
    const FQuat Rotation=Tuning.Rotation().Quaternion();
    FVector Delta=Rotation.UnrotateVector(Target-Aim);
    auto Damp=[DeltaTime](double Amount,double Seconds)
    {
        if(Seconds<.0001||FMath::Abs(Amount)<.01){return Amount;}
        return DeltaTime<.0001?0.:Amount*(1-FMath::Exp(-4.605170186*DeltaTime/Seconds));
    };
    // UE camera axes are forward/right/up; source damping is right/up/forward.
    const FVector Offset(Damp(Delta.X,Tuning.ResponseDamping.Z),Damp(Delta.Y,Tuning.ResponseDamping.X),Damp(Delta.Z,Tuning.ResponseDamping.Y));
    Aim+=Rotation.RotateVector(Offset);
    Delta=Rotation.UnrotateVector(Target-Aim);
    const double Horizontal=Tuning.SoftZoneWidth*Tuning.VerticalHalfHeight*Aspect;
    const double Vertical=Tuning.SoftZoneHeight*Tuning.VerticalHalfHeight;
    // Keep the real follow target inside the source hard guides after damping.
    Aim+=Rotation.RotateVector(FVector(0,Delta.Y-FMath::Clamp(Delta.Y,-Horizontal,Horizontal),Delta.Z-FMath::Clamp(Delta.Z,-Vertical,Vertical)));
}
