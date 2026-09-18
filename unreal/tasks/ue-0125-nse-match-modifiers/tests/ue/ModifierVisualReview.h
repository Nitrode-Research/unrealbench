#pragma once
#include "ModifierPythonRuntime.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"

namespace ModifierVisualReview
{
inline bool Verify(UTextureRenderTarget2D* Target, const FString& Name, int32 Frames)
{
	if (!Target) return false;
	const FString Image = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() /
	    TEXT("Automation/Modifiers/visual-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".png"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Image), true);
	TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*Image));
	if (!File || !FImageUtils::ExportRenderTarget2DAsPNG(Target, *File)) return false;
	File.Reset();
	const FString Driver = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() /
	    TEXT("Source/NightSkyEngineDemo/Tests/modifier_visual_reader.py"));
	const FString Args = ModifierPythonRuntime::QuotePath(Driver) + TEXT(" --verify ") +
	    ModifierPythonRuntime::QuotePath(Image) + TEXT(" --name ") +
	    ModifierPythonRuntime::QuotePath(Name) + FString::Printf(TEXT(" --frames %d"), Frames);
	int32 Exit = -1;
	FString Output, Error;
	// Python bounds the blind reader to 55 seconds, including review-file reload.
	FPlatformProcess::ExecProcess(*ModifierPythonRuntime::Executable(), *Args, &Exit, &Output, &Error);
	UE_LOG(LogTemp, Display, TEXT("Modifier visual review: %s %s; image %s"), *Output, *Error, *Image);
	return Exit == 0;
}
}
