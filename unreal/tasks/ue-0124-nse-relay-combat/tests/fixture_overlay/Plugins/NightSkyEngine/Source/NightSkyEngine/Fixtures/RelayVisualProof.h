#pragma once
#include "RelaySample.h"
#include "RelayVisualProof.generated.h"

// Evaluation-only scene scheduling and observation. It supplies no relay behavior.
UCLASS()
class NIGHTSKYENGINE_API ARelayVisualProofGameMode : public ARelaySampleGameMode
{
    GENERATED_BODY()
  public:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

  private:
    FString ProofDirectory;
    int32 ProofWarmup = 30;
    int32 ProofFrame = 0;
    int32 ProofWait = 0;
    bool ProofPending = false;
};
