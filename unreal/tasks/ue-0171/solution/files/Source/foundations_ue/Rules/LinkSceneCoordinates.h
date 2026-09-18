#pragma once

#include "CoreMinimal.h"

// SceneDataExport stores row-major Unity matrices acting on column vectors.
// Unreal matrices act on row vectors. Preserve the complete affine transform:
// FTransform decomposition would lose shear from rotated, nonuniform parents.
namespace LinkSceneCoordinates
{
    inline FVector Position(const FVector& UnityMetres)
    {
        return FVector(UnityMetres.Z, UnityMetres.X, UnityMetres.Y) * 100.0;
    }

    inline FVector Direction(const FVector& UnityDirection)
    {
        return FVector(UnityDirection.Z, UnityDirection.X, UnityDirection.Y);
    }

    inline bool Matrix(TConstArrayView<double> UnityRowMajor, FMatrix& Out)
    {
        if (UnityRowMajor.Num() != 16) { return false; }
        for (double Value : UnityRowMajor)
        {
            if (!FMath::IsFinite(Value)) { return false; }
        }
        if (!FMath::IsNearlyZero(UnityRowMajor[12]) || !FMath::IsNearlyZero(UnityRowMajor[13]) ||
            !FMath::IsNearlyZero(UnityRowMajor[14]) || !FMath::IsNearlyEqual(UnityRowMajor[15], 1.0))
        {
            return false;
        }
        constexpr int32 Axes[] = {2, 0, 1};
        Out = FMatrix::Identity;
        for (int32 Row = 0; Row < 3; ++Row)
        {
            for (int32 Column = 0; Column < 3; ++Column)
            {
                Out.M[Row][Column] = UnityRowMajor[Axes[Column] * 4 + Axes[Row]];
            }
            Out.M[3][Row] = UnityRowMajor[Axes[Row] * 4 + 3] * 100.0;
        }
        return true;
    }

    // Only reverse triangles when negative scale has been baked into vertices.
    // The cyclic Unity-to-Unreal axis permutation itself has determinant +1.
    inline bool ReversesBakedWinding(const FMatrix& Transform)
    {
        return Transform.RotDeterminant() < 0.0;
    }
}
