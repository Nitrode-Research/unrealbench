#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor.h"
#include "Engine/World.h"
#include "Gameplay/LinkPlayerController.h"
#include "UI/LinkHUD.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Tests/AutomationEditorCommon.h"

class FTripletRenderedHUD : public IAutomationLatentCommand
{
public:
    explicit FTripletRenderedHUD(FAutomationTestBase* InTest):Test(InTest){}
    ~FTripletRenderedHUD(){if(Window.IsValid()&&FSlateApplication::IsInitialized()){FSlateApplication::Get().RequestDestroyWindow(Window.ToSharedRef());}}
    bool Update() override
    {
        const double Now=FPlatformTime::Seconds();if(Start<0){Start=Now;}
        if(Now-Start>30){Test->AddError(TEXT("Rendered HUD startup timed out"));return true;}
        if(FParse::Param(FCommandLine::Get(),TEXT("NullRHI"))||!FSlateApplication::IsInitialized()){Test->AddError(TEXT("INVALID_VERIFICATION: Rendered HUD requires a real initialized renderer"));return true;}
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        auto* PC=World?Cast<ALinkPlayerController>(World->GetFirstPlayerController()):nullptr;
        if(!PC||!PC->GetCharacterAt(0)||!PC->GetCharacterAt(1)){return false;}
        if(!Window.IsValid())
        {
            SAssignNew(HUD,SLinkHUD).Owner(PC);
            SAssignNew(Window,SWindow).Title(FText::FromString(TEXT("Foundations HUD evaluation"))).ClientSize(FVector2D(1280,720)).SupportsMaximize(false).SupportsMinimize(false)[HUD.ToSharedRef()];
            FSlateApplication::Get().AddWindow(Window.ToSharedRef(),true);PhaseStart=Now;return false;
        }
        if(Now-PhaseStart<1){return false;}
        TArray<FColor> Pixels;FIntVector Size;
        if(!FSlateApplication::Get().TakeScreenshot(HUD.ToSharedRef(),Pixels,Size)||Pixels.Num()!=Size.X*Size.Y||Size.X<640||Size.Y<360)
        {Test->AddError(TEXT("INVALID_VERIFICATION: No complete rendered HUD readback"));return true;}
        int32 Visible=0;for(const auto& Pixel:Pixels){if(FMath::Max3(Pixel.R,Pixel.G,Pixel.B)>35){++Visible;}}
        Test->TestTrue(TEXT("Actual Slate HUD renders visible content"),Visible>500);
        if(Phase==0){Before=MoveTemp(Pixels);PC->TogglePauseOrClose();Phase=1;PhaseStart=Now;return false;}
        if(Before.Num()!=Pixels.Num()){Test->AddError(TEXT("INVALID_VERIFICATION: HUD surface dimensions changed"));return true;}
        int32 Changed=0;for(int32 I=0;I<Pixels.Num();++I){if(Pixels[I]!=Before[I]){++Changed;}}
        Test->TestTrue(TEXT("Pause transition updates rendered native action panel"),PC->IsPaused()&&Changed>100);
        PC->TogglePauseOrClose();return true;
    }
private:
    FAutomationTestBase* Test;double Start=-1,PhaseStart=0;int32 Phase=0;
    TSharedPtr<SWindow> Window;TSharedPtr<SLinkHUD> HUD;TArray<FColor> Before;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTripletHUDPixels,"Task0172.Rendered.HUD.StateTransitionPixels",EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::ProductFilter)
bool FTripletHUDPixels::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/EntryGate")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FTripletRenderedHUD(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
