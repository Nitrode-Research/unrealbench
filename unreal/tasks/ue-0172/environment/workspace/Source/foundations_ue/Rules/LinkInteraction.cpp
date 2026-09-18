#include "Rules/LinkInteraction.h"

bool FLinkInteractionState::Focus(int32 Player, const FVector& Position, const TArray<FVector>& Waypoints)
{
    // Restore the documented B gameplay contract.
    return {};
}

void FLinkInteractionState::Leave(int32 Player)
{
    // Restore the documented B gameplay contract.
    
}

void FLinkInteractionState::SetReady(int32 Player, bool bInsideArea)
{
    // Restore the documented B gameplay contract.
    
}

int32 FLinkInteractionState::Presence(int32 Player, bool bAtAnotherInteraction) const
{
    // Restore the documented B gameplay contract.
    return {};
}
