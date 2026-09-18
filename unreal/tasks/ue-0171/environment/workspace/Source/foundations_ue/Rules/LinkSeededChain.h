#pragma once
#include "CoreMinimal.h"

/** Point-constraint adapter that can start from an authored centreline.
 * Ring rendering and source joint/rigid-body equivalence remain separate work. */
struct FLinkSeededChain
{
    TArray<FVector> Points,Previous;
    TArray<double> Lengths;
    bool Initialize(const FVector& Start,const FVector& End,TConstArrayView<FVector> Centres)
    {
    // TODO: restore the documented scene pipeline behavior.
    return false;
}
    void Integrate(double Delta,const FVector& Gravity,const FVector& Start,const FVector& End)
    {
    // TODO: restore the documented scene pipeline behavior.
    
}
    void Constrain(int32 Iterations=12)
    {
    // TODO: restore the documented scene pipeline behavior.
    
}
};
