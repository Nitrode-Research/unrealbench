#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "LinkSaveGame.generated.h"

/** Platform save envelope. The portable, versioned payload schema is defined in Rules/LinkSave. */
UCLASS()
class FOUNDATIONS_UE_API ULinkSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(SaveGame) FString Payload;
};
