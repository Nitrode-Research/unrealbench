#include "Rules/LinkCamera.h"
#include "Misc/ConfigCacheIni.h"

FLinkCameraTuning FLinkCameraTuning::Load(bool bSourceScene)
{
    // Restore the documented A gameplay contract.
    return {};
}

void FLinkCameraState::Step(const FVector& Left, const FVector& Right, const FVector& LeftVelocity, const FVector& RightVelocity,
    int32 Focus, bool bTogether, float Aspect, float DeltaTime, const FLinkCameraTuning& Tuning, bool bSnap)
{
    // Restore the documented A gameplay contract.
    
}

void FLinkCameraState::StepSource(const FVector& Target,float Aspect,float DeltaTime,const FLinkCameraTuning& Tuning,bool bSnap)
{
    // Restore the documented A gameplay contract.
    
}
