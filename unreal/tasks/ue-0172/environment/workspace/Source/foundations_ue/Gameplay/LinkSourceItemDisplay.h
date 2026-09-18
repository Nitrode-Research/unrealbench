#pragma once
#include "CoreMinimal.h"
#include "Engine/StaticMeshActor.h"
#include "LinkSourceItemDisplay.generated.h"

/** Source ItemDisplay visibility, with geometry authored from its original prefab. */
UCLASS()
class FOUNDATIONS_UE_API ALinkSourceItemDisplay : public AStaticMeshActor
{
    GENERATED_BODY()
public:
    ALinkSourceItemDisplay();
    UPROPERTY(EditAnywhere,Category="Source display") FString InteractionId;
    UPROPERTY(EditAnywhere,Category="Source display") int32 ItemFact=0;
    UPROPERTY(EditAnywhere,Category="Source display") int32 InitialItem=0;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    void RefreshVisibility();
};
