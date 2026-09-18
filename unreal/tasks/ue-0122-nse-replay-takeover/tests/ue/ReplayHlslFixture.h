#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Editor-only procedural HLSL rendering fixture. All calls, including destruction,
 * must run on the game thread. The world is borrowed and may be destroyed first.
 *
 * Eight concurrent bursts share one native opaque plane. Their positions are
 * projected into X/Z; the fixed 128x128 capture covers [-500, 500] in both axes.
 * Age advances only through Advance(), with a 0.5 second lifetime. The fixture
 * contains no replay policy, world ticking, engine-time inputs, or compute state.
 */
class FReplayHlslFixture
{
public:
    explicit FReplayHlslFixture(UWorld* World);
    ~FReplayHlslFixture();

    FReplayHlslFixture(const FReplayHlslFixture&) = delete;
    FReplayHlslFixture& operator=(const FReplayHlslFixture&) = delete;
    FReplayHlslFixture(FReplayHlslFixture&&) = delete;
    FReplayHlslFixture& operator=(FReplayHlslFixture&&) = delete;

    /** Requires a real RHI, native plane, render target, and compiled custom material. */
    bool IsReady() const;

    /** False for invalid coordinates, unavailable rendering, or eight active bursts. */
    bool Spawn(const FVector& WorldLocation);

    /** Negative/nonfinite deltas are ignored. Expired slots remain shader-black. */
    void Advance(float DeltaSeconds);

    /** Reset all logical bursts and deterministic seeds, retaining render resources. */
    void Clear();

    /** Actual render-target readback; empty on failure. Does not advance logical age. */
    TArray<FColor> Capture();

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
