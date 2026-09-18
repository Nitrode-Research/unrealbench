#include "Rules/LinkNavigation.h"
#include "Misc/ConfigCacheIni.h"

FLinkMotionTuning FLinkMotionTuning::Load()
{
    FLinkMotionTuning Value;
    const TCHAR* Section = TEXT("Foundations.Motion");
#define READ_TUNING(Name) GConfig->GetFloat(Section, TEXT(#Name), Value.Name, GGameIni)
    READ_TUNING(WalkSpeed); READ_TUNING(Acceleration); READ_TUNING(RotationSpeed);
    READ_TUNING(CloseDistance); READ_TUNING(MinDistance); READ_TUNING(MaxDistance);
    READ_TUNING(LimitDistance); READ_TUNING(MinTightDistance); READ_TUNING(MaxTightDistance);
    READ_TUNING(ReleaseGrace);
#undef READ_TUNING
    if (!ensureAlwaysMsgf(Value.IsValid(), TEXT("Invalid Foundations.Motion configuration; using source defaults")))
    {
        return FLinkMotionTuning();
    }
    return Value;
}

bool FLinkMotionTuning::IsValid() const
{
    for (float Value : {WalkSpeed, Acceleration, RotationSpeed, CloseDistance, MinDistance, MaxDistance,
        LimitDistance, MinTightDistance, MaxTightDistance, ReleaseGrace})
    {
        if (!FMath::IsFinite(Value) || Value < 0) { return false; }
    }
    return WalkSpeed > 0 && Acceleration > 0 && CloseDistance > 0 && LimitDistance > 0
        && MaxDistance > MinDistance && MaxTightDistance > MinTightDistance;
}

bool LinkNavigation::ClipPath(const TArray<FVector>& Corners, double Limit, FVector& OutPosition)
{
    if (!FMath::IsFinite(Limit) || Limit < 0) { return false; }
    double Length = 0;
    for (int32 Index = 1; Index < Corners.Num(); ++Index)
    {
        if (Corners[Index - 1].ContainsNaN() || Corners[Index].ContainsNaN()) { return false; }
        const double Segment = FVector::Distance(Corners[Index - 1], Corners[Index]);
        if (Length + Segment > Limit && Segment > UE_SMALL_NUMBER)
        {
            OutPosition = FMath::Lerp(Corners[Index - 1], Corners[Index], (Limit - Length) / Segment);
            return true;
        }
        Length += Segment;
    }
    return false;
}

FLinkFollowOutput LinkNavigation::Follow(const FLinkFollowInput& Input, const FLinkMotionTuning& Tuning)
{
    const FVector Relative = Input.Self - Input.Other;
    const double Distance = Relative.Size();
    const FVector Direction = Distance > UE_SMALL_NUMBER ? Relative / Distance : FVector::ForwardVector;
    const FVector OtherDirection = Input.OtherVelocity.GetSafeNormal();
    const bool bWalkingTowards = FVector::DotProduct(OtherDirection, Direction) > 0.5;
    const float Min = Input.bTight ? Tuning.MinTightDistance : Tuning.MinDistance;
    const float Max = Input.bTight ? Tuning.MaxTightDistance : Tuning.MaxDistance;
    FLinkFollowOutput Output{Input.Other, Input.RememberedOtherDirection, 0, 800, Min};
    if (bWalkingTowards)
    {
        if (Distance < Tuning.CloseDistance)
        {
            if (!OtherDirection.IsNearlyZero()) { Output.RememberedOtherDirection = OtherDirection; }
            const FVector Perpendicular(-Output.RememberedOtherDirection.Y, Output.RememberedOtherDirection.X, 0);
            const FVector Escape = (Output.RememberedOtherDirection
                + (FVector::DotProduct(Perpendicular, Direction) > 0 ? Perpendicular : -Perpendicular)) * 0.5;
            Output.Destination = Input.Self + Escape * Tuning.CloseDistance;
            const float Range = FMath::Clamp(1.f - float(Distance) / Tuning.CloseDistance, 0.f, 1.f);
            Output.Speed = Range * Tuning.WalkSpeed;
            Output.Acceleration = Range * Tuning.Acceleration;
            Output.StopDistance = 0;
            return Output;
        }
        if (FVector::DotProduct((Input.Self - Input.OtherDestination).GetSafeNormal(), Direction) < 0.5)
        {
            return Output;
        }
        Output.Destination = Input.OtherDestination;
    }
    const float Range = FMath::Clamp((Input.RemainingPathDistance - Min) / (Max - Min), 0.f, 1.f);
    Output.Speed = FMath::Lerp(100.f, Tuning.WalkSpeed, Range);
    Output.Acceleration = FMath::Lerp(800.f, Tuning.Acceleration, Range);
    return Output;
}
