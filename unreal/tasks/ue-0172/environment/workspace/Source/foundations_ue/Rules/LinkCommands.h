// Behavior derived from Solomon's Link ExploreState.cs; see THIRD_PARTY_NOTICES.md.
#pragma once

#include "CoreMinimal.h"

enum class ELinkTargetKind : uint8 { None, Ground, Interaction };

struct FLinkCommandFrame
{
    uint8 Pressed = 0; // bit 0 = LT, bit 1 = RT; performed this frame
    uint8 Held = 0;
    double RealTime = 0;
    bool bInsideViewport = true;
    ELinkTargetKind Target = ELinkTargetKind::Ground;
};

struct FLinkCommandActions
{
    uint8 Navigate = 0;
    uint8 Follow = 0;
    uint8 Interact = 0;
};

/** Input arbitration only. No world, clock, input device, or navigation dependency. */
class FLinkCommands
{
public:
    explicit FLinkCommands(double InReleaseGrace = 0.1) : ReleaseGrace(InReleaseGrace) {}
    FLinkCommandActions Update(const FLinkCommandFrame& Frame);
    int32 GetLeader() const { return Leader; }
    int32 GetFocus() const { return Focus; }
    bool IsTightFollowing() const { return bBothPressed; }
    void Reset() { Leader = Focus = INDEX_NONE; bBothPressed = false; LastBothTime = 0; }

private:
    int32 Leader = INDEX_NONE;
    int32 Focus = INDEX_NONE;
    bool bBothPressed = false;
    double LastBothTime = 0;
    double ReleaseGrace = 0.1;
};
