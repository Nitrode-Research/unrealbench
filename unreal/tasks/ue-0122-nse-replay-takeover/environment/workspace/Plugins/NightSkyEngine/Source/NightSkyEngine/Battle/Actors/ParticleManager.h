// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "ParticleManager.generated.h"

class UNiagaraComponent;
class ABattleObject;

// Optional native renderer for common one-shot effects. The presentation call
// supplies the resolved world transform; returning false retains the asset path.
// This interface owns no replay policy. Catch-up suppression belongs to callers.
struct FCommonParticleRequest
{
    // Borrowed only for the synchronous Spawn call; retain a weak reference if needed.
    ABattleObject* Source = nullptr;
    FGameplayTag Name;
    FTransform Transform;
};

class NIGHTSKYENGINE_API ICommonParticleRenderer
{
public:
    virtual ~ICommonParticleRenderer() = default;
    virtual bool Spawn(const FCommonParticleRequest& Request) = 0;
    virtual void Advance(float DeltaSeconds) = 0;
    virtual void Clear() = 0;
};

USTRUCT()
struct FBattleParticle
{
	GENERATED_BODY()
	
	FBattleParticle()
	{
		NiagaraComponent = nullptr;
		ParticleOwner = nullptr;
	}
	
	FBattleParticle(UNiagaraComponent* InNiagaraComponent, ABattleObject* InOwner)
		: NiagaraComponent(InNiagaraComponent), ParticleOwner(InOwner) {}
	
	UPROPERTY()
	UNiagaraComponent* NiagaraComponent;
	UPROPERTY()
	ABattleObject* ParticleOwner;
};

UCLASS()
class NIGHTSKYENGINE_API AParticleManager : public AActor
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FBattleParticle> BattleParticles;

	TSharedPtr<ICommonParticleRenderer> CommonParticleRenderer;

	// Sets default values for this actor's properties
	AParticleManager();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	void UpdateParticles();
	void ClearCommonParticles();
	void PauseParticles();
	void RollbackParticles(int RollbackFrames);
};
