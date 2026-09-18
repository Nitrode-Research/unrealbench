#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"

#include "Blueprint/UserWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/Clipping.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Widgets/SWindow.h"
#include "Widgets/SNullWidget.h"
#include "Engine/World.h"

namespace RoomWidgetTestAccess
{
enum class EAttemptResult { Failed, Unavailable, Dispatched };

// A test-owned window gives native and worker scenarios the same laid-out input path.
// Input goes through Slate hit testing and focus; no production setter or action delegate is called.
class FPanelHost
{
public:
    FPanelHost() = default;
    FPanelHost(const FPanelHost &) = delete;
    FPanelHost &operator=(const FPanelHost &) = delete;
    ~FPanelHost() { Reset(); }

    TSharedPtr<SWindow> GetWindow() const { return Window; }

    void Reset()
    {
        if (WorldCleanupHandle.IsValid())
        {
            FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
            WorldCleanupHandle.Reset();
        }
        MountedWorld.Reset();
        if (Window.IsValid())
        {
            // Window destruction may be deferred. Release UObject-backed content now.
            Window->SetContent(SNullWidget::NullWidget);
            if (FSlateApplication::IsInitialized())
                FSlateApplication::Get().RequestDestroyWindow(Window.ToSharedRef());
        }
        Window.Reset();
    }

    bool Mount(UUserWidget *Panel)
    {
        Reset();
        if (!Panel || !FSlateApplication::IsInitialized())
        {
            InputFailure(TEXT("mount prerequisite"));
            return false;
        }
        Panel->RemoveFromParent();
        Window = SNew(SWindow)
            .Title(FText::FromString(TEXT("Room widget test controls")))
            .ClientSize(FVector2D(1600, 1200))
            .SaneWindowPlacement(false)
            .FocusWhenFirstShown(false)
            .SupportsMaximize(false)
            .SupportsMinimize(false);
        Window->SetContent(Panel->TakeWidget());
        MountedWorld = Panel->GetWorld();
        WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FPanelHost::OnWorldCleanup);
        FSlateApplication::Get().AddWindow(Window.ToSharedRef(), true);
        const bool Refreshed = Refresh();
        if (!Refreshed)
            InputFailure(TEXT("mount refresh"));
        return Refreshed;
    }

    bool Click(UWidget *Widget) { return AttemptClick(Widget) == EAttemptResult::Dispatched; }
    bool Enter(UWidget *Widget, const FString &Text)
    {
        return AttemptEnter(Widget, Text) == EAttemptResult::Dispatched;
    }

