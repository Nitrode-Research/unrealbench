#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "NightSkyEngine/Battle/Script/State.h"
#include "SweptProjectilePlaytest.generated.h"

class ANightSkyGameState;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;
class UPointLightComponent;
class USkeletalMeshComponent;
class APlayerObject;
class ABattleObject;
class ASweptShotEffect;

// A separate battle input bit keeps the demo shot out of the fighter's authored attacks.
namespace NSEPlaytest
{
    constexpr int32 FireInput = 0x2000;
    NIGHTSKYENGINE_API void Prepare(ANightSkyGameState* Game);
    NIGHTSKYENGINE_API void Update(ANightSkyGameState* Game, int32 Input);
    // Presentation uses the rollback-restored shot frame, and only player one's body.
    NIGHTSKYENGINE_API bool CastPoseTime(const APlayerObject* Player, float& Time);
    NIGHTSKYENGINE_API FVector FistPosition(APlayerObject* Player);
}

UCLASS()
class NIGHTSKYENGINE_API USweptPlaytestShot : public UState
{
    GENERATED_BODY()
public:
    void Configure();
    virtual void Exec_Implementation() override;
    UFUNCTION() void Contact();
    UPROPERTY() TWeakObjectPtr<ASweptShotEffect> Presentation;
};

// Presentation only: all collision and damage belong to the battle objects.
UCLASS()
class NIGHTSKYENGINE_API ASweptShotEffect : public AActor
{
    GENERATED_BODY()
public:
    ASweptShotEffect();
    virtual void Tick(float DeltaSeconds) override;
    static ASweptShotEffect* Launch(ANightSkyGameState* Game, APlayerObject* Caster, ABattleObject* Shot);
    void SetImpact(FVector Position);
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Halo;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Muzzle;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Filaments;
    UPROPERTY() TObjectPtr<UPointLightComponent> Light;
    UPROPERTY() TObjectPtr<UMaterialInterface> ShotMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> HaloMaterial;
private:
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CoreMID;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HaloMID;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ArcMID;
    UPROPERTY() TWeakObjectPtr<APlayerObject> Caster;
    UPROPERTY() TWeakObjectPtr<ABattleObject> Projectile;
    FVector Tip = FVector::ZeroVector;
    bool bContact = false;
    int32 ShotNumber = 0;
    float Age = 0;
};

UCLASS()
class NIGHTSKYENGINE_API ASweptPlaytestHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
