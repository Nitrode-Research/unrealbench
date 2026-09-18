// Behavior derived from Interactions/Interactable.cs in the pinned source.
#pragma once
#include "CoreMinimal.h"
#include "Rules/LinkStory.h"

/** Participant allocation; physical arrival is reported separately by the world adapter. */
class FLinkInteractionState
{
public:
    bool Focus(int32 Player, const FVector& Position, const TArray<FVector>& Waypoints);
    void Leave(int32 Player);
    void SetReady(int32 Player, bool bInsideArea);
    bool IsActive(int32 Player) const { return Valid(Player) && Active[Player]; }
    bool IsReady(int32 Player) const { return Valid(Player) && Active[Player] && Ready[Player]; }
    int32 Waypoint(int32 Player) const { return Valid(Player) ? Assigned[Player] : INDEX_NONE; }
    int32 Presence(int32 Player, bool bAtAnotherInteraction) const;
    int32 GetInitiator() const { return SourceId(Initiator); }
    int32 GetListener() const { return SourceId(Listener); }
    uint8 ActiveMask() const { return (Active[0] ? 1 : 0) | (Active[1] ? 2 : 0); }
private:
    static bool Valid(int32 Player) { return Player == 0 || Player == 1; }
    static int32 SourceId(int32 Player) { return Player == 0 ? LinkFacts::LT : Player == 1 ? LinkFacts::RT : 0; }
    bool Active[2] = {false,false};
    bool Ready[2] = {false,false};
    int32 Assigned[2] = {INDEX_NONE,INDEX_NONE};
    int32 Initiator = INDEX_NONE, Listener = INDEX_NONE;
};
