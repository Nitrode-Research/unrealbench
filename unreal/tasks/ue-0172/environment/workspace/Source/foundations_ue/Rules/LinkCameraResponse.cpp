#include "Rules/LinkCameraResponse.h"
#include "Rules/LinkCamera.h"

ELinkCameraFocus ResolveLinkCameraFocus(ELinkCameraFocus Previous,ELinkCameraFocus Current,
    bool bEligible,bool bSharedInteraction,bool bBothPressed,bool bDialogue)
{
    if(bDialogue||Current==ELinkCameraFocus::None){return Previous;}
    if(bSharedInteraction){return ELinkCameraFocus::Both;}
    return bEligible?(bBothPressed?ELinkCameraFocus::Both:Current):Previous;
}
void FLinkCameraFocusState::StepFixed(float DeltaTime,const FLinkCameraTuning& Tuning)
{
    if(DeltaTime<=0){return;}
    const float Offset=Tuning.CameraWeight/100.f;
    const float Target=Focus==ELinkCameraFocus::Left?.5f-Offset:Focus==ELinkCameraFocus::Right?.5f+Offset:.5f;
    const float Denominator=float(PI)*Tuning.FocusFrequency;
    const float K1=Tuning.FocusDamping/Denominator,K2=1.f/Denominator;
    const float Stable=FMath::Max3(K2,DeltaTime*DeltaTime/2+DeltaTime*K1/2,DeltaTime*K1);
    Position+=DeltaTime*Velocity;
    Velocity+=DeltaTime*(Target-Position-K1*Velocity)/Stable;
    if(FMath::Abs(Velocity)<.0001f&&FMath::Abs(Position-Target)<.0001f){Position=Target;Velocity=0;}
}
void FLinkCameraFocusState::Advance(double DeltaTime,const FLinkCameraTuning& Tuning)
{
    if(!FMath::IsFinite(DeltaTime)||DeltaTime<=0){return;}
    Remainder+=DeltaTime;
    while(Remainder+1e-10>=Tuning.FocusFixedStep)
    {StepFixed(Tuning.FocusFixedStep,Tuning);Remainder-=Tuning.FocusFixedStep;}
}
FVector FLinkCameraFocusState::GroupPosition(const FVector& Left,const FVector& Right) const
{
    // Cinemachine excludes members whose weight is at or below 0.0001. It does
    // not clamp the spring itself: at the default 60 setting the focused member
    // eventually becomes the only contributing member.
    const float RightWeight=Position>.0001f?Position:0;
    const float LeftWeight=1-Position>.0001f?1-Position:0;
    return (Right*RightWeight+Left*LeftWeight)/(RightWeight+LeftWeight);
}
