#pragma once
#include "Misc/Paths.h"

namespace ModifierPythonRuntime
{
inline FString Executable()
{
#if PLATFORM_WINDOWS
	return FPaths::ConvertRelativePathToFull(FPaths::EngineDir() /
	                                         TEXT("Binaries/ThirdParty/Python3/Win64/python.exe"));
#elif PLATFORM_MAC
	return FPaths::ConvertRelativePathToFull(FPaths::EngineDir() /
	                                         TEXT("Binaries/ThirdParty/Python3/Mac/bin/python3"));
#else
	return FPaths::ConvertRelativePathToFull(FPaths::EngineDir() /
	                                         TEXT("Binaries/ThirdParty/Python3/Linux/bin/python3"));
#endif
}
inline FString QuotePath(FString Value)
{
	FPaths::NormalizeFilename(Value);
	Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
	return TEXT("\"") + Value + TEXT("\"");
}
} // namespace ModifierPythonRuntime
