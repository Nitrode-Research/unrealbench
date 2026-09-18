#pragma once
#include "CoreMinimal.h"
#include "Rules/LinkStory.h"

struct FLinkSaveSnapshot
{
    FString Map;
    bool bCompleted=false;
    // Transient pending-spawn instruction, never read from the save codec.
    bool bUseSceneStart=false;
    FVector Positions[2]={FVector::ZeroVector,FVector::ZeroVector};
    float Yaws[2]={0,0};
    FLinkFactMap Globals;
    TMap<FString,FLinkFactMap> Interactions;
};

namespace LinkSave
{
    FString Encode(const FLinkSaveSnapshot& Snapshot);
    /** Validate completely before assigning Out; a failed load cannot partially replace live state. */
    bool Decode(const FString& Json,const FLinkStoryDatabase& Database,FLinkSaveSnapshot& Out,FString& Error);
}
