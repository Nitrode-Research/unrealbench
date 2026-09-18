#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Climbing/ClimbComponent.h"
#include "Stealth/StealthComponent.h"
#include "GameFramework/Character.h"
#include "Facility/FacilityStateSubsystem.h"
#include "FacilityDevices/Ladder/FacilityLadder.h"
#include "FacilityDevices/Hatch/FacilityHatch.h"
#include "FacilityDevices/LightZone/FacilityLightZone.h"
#include "FacilityDevices/HidingSpot/FacilityHidingSpot.h"
#include "FacilityDevices/Camera/FacilityCamera.h"
#include "FacilityDevices/GateController/FacilityGateController.h"
#include "FacilityDevices/AlarmConsole/FacilityAlarmConsole.h"
#include "FacilityDevices/Crate/FacilityCrate.h"
#include "InteractionSystem/InteractableComponent.h"
#include "InteractionSystem/InteractorComponent.h"
#include "InfiltrationFixtures.generated.h"

// Evaluator-only fixtures expose protected configuration/tick hooks, never private state.
UCLASS()
class USSE153Climb : public UClimbComponent
{
    GENERATED_BODY()
public:
    void Advance(float Dt) { TickComponent(Dt, LEVELTICK_All, nullptr); }
};

UCLASS()
class USSE153Stealth : public UStealthComponent
{
    GENERATED_BODY()
public:
    void Advance() { TickComponent(0.01f, LEVELTICK_All, nullptr); }
};

UCLASS()
class ASSE153Character : public ACharacter
{
    GENERATED_BODY()
public:
    ASSE153Character()
    {
        Climb = CreateDefaultSubobject<USSE153Climb>(TEXT("Climb"));
        Stealth = CreateDefaultSubobject<USSE153Stealth>(TEXT("Stealth"));
        GetCapsuleComponent()->SetGenerateOverlapEvents(true);
    }
    UPROPERTY() TObjectPtr<USSE153Climb> Climb;
    UPROPERTY() TObjectPtr<USSE153Stealth> Stealth;
    int32 DamageCalls = 0;
    float LastDamage = 0.f;
    AActor* LastCauser = nullptr;
    AController* LastInstigator = nullptr;
    virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* DamageInstigator, AActor* Causer) override
    {
        ++DamageCalls; LastDamage = Amount; LastCauser = Causer; LastInstigator = DamageInstigator;
        return Amount;
    }
};

UCLASS()
class USSE153Interactor : public UInteractorComponent
{
    GENERATED_BODY()
public:
    void Configure(UMaterialInterface* Material) { HighlightMaterial = Material; SphereRadius = 450.f; }
};

UCLASS()
class ASSE153Viewer : public AActor
{
    GENERATED_BODY()
public:
    ASSE153Viewer()
    {
        SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
        Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
        Camera->SetupAttachment(GetRootComponent());
        Interactor = CreateDefaultSubobject<USSE153Interactor>(TEXT("Interactor"));
    }
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<USSE153Interactor> Interactor;
};

UCLASS()
class ASSE153Hatch : public AFacilityHatch
{
    GENERATED_BODY()
public:
    void Configure(FName Id) { DoorId = Id; }
    void Advance(float Dt) { Tick(Dt); }
    FQuat Pose() const { return Hinge->GetRelativeRotation().Quaternion(); }
    int32 Settles = 0;
protected:
    virtual void OnLidSettled(bool Open) override { ++Settles; }
};

UCLASS()
class ASSE153LightZone : public AFacilityLightZone
{
    GENERATED_BODY()
public:
    void Configure(FName Id) { LightsId = Id; }
    UBoxComponent* Box() const { return Volume; }
};

UCLASS()
class ASSE153HidingSpot : public AFacilityHidingSpot
{
    GENERATED_BODY()
public:
    void RequireCrouch(bool Value) { bRequiresCrouch = Value; }
    UBoxComponent* Box() const { return Volume; }
};

UCLASS()
class ASSE153Camera : public AFacilityCamera
{
    GENERATED_BODY()
public:
    void Configure(FName Id, FName Light) { DeviceId = Id; LightsId = Light; }
    void Pan(float Start, float End, float Speed) { StartYaw = Start; MaxYaw = End; TurnSpeed = Speed; }
    void Advance(float Dt) { if (IsActorTickEnabled()) Tick(Dt); }
    UBoxComponent* Box() const { return View; }
    UStaticMeshComponent* Mesh() const { return Housing; }
    int32 WatchingEdges = 0;
    int32 InstantEdges = 0;
    EAlarmLevel AlarmAtWatch = EAlarmLevel::EAL_None;
    bool Reenter = false;
protected:
    virtual void OnWatchingChanged(bool Watching, bool Instant) override
    {
        ++WatchingEdges;
        if (Instant) ++InstantEdges;
        if (UFacilityStateSubsystem* F = FindFacility())
        {
            AlarmAtWatch = F->GetAlarmLevel();
            if (Reenter && !Instant && Watching) F->GiveItem(EFacilityItem::ServiceKey);
        }
    }
};

UCLASS()
class ASSE153GateController : public AFacilityGateController
{
    GENERATED_BODY()
public:
    void Configure(FName Id) { DeviceId = Id; }
};

UCLASS()
class ASSE153Console : public AFacilityAlarmConsole
{
    GENERATED_BODY()
public:
    void Configure(FName Id) { DeviceId = Id; }
};

UCLASS()
class ASSE153Crate : public AFacilityCrate
{
    GENERATED_BODY()
public:
    void Configure(FName Id, FName Lift) { CrateId = Id; LiftId = Lift; }
    void Advance(float Dt) { Tick(Dt); }
    UStaticMeshComponent* Mesh() const { return Body; }
    UBoxComponent* Box() const { return KillVolume; }
    USceneComponent* Head() const { return ShaftHead; }
    USceneComponent* Bottom() const { return Landing; }
    int32 PositionEvents = 0;
    int32 InstantEvents = 0;
    bool Reenter = false;
protected:
    virtual void OnPositionChanged(ECratePosition Position, bool Dropped, bool Instant) override
    {
        ++PositionEvents; if (Instant) ++InstantEvents;
        if (Reenter && Dropped && !Instant)
            if (UFacilityStateSubsystem* F = FindFacility()) F->GiveItem(EFacilityItem::ServiceKey);
    }
};

UCLASS()
class USSE153Probe : public UObject
{
    GENERATED_BODY()
public:
    int32 Displays = 0, Detected = 0, Lost = 0, Climbs = 0, StealthChanges = 0;
    UPROPERTY() TObjectPtr<AFacilityLadder> Ladder;
    UPROPERTY() TObjectPtr<USSE153Stealth> Stealth;
    UFUNCTION() void Display() { ++Displays; }
    UFUNCTION() void Detect() { ++Detected; }
    UFUNCTION() void Lose() { ++Lost; }
    UFUNCTION() void Climbing(bool Active) { ++Climbs; }
    UFUNCTION() void UseLadder(AActor* User) { if (Ladder) Ladder->Use(User); }
    UFUNCTION() void StealthChanged(EStealthState Old, EStealthState New)
    { ++StealthChanges; if (Stealth) Stealth->Advance(); }
};
