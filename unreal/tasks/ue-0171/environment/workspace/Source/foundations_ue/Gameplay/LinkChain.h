#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rules/LinkSeededChain.h"
#include "LinkChain.generated.h"

/** Visual/collision chain; navigation rules own character separation, as in the source. */
UCLASS()
class FOUNDATIONS_UE_API ALinkChain : public AActor
{
    GENERATED_BODY()
public:
    ALinkChain();
    void Initialize(class ALinkCharacter* Left, class ALinkCharacter* Right);
    bool InitializeAuthoredCurve(const TArray<FVector>& Centres);
    virtual void Tick(float DeltaSeconds) override;
    void GetPoints(TArray<FVector>& OutPoints) const;
    float GetRestLength() const;
    static constexpr float AttachmentOffset = -60.f;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UCableComponent> Cable;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> SeededTube;
    UPROPERTY() TObjectPtr<class ALinkCharacter> LeftCrew;
    UPROPERTY() TObjectPtr<class ALinkCharacter> RightCrew;
    FLinkSeededChain Seeded;
    float SeededRemainder=0;
    bool bSeeded=false;
    void UpdateSeededTube(bool bCreate);
};
