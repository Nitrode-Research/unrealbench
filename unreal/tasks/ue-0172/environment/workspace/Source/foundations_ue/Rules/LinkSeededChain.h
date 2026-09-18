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
        if (Centres.Num()!=82 || Start.ContainsNaN() || End.ContainsNaN()) { return false; }
        for (const FVector& P:Centres) { if (P.ContainsNaN()) { return false; } }
        Points.Reset(); Points.Add(Start); Points.Append(Centres.GetData(),Centres.Num()); Points.Add(End); Previous=Points;
        Lengths.Init(10.0,Points.Num()-1); Lengths[0]=Lengths.Last()=5.0;
        return true;
    }
    void Integrate(double Delta,const FVector& Gravity,const FVector& Start,const FVector& End)
    {
        Points[0]=Previous[0]=Start; Points.Last()=Previous.Last()=End;
        for (int32 I=1;I<Points.Num()-1;++I)
        {
            const FVector P=Points[I]; Points[I]+=P-Previous[I]+Gravity*(Delta*Delta); Previous[I]=P;
        }
    }
    void Constrain(int32 Iterations=12)
    {
        for (int32 Iteration=0;Iteration<Iterations;++Iteration) for (int32 I=0;I<Lengths.Num();++I)
        {
            const FVector Delta=Points[I+1]-Points[I]; const double Distance=Delta.Size();
            if (Distance<1e-8) { continue; }
            const FVector Correction=Delta*((Distance-Lengths[I])/Distance);
            const bool A=I>0,B=I+1<Points.Num()-1;
            if (A && B) { Points[I]+=Correction*.5; Points[I+1]-=Correction*.5; }
            else if (A) { Points[I]+=Correction; }
            else if (B) { Points[I+1]-=Correction; }
        }
    }
};
