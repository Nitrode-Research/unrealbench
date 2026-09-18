#include "RelayOverlay.h"

int32 URelayOverlay::NativePaint(const FPaintArgs &Args, const FGeometry &Geometry,
                                 const FSlateRect &CullingRect,
                                 FSlateWindowElementList &DrawElements, int32 LayerId,
                                 const FWidgetStyle &WidgetStyle, bool bParentEnabled) const
{
    // The starter declares the observable widget; relay presentation is the task.
    return Super::NativePaint(Args, Geometry, CullingRect, DrawElements, LayerId, WidgetStyle,
                              bParentEnabled);
}
