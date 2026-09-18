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
    FString MatchIdentity;
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
    void SelectRetainedMatch(const FString &Identity);

  protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

  private:
    class UEditableTextBox *AddTextField(class UVerticalBox *Column, const TCHAR *Name, const TCHAR *Hint);
    void AddActionButton(class UVerticalBox *Column, const TCHAR *Operation);
    void CreateRoomFromControls();
    void HostRoomFromControls();
    void ConnectFromControls();
    void RunReplayControl(const FString &Operation);
    void AuthenticateFromControls();
    UPROPERTY()
    TObjectPtr<UEditableTextBox> Address;
    UPROPERTY()
    TObjectPtr<UEditableTextBox> Identity;
    UPROPERTY()
    TObjectPtr<UEditableTextBox> Secret;
    UPROPERTY()
    TObjectPtr<UEditableTextBox> AuthoritySlot;
    UPROPERTY()
    TObjectPtr<UEditableTextBox> Delay;
    UPROPERTY()
    TObjectPtr<UEditableTextBox> Value;
    UPROPERTY()
    TObjectPtr<UEditableTextBox> Frame;
    UPROPERTY()
    TObjectPtr<UEditableTextBox> Match;
    UPROPERTY()
    TObjectPtr<class UVerticalBox> RetainedMatchList;
    TArray<FString> ShownMatches;
    UPROPERTY()
    TObjectPtr<UTextBlock> Status;
    UPROPERTY()
    TObjectPtr<class UImage> Playback;
};
