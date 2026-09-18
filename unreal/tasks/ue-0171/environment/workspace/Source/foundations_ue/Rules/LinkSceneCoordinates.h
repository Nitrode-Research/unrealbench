#pragma once

#include "CoreMinimal.h"

// SceneDataExport stores row-major Unity matrices acting on column vectors.
// Unreal matrices act on row vectors. Preserve the complete affine transform:
// FTransform decomposition would lose shear from rotated, nonuniform parents.
namespace LinkSceneCoordinates
{
    inline FVector Position(const FVector& UnityMetres)
    {
    // TODO: restore the documented scene pipeline behavior.
    return FVector::ZeroVector;
}

    inline FVector Direction(const FVector& UnityDirection)
    {
    // TODO: restore the documented scene pipeline behavior.
    return FVector::ZeroVector;
}

    inline bool Matrix(TConstArrayView<double> UnityRowMajor, FMatrix& Out)
    {
    // TODO: restore the documented scene pipeline behavior.
    return false;
}

    // Only reverse triangles when negative scale has been baked into vertices.
    // The cyclic Unity-to-Unreal axis permutation itself has determinant +1.
    inline bool ReversesBakedWinding(const FMatrix& Transform)
    {
    // TODO: restore the documented scene pipeline behavior.
    return false;
}
}
