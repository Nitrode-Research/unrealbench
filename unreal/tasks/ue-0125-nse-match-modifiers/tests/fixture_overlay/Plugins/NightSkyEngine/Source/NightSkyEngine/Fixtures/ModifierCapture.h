#pragma once
#include "Blueprint/UserWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Layout/Children.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Text/STextBlock.h"

// Offscreen presentation fixture. It does not refresh or evaluate rule data.
namespace ModifierCapture
{
inline void FullPaint(const TSharedRef<SWidget>& Widget)
{
	Widget->ForceVolatile(true);
	Widget->Invalidate(EInvalidateWidgetReason::Layout);
	if (FChildren* Children = Widget->GetAllChildren())
		for (int32 I = 0; I < Children->Num(); ++I)
			FullPaint(Children->GetChildAt(I));
}
} // namespace ModifierCapture
