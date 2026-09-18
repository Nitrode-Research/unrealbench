#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "ModifierArenaProbe.generated.h"
/** Opt-in end-to-end fixture driver. Exercises the public setup and controller APIs. */
UCLASS()
class NIGHTSKYENGINE_API UModifierArenaProbe : public UGameInstanceSubsystem,
                                               public FTickableGameObject
{
	GENERATED_BODY()
  public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override
	{
		return !Directory.IsEmpty() && !Finished && !IsTemplate();
	}
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UModifierArenaProbe, STATGROUP_Tickables);
	}

  private:
	FString Directory;
	TArray<FString> Errors;
	int32 Assertions = 0;
	bool Started = false, Finished = false, SawContact = false, CheckedEarly = false;
	double Deadline = 0;
	int32 InitialX = 0, InitialY = 0;
	void Check(bool Value, const FString& Message);
	void Finish();
};
