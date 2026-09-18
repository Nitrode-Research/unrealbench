#include "Rules/LinkInteraction.h"

bool FLinkInteractionState::Focus(int32 Player, const FVector& Position, const TArray<FVector>& Waypoints)
{
    if (!Valid(Player) || Position.ContainsNaN() || Waypoints.Num() < 2) { return false; }
    for (const auto& Point : Waypoints) { if (Point.ContainsNaN()) { return false; } }
    if (Active[Player]) { return true; }
    const int32 Other = 1 - Player;
    int32 Closest = 0;
    double ClosestDistance = TNumericLimits<double>::Max();
    for (int32 Index=0; Index<Waypoints.Num(); ++Index)
    {
        const double Distance = FVector::Distance(Position,Waypoints[Index]);
        if (Distance < ClosestDistance) { ClosestDistance = Distance; Closest = Index; }
    }
    if (Active[Other] && Assigned[Other] == Closest)
    {
        int32 Better = INDEX_NONE;
        int32 Replacement = Closest == 0 ? 1 : 0;
        double BestDistance = TNumericLimits<double>::Max();
        double ReplacementDistance = TNumericLimits<double>::Max();
        for (int32 Index=0; Index<Waypoints.Num(); ++Index)
        {
            if (Index == Closest) { continue; }
            const double Distance = FVector::Distance(Position,Waypoints[Index]);
            const double Dot = FVector::DotProduct(Position-Waypoints[Closest],Waypoints[Index]-Waypoints[Closest]);
            if (Distance < BestDistance && Dot > 0) { Better = Index; BestDistance = Distance; }
            const double OtherDistance = FVector::Distance(Waypoints[Closest],Waypoints[Index]);
            if (OtherDistance < ReplacementDistance) { Replacement = Index; ReplacementDistance = OtherDistance; }
        }
        if (Better != INDEX_NONE) { Closest = Better; }
        else { Assigned[Other] = Replacement; }
    }
    Assigned[Player] = Closest;
    Active[Player] = true;
    Ready[Player] = false;
    if (!Active[Other]) { Initiator = Player; }
    else { Listener = Player; }
    return true;
}

void FLinkInteractionState::Leave(int32 Player)
{
    if (!Valid(Player)) { return; }
    Active[Player] = Ready[Player] = false;
    Assigned[Player] = INDEX_NONE;
    Listener = INDEX_NONE;
    Initiator = Active[1-Player] ? 1-Player : INDEX_NONE;
}

void FLinkInteractionState::SetReady(int32 Player, bool bInsideArea)
{
    if (Valid(Player) && Active[Player]) { Ready[Player] = bInsideArea; }
}

int32 FLinkInteractionState::Presence(int32 Player, bool bAtAnotherInteraction) const
{
    if (!Valid(Player)) { return 0; }
    if (Active[Player]) { return 1; }
    return bAtAnotherInteraction ? -1 : 0;
}