    // Reveal through ordinary wheel events. This also supports non-interactive
    // text being brought into view for a screenshot without clicking it.
    bool Reveal(UWidget *Widget)
    {
        const double Started = FPlatformTime::Seconds();
        int32 Steps = 0;
        bool WindowHit = false, RegionHit = false, WheelHandled = false;
        FSlateRect LastTarget(0, 0, 0, 0), LastViewport(0, 0, 0, 0);
        const auto Fail = [&](const TCHAR *Stage) {
            UE_LOG(LogTemp, Warning,
                   TEXT("Room widget reveal failed: stage=%s steps=%d elapsed=%.3f target=[%.1f,%.1f,%.1f,%.1f] viewport=[%.1f,%.1f,%.1f,%.1f] window-hit=%d region-hit=%d wheel-handled=%d"),
                   Stage, Steps, FPlatformTime::Seconds() - Started,
                   LastTarget.Left, LastTarget.Top, LastTarget.Right, LastTarget.Bottom,
                   LastViewport.Left, LastViewport.Top, LastViewport.Right, LastViewport.Bottom,
                   int32(WindowHit), int32(RegionHit), int32(WheelHandled));
            return false;
        };
        if (!Window.IsValid() || !FSlateApplication::IsInitialized() || !Widget || !Widget->IsVisible())
            return Fail(TEXT("prerequisite"));
        auto &App = FSlateApplication::Get();
        if (App.HasAnyMouseCaptor())
            return Fail(TEXT("mouse capture"));
        const TSharedRef<SWidget> Target = Widget->TakeWidget();
        const double Deadline = FPlatformTime::Seconds() + 2.0;
        for (int32 Attempt = 0; Attempt < 128 && FPlatformTime::Seconds() < Deadline; ++Attempt)
        {
            Steps = Attempt + 1;
            if (!Refresh() || !Window.IsValid())
                return Fail(TEXT("Slate refresh"));
            // The fast application path may reuse cached geometry. Arrange from
            // the current window geometry without changing the engine's path mode.
            FArrangedChildren WindowRoot(EVisibility::Visible);
            WindowRoot.AddWidget(FArrangedWidget(Window.ToSharedRef(), Window->GetWindowGeometryInScreen()));
            FWidgetPath Path(Window, WindowRoot);
            if (!Path.ExtendPathTo(FWidgetMatcher(Target), EVisibility::Visible))
                return Fail(TEXT("arranged path"));
            // Offscreen widgets may not have painted yet. Use the freshly arranged
            // target geometry from the path, as we do for its clipping ancestors.
            const FSlateRect TargetRect = Path.Widgets[Path.Widgets.Num() - 1].Geometry.GetRenderBoundingRect();
            LastTarget = TargetRect;
            if (TargetRect.Right <= TargetRect.Left || TargetRect.Bottom <= TargetRect.Top)
                return Fail(TEXT("target geometry"));
            FSlateRect Viewport = Window->GetClientRectInScreen();
            TSharedPtr<SWidget> ScrollRegion;
            FindVisibleClip(Path, Viewport, ScrollRegion);
            LastViewport = Viewport;
            if (Viewport.Right <= Viewport.Left || Viewport.Bottom <= Viewport.Top)
                return Fail(TEXT("viewport geometry"));
            const auto Gap = [](float Low, float High, float VisibleLow, float VisibleHigh) {
                // A control larger than its viewport can still expose its center.
                if (High - Low > VisibleHigh - VisibleLow)
                    Low = High = (Low + High) * .5f;
                return Low < VisibleLow ? Low - VisibleLow : High > VisibleHigh ? High - VisibleHigh : 0.f;
            };
            const float Vertical = Gap(TargetRect.Top, TargetRect.Bottom, Viewport.Top, Viewport.Bottom);
            const float Horizontal = Gap(TargetRect.Left, TargetRect.Right, Viewport.Left, Viewport.Right);
            if (Vertical == 0.f && Horizontal == 0.f)
                return true;
            if (!ScrollRegion.IsValid())
                return Fail(TEXT("clipping ancestor"));
            const FVector2D Position((Viewport.Left + Viewport.Right) * .5f,
                                     (Viewport.Top + Viewport.Bottom) * .5f);
            const TSet<FKey> Released;
            const FModifierKeysState Modifiers;
            App.ProcessMouseMoveEvent(FPointerEvent(0, Position, Position, Released, FKey(), 0, Modifiers), true);
            const FWidgetPath WheelPath = App.LocateWindowUnderMouse(Position, App.GetInteractiveTopLevelWindows());
            WindowHit = WheelPath.ContainsWidget(Window.Get());
            RegionHit = WheelPath.ContainsWidget(ScrollRegion.Get());
            if (!WindowHit || !RegionHit)
                return Fail(TEXT("wheel hit path"));
            const float Distance = Vertical != 0.f ? Vertical : Horizontal;
            const float Extent = Vertical != 0.f ? Viewport.Bottom - Viewport.Top : Viewport.Right - Viewport.Left;
            const float Delta = -FMath::Sign(Distance) * FMath::Clamp(FMath::Abs(Distance) / Extent, .25f, 3.f);
            WheelHandled = App.ProcessMouseWheelOrGestureEvent(
                FPointerEvent(0, Position, Position, Released, FKey(), Delta, Modifiers), nullptr);
            // An unhandled wheel may mean the desired offset reached the edge
            // while animated geometry is still catching up. Allow a bounded tick.
            FPlatformProcess::SleepNoStats(1.f / 120.f);
        }
        return Fail(TEXT("navigation budget"));
    }

