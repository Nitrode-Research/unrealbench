#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Rules/LinkLocomotion.h"
#include "LinkUnityPoseGrid.generated.h"

struct FPoseContext;

// Content adapter for Unity's humanoid muscle/retarget blend result. State,
// timing, thresholds and event rules remain native C++ in LinkLocomotion.
UCLASS(BlueprintType)
class FOUNDATIONS_UE_API ULinkUnityPoseGrid : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) TArray<FName> SourceBones;
    UPROPERTY(EditAnywhere) TArray<FQuat> SourceRestRotations;
    UPROPERTY(EditAnywhere) TArray<int32> MovingDimensions;
    UPROPERTY(EditAnywhere) TArray<int32> EnteringDimensions;
    UPROPERTY(EditAnywhere) TArray<int32> ExitingDimensions;
    UPROPERTY(EditAnywhere) TArray<float> MovingPoses;
    UPROPERTY(EditAnywhere) TArray<float> EnteringPoses;
    UPROPERTY(EditAnywhere) TArray<float> ExitingPoses;
    bool IsValidData() const;
    bool Evaluate(const FLinkLocomotionFrame& Frame,FPoseContext& Output) const;
};
