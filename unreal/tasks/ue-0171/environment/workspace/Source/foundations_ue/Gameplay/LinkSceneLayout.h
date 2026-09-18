#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LinkSceneLayout.generated.h"

/** Text-backed starting state for a source-based scene. Maps only select the definition. */
UCLASS()
class FOUNDATIONS_UE_API ALinkSceneLayout : public AActor
{
    GENERATED_BODY()
public:
    ALinkSceneLayout();
    UPROPERTY(EditAnywhere, Category="Source scene") FName SourceScene=TEXT("EntryGate");
    bool GetPlayerStart(int32 Index,FTransform& Out);
    const TArray<FVector>& GetChainCentres();
    static ALinkSceneLayout* Find(const UWorld* World);
private:
    bool Load();
    bool bLoaded=false,bValid=false;
    FTransform Starts[2];
    TArray<FVector> ChainCentres;
};
