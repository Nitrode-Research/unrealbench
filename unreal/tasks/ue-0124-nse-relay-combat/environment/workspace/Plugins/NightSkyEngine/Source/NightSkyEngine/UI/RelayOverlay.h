#pragma once
#include "Blueprint/UserWidget.h"
#include "RelayOverlay.generated.h"
class ANightSkyGameState;
UCLASS()
class NIGHTSKYENGINE_API URelayOverlay : public UUserWidget
{
    GENERATED_BODY()
  public:
    UPROPERTY()
    ANightSkyGameState *Battle;
    // Public observation of text actually submitted by the latest paint callback.
    mutable TArray<FString> PaintedText;
    mutable int32 PaintedFrame = -1;
    virtual int32 NativePaint(const FPaintArgs &Args, const FGeometry &Geometry,
                              const FSlateRect &Cull, FSlateWindowElementList &Elements,
                              int32 Layer, const FWidgetStyle &Style, bool Enabled) const override;
};
