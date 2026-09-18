#include "Rules/LinkCommands.h"

FLinkCommandActions FLinkCommands::Update(const FLinkCommandFrame& Frame)
{
    FLinkCommandActions Actions;
    if ((Frame.Pressed & 3) != 0)
    {
        bBothPressed = false;
        LastBothTime = -ReleaseGrace;
    }
    if ((Frame.Held & 3) == 3)
    {
        bBothPressed = true;
        LastBothTime = Frame.RealTime;
    }
    const bool bOnlyOneHeld = (Frame.Held & 3) == 1 || (Frame.Held & 3) == 2;
    if (bOnlyOneHeld && Frame.RealTime - LastBothTime > ReleaseGrace) { bBothPressed = false; }

    // The source updates LT then RT. Preserve this ordering for simultaneous edges.
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const uint8 Bit = 1 << Index;
        const int32 Other = 1 - Index;
        if ((Frame.Pressed & Bit) && Leader == INDEX_NONE && Frame.bInsideViewport)
        {
            Focus = Index;
            if (Frame.Target == ELinkTargetKind::Ground)
            {
                Leader = Index;
                Actions.Navigate |= Bit;
            }
            else if (Frame.Target == ELinkTargetKind::Interaction) { Actions.Interact |= Bit; }
        }
        if ((Frame.Pressed & Bit) && Leader == Other && Frame.Target == ELinkTargetKind::Ground)
        {
            Actions.Follow |= Bit;
        }
        if (!(Frame.Held & Bit) && Leader == Index)
        {
            if (Frame.Target == ELinkTargetKind::Ground && (Frame.Held & (1 << Other)))
            {
                if (Frame.RealTime - LastBothTime > ReleaseGrace)
                {
                    Leader = Focus = Other;
                    Actions.Navigate |= 1 << Other;
                }
            }
            else { Leader = INDEX_NONE; }
        }
    }
    return Actions;
}
