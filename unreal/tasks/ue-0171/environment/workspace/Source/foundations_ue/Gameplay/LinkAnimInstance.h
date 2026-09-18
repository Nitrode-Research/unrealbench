#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Rules/LinkLocomotion.h"
#include "LinkAnimInstance.generated.h"

// Native pose playback; source state/clock/event rules live in LinkLocomotion.
UCLASS(Transient)
class FOUNDATIONS_UE_API ULinkAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    virtual void NativeInitializeAnimation() override;
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
    void AdvanceSourceAnimation(float DeltaSeconds);
    void SettleToIdle();
    bool HasSourceClips() const;
    const FLinkLocomotionFrame& GetSourceFrame() const { return Frame; }
    class UAnimSequence* GetSourceClip(int32 Index) const;
    class ULinkUnityPoseGrid* GetPoseGrid() const{return PoseGrid;}
#if WITH_DEV_AUTOMATION_TESTS
    void SetFixtureFrame(const FLinkLocomotionFrame& Value){Frame=Value;bFixtureFrame=true;}
#endif
private:
    UPROPERTY() TArray<TObjectPtr<class UAnimSequence>> Clips;
    UPROPERTY() TObjectPtr<class ULinkUnityPoseGrid> PoseGrid;
    FLinkLocomotion Locomotion;
    FLinkLocomotionFrame Frame;
    float SmoothedSpeed=0;
#if WITH_DEV_AUTOMATION_TESTS
    bool bFixtureFrame=false;
#endif
};
