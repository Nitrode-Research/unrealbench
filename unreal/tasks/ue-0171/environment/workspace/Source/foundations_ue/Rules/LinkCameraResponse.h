// Source behavior: PlayerManager, ExploreState and SpringTween; see THIRD_PARTY_NOTICES.md.
#pragma once
#include "CoreMinimal.h"
struct FLinkCameraTuning;

enum class ELinkCameraFocus : uint8 { None=0, Left=1, Right=2, Both=3 };

/** Focus is retained when the selected character stops or the dialogue manager is active. */
ELinkCameraFocus ResolveLinkCameraFocus(ELinkCameraFocus Previous,ELinkCameraFocus Current,
    bool bEligible,bool bSharedInteraction,bool bBothPressed,bool bDialogue);

/** Source RT weight spring. Values outside [0,1] are intentional; group validity handles them. */
struct FLinkCameraFocusState
{
    ELinkCameraFocus Focus=ELinkCameraFocus::None;
    float Position=.5f,Velocity=0;
    double Remainder=0;
    void StepFixed(float DeltaTime,const FLinkCameraTuning& Tuning);
    void Advance(double DeltaTime,const FLinkCameraTuning& Tuning);
    void Settle() { Velocity=0; }
    FVector GroupPosition(const FVector& Left,const FVector& Right) const;
};
