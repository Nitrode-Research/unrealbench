#pragma once
#include "CoreMinimal.h"

struct FLinkInteractionCameraProfile
{
    FName Id,Scene;
    FVector Target=FVector::ZeroVector;
    TArray<FVector> Waypoints;
    float HalfHeight=400;
    int32 Priority=100;
};
struct FLinkInteractionCameraLibrary
{
    TMap<FName,FLinkInteractionCameraProfile> Profiles;
    float BlendSeconds=.6f;
    int32 ExplorationPriority=10;
    bool Load();
    bool Parse(const FString& Text);
};
struct FLinkCameraPose
{
    FVector Position=FVector::ZeroVector;
    double HalfHeight=500;
};

/** A blend keeps live endpoint poses. An interrupted blend freezes its weights,
 * not its world position, so moving exploration continues to update underneath. */
struct FLinkCameraTransition
{
    FName Selected=TEXT("Explore"),Backtrack=NAME_None;
    TMap<FName,double> From;
    float Elapsed=0,Duration=0,StartFraction=0;
    bool bInitialized=false;
    void Step(FName Requested,float DeltaTime,float BlendSeconds,bool bSnap=false);
    double Weight() const;
    TMap<FName,double> Weights() const;
    FLinkCameraPose Evaluate(TFunctionRef<FLinkCameraPose(FName)> PoseFor) const;
};
