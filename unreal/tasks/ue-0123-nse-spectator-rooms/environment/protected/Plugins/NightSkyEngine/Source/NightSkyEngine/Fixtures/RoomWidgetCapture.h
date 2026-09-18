#pragma once

#include "CoreMinimal.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/SWindow.h"
#include "Input/HittestGrid.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"

namespace RoomWidgetCapture
{
// Render the same mounted Slate window that received user input. DrawWindow
// paints custom Slate/UMG content as well as ordinary text controls.
inline bool SaveWindow(const TSharedPtr<SWindow> &Window, FString &Path)
{
    Path.Empty();
    if (!Window.IsValid() || !FApp::CanEverRender())
        return false;
    const FVector2D Size = Window->GetSizeInScreen();
    const int32 Width = FMath::CeilToInt(Size.X);
    const int32 Height = FMath::CeilToInt(Size.Y);
    if (Width <= 0 || Height <= 0)
        return false;
    TStrongObjectPtr<UTextureRenderTarget2D> Target(
        FWidgetRenderer::CreateTargetFor(FVector2D(Width, Height), TF_Bilinear, true));
    if (!Target.IsValid())
        return false;
    auto *Renderer = new FWidgetRenderer(true);
    FHittestGrid HitTestGrid;
    Renderer->DrawWindow(Target.Get(), HitTestGrid, Window.ToSharedRef(),
                         Window->GetWindowGeometryInWindow(),
                         Window->GetClippingRectangleInWindow(), 0.f, false);
    BeginCleanup(Renderer);
    FlushRenderingCommands();
    auto *Resource = Target->GameThread_GetRenderTargetResource();
    TArray<FColor> Pixels;
    if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != Width * Height)
        return false;
    TArray64<uint8> PNG;
    FImageUtils::PNGCompressImageArray(Width, Height,
        TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), PNG);
    if (PNG.IsEmpty())
        return false;
    const FString Directory = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("Automation/RoomWidgetPixels"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    Path = Directory / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".png"));
    return FFileHelper::SaveArrayToFile(PNG, *Path);
}
}
