// Source behavior: PlayerConfig.asset, FollowState.cs and PlayerState.cs.
#pragma once
#include "CoreMinimal.h"

struct FLinkMotionTuning
{
    float WalkSpeed = 500;
    float Acceleration = 3000;
    float RotationSpeed = 400;
    float CloseDistance = 200;
    float MinDistance = 300;
    float MaxDistance = 600;
    float LimitDistance = 800;
    float MinTightDistance = 50;
    float MaxTightDistance = 150;
    float ReleaseGrace = 0.1f;
    static FLinkMotionTuning Load();
    bool IsValid() const;
};

struct FLinkFollowInput
{
    FVector Self = FVector::ZeroVector;
    FVector Other = FVector::ZeroVector;
    FVector OtherVelocity = FVector::ZeroVector;
    FVector OtherDestination = FVector::ZeroVector;
    FVector RememberedOtherDirection = FVector::ZeroVector;
    float RemainingPathDistance = 0;
    bool bTight = false;
};

struct FLinkFollowOutput
{
    FVector Destination;
    FVector RememberedOtherDirection;
    float Speed;
    float Acceleration;
    float StopDistance;
};

namespace LinkNavigation
{
    bool ClipPath(const TArray<FVector>& Corners, double Limit, FVector& OutPosition);
    FLinkFollowOutput Follow(const FLinkFollowInput& Input, const FLinkMotionTuning& Tuning);
}
