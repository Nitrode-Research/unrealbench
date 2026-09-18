#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "SpectatorRoom.h"
#include "RoomPanel.generated.h"
class UEditableTextBox;
class UTextBlock;
class URoomPanel;
UCLASS()

class NIGHTSKYENGINE_API URoomActionButton : public UButton
{
    GENERATED_BODY()
  public:
    UPROPERTY()
    TObjectPtr<URoomPanel> Panel;
    FString Operation;
    UFUNCTION()
    void Activate();
};
/** Native room controls, usable without a authored widget Blueprint. */
UCLASS()

class NIGHTSKYENGINE_API URoomPanel : public UUserWidget
{
    GENERATED_BODY()
  public:
    UPROPERTY(BlueprintReadWrite)
    TObjectPtr<URoomConnection> Connection;
    UFUNCTION(BlueprintCallable)
    void Run(const FString &Operation);
    UFUNCTION()
    void UpdateDelivery(const FRoomDelivery &Delivery);

  protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
};
