#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LinkWorldEffect.generated.h"

/** Source fact adapters for window/trailer variants, hinged geometry and floodlight output. */
UCLASS()
class FOUNDATIONS_UE_API ALinkWorldEffect : public AActor
{
    GENERATED_BODY()
public:
    ALinkWorldEffect();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    bool IsActivated() const{return bActivated;}
    UPROPERTY(EditAnywhere,Category="Effect") int32 InputFact=0;
    UPROPERTY(EditAnywhere,Category="Effect") FString InteractionId;
    UPROPERTY(EditAnywhere,Category="Effect") int32 OutputFact=0;
    UPROPERTY(EditAnywhere,Category="Effect") FName TrueTag;
    UPROPERTY(EditAnywhere,Category="Effect") FName FalseTag;
    UPROPERTY(EditAnywhere,Category="Effect") FName MovingTag;
    UPROPERTY(EditAnywhere,Category="Effect") FVector ActiveOffset=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,Category="Effect") float ActiveYaw=0;
    UPROPERTY(EditAnywhere,Category="Effect") FName LightTag;
    UPROPERTY(EditAnywhere,Category="Effect") float LightIntensity=8000;
private:
    void Apply(float DeltaTime,bool bSnap);
    bool bActivated=false;
    float Blend=0;
    TMap<TWeakObjectPtr<AActor>,FTransform> Movables;
};
