#include "Rules/LinkInteractionCamera.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FLinkInteractionCameraLibrary::Load()
{
    // Restore the documented A gameplay contract.
    return {};
}
bool FLinkInteractionCameraLibrary::Parse(const FString& Text)
{
    // Restore the documented A gameplay contract.
    return {};
}
double FLinkCameraTransition::Weight() const
{
    // Restore the documented A gameplay contract.
    return {};
}
TMap<FName,double> FLinkCameraTransition::Weights() const
{
    // Restore the documented A gameplay contract.
    return {};
}
void FLinkCameraTransition::Step(FName Requested,float DeltaTime,float BlendSeconds,bool bSnap)
{
    // Restore the documented A gameplay contract.
    
}
FLinkCameraPose FLinkCameraTransition::Evaluate(TFunctionRef<FLinkCameraPose(FName)> PoseFor) const
{
    // Restore the documented A gameplay contract.
    return {};
}
