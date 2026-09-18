#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"

class ALinkPlayerController;
class SVerticalBox;
class SLinkHUD : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SLinkHUD){} SLATE_ARGUMENT(ALinkPlayerController*,Owner) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual void Tick(const FGeometry& Geometry,double Time,float DeltaTime) override;
private:
    void RebuildActions();
    TWeakObjectPtr<ALinkPlayerController> Owner;
    TSharedPtr<SVerticalBox> Actions;
    int32 LastRevision=-1;
    FSlateRoundedBoxBrush Card{FLinearColor(0.016f,0.028f,0.042f,0.96f),10.f};
};
