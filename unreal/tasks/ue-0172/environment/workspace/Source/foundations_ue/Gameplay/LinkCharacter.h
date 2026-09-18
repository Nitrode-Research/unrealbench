#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "LinkCharacter.generated.h"

enum class ELinkMotionMode : uint8 { Idle, Navigate, Follow, Interact };

struct FLinkAnimationMotionInput
{
    float ActualSpeed=0,DesiredSpeed=0,RemainingDistance=0,StopDistance=0;
};

UCLASS()
class FOUNDATIONS_UE_API ALinkCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    ALinkCharacter();
    void SetIdentity(int32 InIdentity);
    int32 GetIdentity() const { return Identity; }
    bool MoveTo(const FVector& Target, float StopDistance = 5.f, float Speed = 500.f, float Acceleration = 3000.f);
    void StopNavigation();
    void SettleForCompletion();
    FLinkAnimationMotionInput GetAnimationMotionInput() const;
    void OnSourceAnimationStep(float NormalizedSpeed);
    int32 GetAnimationStepCount() const { return AnimationStepCount; }
    ELinkMotionMode GetMotionMode() const { return MotionMode; }
    void SetMotionMode(ELinkMotionMode InMode) { MotionMode = InMode; }

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UStaticMeshComponent> Body;
    UPROPERTY()
    int32 Identity = 0;
    ELinkMotionMode MotionMode = ELinkMotionMode::Idle;
    FVector LastMoveGoal = FVector::ZeroVector;
    float LastStopDistance = -1;
    bool bHasMoveGoal = false;
    int32 AnimationStepCount=0;
};
