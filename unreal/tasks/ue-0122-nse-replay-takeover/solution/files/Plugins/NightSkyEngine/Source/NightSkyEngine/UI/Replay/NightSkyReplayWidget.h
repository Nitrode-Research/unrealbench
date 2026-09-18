#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "NightSkyReplayWidget.generated.h"

class UTextBlock;
class UProgressBar;
class UVerticalBox;
class UNightSkyGameInstance;

enum class EReplayUICommand : uint8
{
    ToggleHelp, WatchRecording, Pause, BackFrame, ForwardFrame, BackSecond, ForwardSecond,
    Start, End, TakeP1, TakeP2, Finish, PreviousSlot, NextSlot, LoadSlot
};

struct FReplayUIBinding
{
    FKey Key;
    EReplayUICommand Command;
    const TCHAR* Hint;
};

/** Presentation and local shortcuts only; replay simulation remains in the game instance. */
UCLASS()
class NIGHTSKYENGINE_API UNightSkyReplayWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    static const TArray<FReplayUIBinding>& GetBindings();
    void ExecuteCommand(EReplayUICommand Command);
    // Also used by the UI automation tests without creating a Slate viewport.
    bool ApplyCommand(UNightSkyGameInstance* Game, EReplayUICommand Command);
    FString GetFeedback() const { return Feedback; }
    static FString DescribeState(const UNightSkyGameInstance* Game, bool bBattlePaused);

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

private:
    void Refresh();
    void RefreshSlots(UNightSkyGameInstance* Game);
    UTextBlock* AddText(UVerticalBox* Box, const FString& Text, int32 Size, FLinearColor Color);
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PositionText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ContextText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> LibraryText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> FeedbackText;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> Timeline;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Details;
    TArray<FString> Slots;
    int32 SelectedSlot = 0;
    bool bShowHelp = true;
    float RefreshTime = 0;
    FString Feedback;
};
