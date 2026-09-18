#pragma once
#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class SLinkWorldHints : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SLinkWorldHints){} SLATE_ARGUMENT(class ALinkPlayerController*,Owner) SLATE_END_ARGS()
    void Construct(const FArguments& Args){Owner=Args._Owner;SetVisibility(EVisibility::HitTestInvisible);}
    virtual FVector2D ComputeDesiredSize(float Scale) const override{return FVector2D::ZeroVector;}
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Cull,FSlateWindowElementList& Draw,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override;
private:
    TWeakObjectPtr<class ALinkPlayerController> Owner;
};