    EAttemptResult AttemptClick(UWidget *Widget)
    {
        if (!Window.IsValid() || !FSlateApplication::IsInitialized() || !Widget)
            return InputFailure(TEXT("click prerequisite"));
        if (!Widget->IsVisible())
            return EAttemptResult::Unavailable;
        auto &App = FSlateApplication::Get();
        if (App.HasAnyMouseCaptor())
            return InputFailure(TEXT("click mouse capture"));
        const TSharedRef<SWidget> Target = Widget->TakeWidget();
        if (!Reveal(Widget))
            return InputFailure(TEXT("click reveal"));
        // Reveal and click must use the same current layout. A control that has
        // only just been mounted/revealed may still have an older paint cache.
        FArrangedChildren WindowRoot(EVisibility::Visible);
        WindowRoot.AddWidget(FArrangedWidget(Window.ToSharedRef(), Window->GetWindowGeometryInScreen()));
        FWidgetPath ArrangedPath(Window, WindowRoot);
        if (!ArrangedPath.ExtendPathTo(FWidgetMatcher(Target), EVisibility::Visible))
            return InputFailure(TEXT("click arranged path"));
        const FGeometry &Geometry = ArrangedPath.Widgets[ArrangedPath.Widgets.Num() - 1].Geometry;
        if (Geometry.GetLocalSize().X <= 0 || Geometry.GetLocalSize().Y <= 0)
            return InputFailure(TEXT("click arranged geometry"));
        const FVector2D Position = Geometry.GetAbsolutePositionAtCoordinates(FVector2D(.5, .5));
        const TSet<FKey> Released;
        const FModifierKeysState Modifiers;
        App.ProcessMouseMoveEvent(FPointerEvent(0, Position, Position, Released, FKey(), 0, Modifiers), true);
        const FWidgetPath Path = App.LocateWindowUnderMouse(Position, App.GetInteractiveTopLevelWindows());
        if (!Widget->GetIsEnabled() || !Path.ContainsWidget(Window.Get()) ||
            !Path.ContainsWidget(&Target.Get()))
        {
            // A visible control is unavailable only when the disabled hit path reaches it.
            // Covered or still-clipped controls remain failed attempts after wheel navigation.
            const FWidgetPath DisabledPath =
                App.LocateWindowUnderMouse(Position, App.GetInteractiveTopLevelWindows(), true);
            const bool Unavailable = DisabledPath.ContainsWidget(Window.Get()) &&
                                     DisabledPath.ContainsWidget(&Target.Get());
            if (!Unavailable)
            {
                const FVector2D CachedPosition = Widget->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(.5, .5));
                UE_LOG(LogTemp, Warning,
                       TEXT("Room widget click hit mismatch: arranged=[%.1f,%.1f] cached=[%.1f,%.1f] enabled=%d window-hit=%d target-hit=%d disabled-window-hit=%d disabled-target-hit=%d"),
                       Position.X, Position.Y, CachedPosition.X, CachedPosition.Y, int32(Widget->GetIsEnabled()),
                       int32(Path.ContainsWidget(Window.Get())), int32(Path.ContainsWidget(&Target.Get())),
                       int32(DisabledPath.ContainsWidget(Window.Get())), int32(DisabledPath.ContainsWidget(&Target.Get())));
            }
            return Unavailable ? EAttemptResult::Unavailable : InputFailure(TEXT("click hit path"));
        }
        TSet<FKey> Pressed;
        Pressed.Add(EKeys::LeftMouseButton);
        const bool Down = App.ProcessMouseButtonDownEvent(
            nullptr, FPointerEvent(0, Position, Position, Pressed, EKeys::LeftMouseButton, 0, Modifiers));
        const bool Up = App.ProcessMouseButtonUpEvent(
            FPointerEvent(0, Position, Position, Released, EKeys::LeftMouseButton, 0, Modifiers));
        return Refresh() && (Down || Up) ? EAttemptResult::Dispatched : InputFailure(TEXT("click dispatch or refresh"));
    }

    EAttemptResult AttemptEnter(UWidget *Widget, const FString &Text)
    {
        const EAttemptResult ClickResult = AttemptClick(Widget);
        if (ClickResult != EAttemptResult::Dispatched)
        {
            InputFailure(TEXT("enter click"));
            return ClickResult;
        }
        auto &App = FSlateApplication::Get();
        const TSharedPtr<SWidget> Focus = App.GetUserFocusedWidget(0);
        const TSharedRef<SWidget> Target = Widget->TakeWidget();
        if (!Focus.IsValid() || (Focus != Target && !App.HasUserFocusedDescendants(Target, 0)))
            return InputFailure(TEXT("enter focus"));
        // Slate uses Command for standard editing shortcuts on macOS, Control elsewhere.
        const FModifierKeysState SelectAll(false, false, !PLATFORM_MAC, false,
                                          false, false, PLATFORM_MAC, false, false);
        if (!Key(EKeys::A, SelectAll))
            return InputFailure(TEXT("enter select all"));
        if (!Key(EKeys::BackSpace, FModifierKeysState(), TCHAR(8)))
            return InputFailure(TEXT("enter backspace"));
        for (const TCHAR Character : Text)
            if (!App.ProcessKeyCharEvent(FCharacterEvent(Character, FModifierKeysState(), uint32(0), false)))
                return InputFailure(TEXT("enter character"));
        Key(EKeys::Enter);
        return Refresh() ? EAttemptResult::Dispatched : InputFailure(TEXT("enter refresh"));
    }

