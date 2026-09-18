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
    if(!Owner.IsValid()){return Layer;}
    int32 Width=0,Height=0;Owner->GetViewportSize(Width,Height);if(Width<=0||Height<=0){return Layer;}
    const FVector2D Scale=Geometry.GetLocalSize()/FVector2D(Width,Height);
    auto Project=[&](const FVector& World,FVector2D& Point)
    {
        if(!Owner->ProjectWorldLocationToScreen(World,Point,true)){return false;}
        Point*=Scale;return true;
    };
    auto Line=[&](const TArray<FVector2D>& Points,const FLinearColor& Color,float Thickness)
    {
        FSlateDrawElement::MakeLines(Draw,Layer,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Thickness);
    };
    auto Ring=[&](const FVector& Center,float Radius,const FLinearColor& Color,float Thickness)
    {
        TArray<FVector2D> Points;
        for(int32 Index=0;Index<=40;++Index)
        {
            const float Angle=2.f*PI*Index/40.f;FVector2D Point;
            if(!Project(Center+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0),Point)){return;}
            Points.Add(Point);
        }
        Line(Points,Color,Thickness);
    };
    const double Now=Owner->GetWorld()->GetTimeSeconds();
    for(int32 Index=0;Index<2;++Index)
    {
        const auto* Crew=Owner->GetCharacterAt(Index);if(!Crew){continue;}
        const FLinearColor Color=Index==0?FLinearColor(0.16f,0.82f,0.92f,0.75f):FLinearColor(1.f,0.58f,0.2f,0.75f);
        Ring(Crew->GetNavAgentLocation()+FVector(0,0,3),44,Color,1.5f);
        FVector2D Label;
        if(Project(Crew->GetActorLocation()+FVector(0,0,120),Label))
        {
            FSlateDrawElement::MakeText(Draw,Layer,Geometry.ToPaintGeometry(FVector2D(30,20),FSlateLayoutTransform(Label-FVector2D(10,0))),
                Index==0?TEXT("LT"):TEXT("RT"),FCoreStyle::GetDefaultFontStyle("Bold",11),ESlateDrawEffect::None,Color);
        }
        const double Age=Now-Owner->GetCommandFeedbackTime(Index);
        if(Age>=0&&Age<1.1)
        {
            const float Fraction=float(Age/1.1);FLinearColor Faded=Color;Faded.A=1-Fraction;
            Ring(Owner->GetCommandFeedbackLocation(Index)+FVector(0,0,5),25+45*Fraction,Faded,2.f);
        }
    }
    if(!Owner->GetActiveInteraction()&&!Owner->IsPaused())
    {
        for(TActorIterator<ALinkInteractionActor> It(Owner->GetWorld());It;++It)
        {
            FVector2D Point;if(!Project(It->GetActorLocation()+FVector(0,0,165),Point)){continue;}
            const FVector2D Size=Geometry.GetLocalSize();if(Point.X<20||Point.Y<85||Point.X>Size.X-20||Point.Y>Size.Y-120){continue;}
            const bool Hovered=*It==Owner->GetHoveredInteraction();
            const float Radius=Hovered?10.f:6.f;
            Line({Point+FVector2D(0,-Radius),Point+FVector2D(Radius,0),Point+FVector2D(0,Radius),Point+FVector2D(-Radius,0),Point+FVector2D(0,-Radius)},
                FLinearColor(0.85f,0.92f,0.93f,Hovered?1.f:0.55f),Hovered?2.f:1.3f);
            if(Hovered){Ring(It->GetActorLocation()+FVector(0,0,4),100,FLinearColor(0.8f,0.9f,0.93f,0.75f),1.5f);}
        }
    }
    return Layer+1;
}
