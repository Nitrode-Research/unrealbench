#include "UI/LinkWorldHints.h"
#include "Gameplay/LinkPlayerController.h"
#include "Gameplay/LinkCharacter.h"
#include "Gameplay/LinkInteractionActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

int32 SLinkWorldHints::OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Cull,FSlateWindowElementList& Draw,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    // Restore the documented B gameplay contract.
    return {};
}