private:
    static EAttemptResult InputFailure(const TCHAR *Stage)
    {
        const bool Initialized = FSlateApplication::IsInitialized();
        UE_LOG(LogTemp, Warning,
               TEXT("Room widget input failed: stage=%s slate=%d ticking=%d mouse-capture=%d focus=%d"),
               Stage, int32(Initialized),
               int32(Initialized && FSlateApplication::Get().IsTicking()),
               int32(Initialized && FSlateApplication::Get().HasAnyMouseCaptor()),
               int32(Initialized && FSlateApplication::Get().GetUserFocusedWidget(0).IsValid()));
        return EAttemptResult::Failed;
    }

    static void FindVisibleClip(const FWidgetPath &Path, FSlateRect &Viewport, TSharedPtr<SWidget> &ScrollRegion)
    {
        const auto Intersect = [](const FSlateRect &A, const FSlateRect &B) {
            return FSlateRect(FMath::Max(A.Left, B.Left), FMath::Max(A.Top, B.Top),
                              FMath::Min(A.Right, B.Right), FMath::Min(A.Bottom, B.Bottom));
        };
        FSlateRect Clip = Viewport;
        FSlateRect AlwaysClip = Viewport;
        // The target's own clipping does not limit its position in its parent.
        for (int32 Index = 0; Index + 1 < Path.Widgets.Num(); ++Index)
        {
            const FArrangedWidget &Ancestor = Path.Widgets[Index];
            bool Clips = false, Always = false, IntersectsParent = true;
            const FSlateRect NextClip = Ancestor.Widget->CalculateCullingAndClippingRules(
                Ancestor.Geometry, Clip, Clips, Always, IntersectsParent);
            if (!Clips)
                continue;
            if (Always)
            {
                bool UnusedClips, UnusedAlways, UnusedIntersects;
                AlwaysClip = Ancestor.Widget->CalculateCullingAndClippingRules(
                    Ancestor.Geometry, AlwaysClip, UnusedClips, UnusedAlways, UnusedIntersects);
            }
            // Use Slate's clipping rules, including proxies and OnDemand overflow.
            // Preserve always-clipped ancestors when a child starts a new clip.
            Clip = Intersect(AlwaysClip, NextClip);
            if (Clip.Right > Clip.Left && Clip.Bottom > Clip.Top)
            {
                Viewport = Clip;
                ScrollRegion = Ancestor.Widget;
            }
        }
    }

    void OnWorldCleanup(UWorld *World, bool, bool)
    {
        if (World && MountedWorld == TWeakObjectPtr<UWorld>(World))
            Reset();
    }

    bool Refresh()
    {
        auto &App = FSlateApplication::Get();
        if (App.IsTicking())
            return false;
        App.Tick();
        return true;
    }

    bool Key(FKey Code, const FModifierKeysState &Modifiers = FModifierKeysState(), TCHAR Character = 0)
    {
        auto &App = FSlateApplication::Get();
        const FKeyEvent Event(Code, Modifiers, uint32(0), false, Character, 0);
        const bool KeyHandled = App.ProcessKeyDownEvent(Event);
        // Native text input sends a character event between key down and key up.
        // Slate handles plain Backspace in OnKeyChar, including an empty field.
        const bool CharacterHandled = Character != 0 &&
            App.ProcessKeyCharEvent(FCharacterEvent(Character, Modifiers, uint32(0), false));
        App.ProcessKeyUpEvent(Event);
        return KeyHandled || CharacterHandled;
    }

    TSharedPtr<SWindow> Window;
    TWeakObjectPtr<UWorld> MountedWorld;
    FDelegateHandle WorldCleanupHandle;
};

// These observation helpers retain the existing text/masking checks. Custom paint
// and wrappers without these properties still need independent visual observations.
inline FText GetText(UWidget *Widget)
{
    if (!Widget || !Widget->IsVisible())
        return FText();
    if (UFunction *Function = Widget->FindFunction(TEXT("GetText")))
    {
        FStructOnScope Parameters(Function);
        Widget->ProcessEvent(Function, Parameters.GetStructMemory());
        for (TFieldIterator<FTextProperty> It(Function); It; ++It)
            if (It->HasAnyPropertyFlags(CPF_ReturnParm))
                return It->GetPropertyValue_InContainer(Parameters.GetStructMemory());
    }
    // Some UMG text widgets expose a reflected Text property with a native-only getter.
    const auto *Property = FindFProperty<FTextProperty>(Widget->GetClass(), TEXT("Text"));
    return Property ? Property->GetPropertyValue_InContainer(Widget) : FText();
}
inline bool IsPassword(UWidget *Widget)
{
    const auto *Property = Widget ? FindFProperty<FBoolProperty>(Widget->GetClass(), TEXT("IsPassword")) : nullptr;
    return Property && Property->GetPropertyValue_InContainer(Widget);
}
}
