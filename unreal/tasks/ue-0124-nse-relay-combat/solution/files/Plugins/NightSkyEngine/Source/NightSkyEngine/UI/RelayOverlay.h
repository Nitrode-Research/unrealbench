#pragma once
#include "Blueprint/UserWidget.h"
#include "RelayOverlay.generated.h"
class ANightSkyGameState;
class UTextBlock;
class UImage;
UCLASS()
class NIGHTSKYENGINE_API URelayOverlay : public UUserWidget
{
    GENERATED_BODY()
  public:
    UPROPERTY()
    ANightSkyGameState *Battle;
    virtual TSharedRef<SWidget> RebuildWidget() override;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> TextRows;
    UPROPERTY() TArray<TObjectPtr<UImage>> BoxRows;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    void RefreshHUD(const FGeometry& Geometry);
    // Public observation of text actually submitted by the latest paint callback.
    mutable TArray<FString> PaintedText;
    mutable int32 PaintedFrame = -1;
};
