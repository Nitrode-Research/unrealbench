#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LinkGameMode.generated.h"

UCLASS()
class FOUNDATIONS_UE_API ALinkGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ALinkGameMode();
    virtual void InitGame(const FString& MapName,const FString& Options,FString& ErrorMessage) override;
    virtual void StartPlay() override;
};
