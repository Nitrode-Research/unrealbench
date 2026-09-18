#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "ModifierPeerWorker.h"
#include "ModifierScenarioWorker.generated.h"

UCLASS()
class NIGHTSKYENGINE_API UModifierWorkerGameInstance : public UModifierFixtureGameInstance
{
	GENERATED_BODY()
  public:
	virtual void Init() override;
};

/** Fixed controller tape and public state diagnostics, without modifier policy. */
UCLASS()
class NIGHTSKYENGINE_API UModifierPeerWorker : public UGameInstanceSubsystem,
                                               public FTickableGameObject
{
	GENERATED_BODY()
  public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override
	{
		return Enabled && !IsTemplate();
	}
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UModifierPeerWorker, STATGROUP_Tickables);
	}
	void Observe(ANightSkyGameState* Battle, int32 A, int32 B, bool Resimulation);

  private:
	FString Directory, Role;
	bool Enabled = false, Bound = false, Sent = false, Started = false, Saved = false,
	     RequestedRematch = false;
	double Heartbeat = 0;
};
