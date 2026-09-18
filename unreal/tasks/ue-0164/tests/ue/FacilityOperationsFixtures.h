#pragma once

#include "CoreMinimal.h"
#include "Facility/FacilityStateSubsystem.h"
#include "FacilityDevices/Door/FacilityDoor.h"
#include "FacilityDevices/Pickup/FacilityPickup.h"
#include "FacilityDevices/Lift/FacilityLift.h"
#include "FacilityDevices/Lift/FacilityLiftCallPanel.h"
#include "FacilityDevices/Generator/FacilityGenerator.h"
#include "FacilityDevices/PanelSwitch.h"
#include "FacilityDevices/RoutingPanel/FacilityRoutingSwitch.h"
#include "FacilityDevices/Fan/FacilityFan.h"
#include "FacilityDevices/Flood/FacilityFlood.h"
#include "GameFramework/Pawn.h"
#include "FacilityOperationsFixtures.generated.h"

// Evaluator-only observers/configuration adapters. No target behavior is implemented here.
UCLASS()
class UGE152StateObserver : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<UFacilityStateSubsystem> Facility;
    TArray<FString> Events;
    bool bResetObserved = false;
    bool bLastStateWasReset = false;
    bool bLastAlarmWasReset = false;
    bool bResetOnItem = false;
    EAlarmLevel LastOld = EAlarmLevel::EAL_None;
    EAlarmLevel LastNew = EAlarmLevel::EAL_None;
    UFUNCTION() void StateChanged();
    UFUNCTION() void AlarmChanged(EAlarmLevel Old, EAlarmLevel New);
};

UCLASS()
class AGE152Door : public AFacilityDoor
{
    GENERATED_BODY()
public:
    void Configure(FName Id, EFacilityCircuit Circuit, EDoorKind DoorKind, bool Open = false);
    void FrameStep(float Dt) { Tick(Dt); }
    UStaticMeshComponent* Leaf() const { return Panel; }
    TArray<FString> Events;
    bool bSettledWhileMoving = false;
    virtual void ProcessEvent(UFunction* Function, void* Params) override;
protected:
    virtual void OnPanelSettled(bool Open) override;
};

UCLASS()
class AGE152Pickup : public AFacilityPickup
{
    GENERATED_BODY()
public:
    void Configure(FName Id) { PickupId = Id; }
    TArray<FString> Events;
    bool bVisibilityCorrectInHook = true;
    bool bNotifyUnrelatedOnTaken = false;
    virtual void ProcessEvent(UFunction* Function, void* Params) override;
protected:
    virtual void OnTakenChanged(bool Taken, bool Instant) override;
};

UCLASS()
class AGE152Lift : public AFacilityLift
{
    GENERATED_BODY()
public:
    void Configure(FName Id, EFacilityCircuit Circuit, ELiftKind LiftKind, ELiftStop Initial = ELiftStop::ELS_Bottom);
    void FrameStep(float Dt) { Tick(Dt); }
    TArray<FString> Events;
    bool bArrivedWhileMoving = false;
    virtual void ProcessEvent(UFunction* Function, void* Params) override;
protected:
    virtual void OnCarArrived(ELiftStop Stop) override;
};

UCLASS()
class AGE152CallPanel : public AFacilityLiftCallPanel
{
    GENERATED_BODY()
public:
    void Configure(FName Id, ELiftStop InStop) { LiftId = Id; Stop = InStop; }
};

UCLASS()
class AGE152Generator : public AFacilityGenerator
{
    GENERATED_BODY()
public:
    TArray<FString> Events;
    virtual void ProcessEvent(UFunction* Function, void* Params) override;
protected:
    virtual void OnRunningChanged(bool Running, bool Instant) override;
    virtual void OnOverloadedChanged(bool Overloaded, bool Instant) override;
};

UCLASS()
class AGE152Breaker : public APanelSwitch
{
    GENERATED_BODY()
public:
    void Configure(EFacilityCircuit InCircuit) { Circuit = InCircuit; }
    UInteractableComponent* Interaction() const { return Interactable; }
    TArray<FString> Events;
    virtual void ProcessEvent(UFunction* Function, void* Params) override;
};

UCLASS()
class AGE152Routing : public AFacilityRoutingSwitch
{
    GENERATED_BODY()
public:
    void Configure(FName Panel, FName Target, EFacilityCircuit ToCircuit, bool Pump = false);
    TArray<FString> Events;
    virtual void ProcessEvent(UFunction* Function, void* Params) override;
};

UCLASS()
class AGE152Fan : public AFacilityFan
{
    GENERATED_BODY()
public:
    void Configure(FName Id, EFacilityCircuit Circuit, FName Lights = NAME_None);
    void FrameStep(float Dt) { Tick(Dt); }
    UBoxComponent* Volume() const { return KillVolume; }
    UPointLightComponent* Lamp() const { return Light; }
    USceneComponent* RotationHub() const { return Hub; }
    TArray<FString> Events;
    virtual void ProcessEvent(UFunction* Function, void* Params) override;
protected:
    virtual void OnRunningChanged(bool Running, bool Instant) override;
};

UCLASS()
class AGE152Flood : public AFacilityFlood
{
    GENERATED_BODY()
public:
    void Configure(FName InFans);
    void FrameStep(float Dt) { Tick(Dt); }
    UBoxComponent* Volume() const { return KillVolume; }
    UStaticMeshComponent* WaterMesh() const { return Water; }
    TArray<FString> Events;
    bool bSettledWhileMoving = false;
    virtual void ProcessEvent(UFunction* Function, void* Params) override;
};

UCLASS()
class AGE152Victim : public APawn
{
    GENERATED_BODY()
public:
    AGE152Victim();
    UPROPERTY() TObjectPtr<class USphereComponent> Shape;
    int32 Hits = 0;
    float TotalDamage = 0;
    AActor* LastCauser = nullptr;
    AController* LastInstigator = nullptr;
    bool bResetOnHit = false;
    virtual float TakeDamage(float Damage, const FDamageEvent& Event, AController* DamageInstigator, AActor* Causer) override;
};
