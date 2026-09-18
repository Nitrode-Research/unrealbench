// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParticleManager.generated.h"

class UNiagaraComponent;
class ABattleObject;
class UNiagaraSimCache;
class USceneComponent;
class ANightSkyGameState;

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

// Component properties and simulation buffers have different lifetimes. Keep both
// under the playback world's manager after the original component has expired.
USTRUCT()
struct FReplayStartupParticle
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Configuration;
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSimCache> Simulation;
	UPROPERTY(Transient)
	TObjectPtr<UObject> ComponentOuter;
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> AttachParent;
	UPROPERTY(Transient)
	TObjectPtr<ABattleObject> ParticleOwner;
	UPROPERTY(Transient)
	TArray<TObjectPtr<ABattleObject>> LinkedObjects;

	FTransform WorldTransform;
	FTransform RelativeTransform;
	FTransform ParentTransform;
	FName Socket;
	float DesiredAge = 0;
	uint8 AgeMode = 0;
	uint8 RequestedState = 0;
	uint8 ActualState = 0;
	bool Active = false;
	bool Complete = false;
	bool Visible = false;
};

UCLASS()
class NIGHTSKYENGINE_API AParticleManager : public AActor
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FBattleParticle> BattleParticles;

	// Sets default values for this actor's properties
	AParticleManager();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	void UpdateParticles();
	void PauseParticles();
	void RollbackParticles(int RollbackFrames);

	bool CaptureReplayStart(ANightSkyGameState* Battle);
	bool PrepareReplayStart();
	void ClearReplayParticles(ANightSkyGameState* Battle);
	void InstallReplayStart();

private:
	bool ReplayManaged = false;
	UPROPERTY(Transient)
	TArray<FReplayStartupParticle> ReplayStartupParticles;
	UPROPERTY(Transient)
	TArray<FBattleParticle> PreparedReplayParticles;

};
