#pragma once

#include "CoreMinimal.h"
#include "ScifiSimEscapeCharacter.h"
#include "ScifiSimEscapeGameMode.h"
#include "FacilityOperationsFixtures.h"
#include "InfiltrationFixtures.h"
#include "FacilityDevices/Exit/FacilityExit.h"
#include "FacilityDevices/Light/FacilityLight.h"
#include "FacilityDevices/CoolantValve/FacilityCoolantValve.h"
#include "FacilityDevices/ServerRack/FacilityServerRack.h"
#include "CompleteGameFixtures.generated.h"

// Configuration, event observers and Blueprint-equivalent verb wiring only.
// No fixture supplies rules, player movement, inventory, registration or victory.
UCLASS()
class ASSEGamePlayer : public AScifiSimEscapeCharacter
{
    GENERATED_BODY()
public:
    void PressInteract() { DoInteract(); }
    void PressHack() { DoHack(); }
    void MoveAxes(float Right, float Forward) { DoMove(Right, Forward); }
    void JumpPressed() { DoJumpStart(); }
};

UCLASS()
class ASSEGameModeObserver : public AScifiSimEscapeGameMode
{
    GENERATED_BODY()
public:
    int32 Wins = 0;
    EFacilityExit LastExit = EFacilityExit::None;
protected:
    virtual void OnEscaped(EFacilityExit Exit) override { ++Wins; LastExit = Exit; }
};

UCLASS()
class ASSEGameExit : public AFacilityExit
{
    GENERATED_BODY()
public:
    void Configure(EFacilityExit Kind, FName Door = NAME_None, FName Fans = NAME_None)
    { Exit = Kind; DoorId = Door; FansId = Fans; }
    UBoxComponent* Box() const { return Volume; }
};

UCLASS()
class ASSEGameLamp : public AFacilityLight
{
    GENERATED_BODY()
public:
    void Configure(FName Id, EFacilityCircuit Circuit) { DeviceId = Id; DefaultCircuit = Circuit; }
    int32 Ordinary = 0, Instant = 0;
protected:
    virtual void OnLitChanged(bool Lit, bool IsInstant) override { IsInstant ? ++Instant : ++Ordinary; }
};

UCLASS()
class ASSEGameRack : public AFacilityServerRack
{
    GENERATED_BODY()
public:
    void Configure(FName Id) { DeviceId = Id; }
    int32 Ordinary = 0, Instant = 0;
protected:
    virtual void OnOverloadedChanged(bool Overloaded, bool IsInstant) override { IsInstant ? ++Instant : ++Ordinary; }
};

UCLASS()
class ASSEGameValve : public AFacilityCoolantValve
{
    GENERATED_BODY()
public:
    int32 Ordinary = 0, Instant = 0;
protected:
    virtual void OnOpenChanged(bool Open, bool IsInstant) override { IsInstant ? ++Instant : ++Ordinary; }
};

UCLASS()
class USSEGameVerbBinding : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<AFacilityPickup> Pickup;
    UPROPERTY() TObjectPtr<AFacilityLadder> Ladder;
    UPROPERTY() TObjectPtr<AActor> LastUser;
    UFUNCTION() void Take(AActor* User) { LastUser = User; if (Pickup) Pickup->Take(); }
    UFUNCTION() void Climb(AActor* User) { LastUser = User; if (Ladder) Ladder->Use(User); }
};
