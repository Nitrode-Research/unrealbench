#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LinkAudio.generated.h"

UCLASS()
class FOUNDATIONS_UE_API ULinkAudio : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void OnWorldBeginPlay(UWorld& World) override;
    virtual void Deinitialize() override;
    void Command();
    void Pickup();
    void Footstep(int32 Identity,int32 Count);
    void ToggleMute();
    bool IsMuted() const{return bMuted;}
    bool HasRequiredAssets() const;
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override{return Type==EWorldType::Game||Type==EWorldType::PIE;}
private:
    void Cue(class USoundBase* Sound,float Volume,float Pitch=1.f);
    UPROPERTY() TObjectPtr<class USoundBase> StepSound;
    UPROPERTY() TObjectPtr<class USoundBase> ChainSound;
    UPROPERTY() TObjectPtr<class USoundBase> CommandSound;
    UPROPERTY() TObjectPtr<class USoundBase> PickupSound;
    UPROPERTY() TObjectPtr<class USoundBase> BedSound;
    UPROPERTY() TObjectPtr<class UAudioComponent> Bed;
    float MasterVolume=0.7f,AmbienceVolume=0.16f,FootstepVolume=0.35f;
    bool bMuted=false;
};
