#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "RelaySample.h"
#include "RelayPeerWorker.generated.h"
UCLASS()
class NIGHTSKYENGINE_API ARelayPeerGameMode : public ARelaySampleGameMode
{
    GENERATED_BODY()
  public:
    ARelayPeerGameMode();
};
/** Native peer diagnostics and a fixed public controller input tape; no relay policy. */
UCLASS()
class NIGHTSKYENGINE_API URelayPeerWorker : public UGameInstanceSubsystem,
                                            public FTickableGameObject
{
    GENERATED_BODY()
  public:
    virtual void Initialize(FSubsystemCollectionBase &Collection) override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override
    {
        return Enabled && !IsTemplate();
    }
    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(URelayPeerWorker, STATGROUP_Tickables);
    }
    void Observe(ANightSkyGameState *Battle, int32 Input1, int32 Input2, bool Resimulation);

  private:
    FString Directory, Role;
    bool Enabled = false, Bound = false, Saved = false, GateBound = false, Staggered = false;
    int32 GateRelease = -1, GateStart = -1, GateWindow = -1, GateInputFrame = -1;
    uint32 UsedWindows = 0;
    int32 ObservedFrames = 0;
    double Heartbeat = 0;
};
