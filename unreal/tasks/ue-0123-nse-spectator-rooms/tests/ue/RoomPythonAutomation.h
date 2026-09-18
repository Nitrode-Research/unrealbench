#pragma once
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

/** Latent launcher for separate-process Unreal scenarios. No missing-evidence pass. */
class FRoomPythonCommand : public IAutomationLatentCommand
{
  public:
    FRoomPythonCommand(FAutomationTestBase *InTest,
                       const FString &InScript,
                       const FString &InArguments = TEXT(""),
                       double InTimeout = 360.)
        : Test(InTest), Script(InScript), Arguments(InArguments), Timeout(InTimeout)
    {
    }

    virtual ~FRoomPythonCommand()
    {
        if (Handle.IsValid())
        {
            if (FPlatformProcess::IsProcRunning(Handle))
                FPlatformProcess::TerminateProc(Handle, true);
            FPlatformProcess::CloseProc(Handle);
        }
        FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
    }

    virtual bool Update() override
    {
        if (!bStarted)
        {
            bStarted = true;
            Start = FPlatformTime::Seconds();
            const FString Driver = FPaths::ConvertRelativePathToFull(
                FPaths::ProjectDir() / TEXT("Source/NightSkyEngineDemo/Tests") / Script);
            if (!FPaths::FileExists(Driver))
            {
                Test->AddError(TEXT("Injected network driver missing: ") + Driver);
                return true;
            }
            FString Python;
            {
#if PLATFORM_WINDOWS
                Python = FPaths::ConvertRelativePathToFull(
                    FPaths::EngineDir() / TEXT("Binaries/ThirdParty/Python3/Win64/python.exe"));
                if (!FPaths::FileExists(Python))
                    Python = TEXT("python.exe");
#else
                Python = TEXT("/usr/bin/python3");
#endif
            }
            Directory =
                FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/Room") /
                                                  (FPaths::GetBaseFilename(Script) + TEXT("_") +
                                                   FGuid::NewGuid().ToString(EGuidFormats::Digits)));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Directory), true);
            // Nested editors are spawned by the Python driver and never see the
            // command line this editor was given, so the RHI selection from
            // spec.yaml would be lost on them. Forward only capability flags:
            // the driver still decides -NullRHI versus -RenderOffscreen per role.
            FString EditorArgs;
            {
                static const TCHAR *const Capability[] = {
                    TEXT("-d3d11"), TEXT("-d3d12"), TEXT("-vulkan"), TEXT("-opengl"),
                    TEXT("-sm5"), TEXT("-sm6"), TEXT("-AllowSoftwareRendering")};
                const FString Parent = FCommandLine::Get();
                for (const TCHAR *const Flag : Capability)
                {
                    if (Parent.Contains(Flag, ESearchCase::IgnoreCase))
                    {
                        if (!EditorArgs.IsEmpty())
                        {
                            EditorArgs += TEXT(" ");
                        }
                        EditorArgs += Flag;
                    }
                }
            }
            const FString Command = Quote(Driver) + TEXT(" ") +
                                    Quote(FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())) +
                                    TEXT(" ") + Quote(Directory) + TEXT(" --editor ") +
                                    Quote(FPlatformProcess::ExecutablePath()) +
                                    (EditorArgs.IsEmpty() ? TEXT("") : TEXT(" --editor-args=") + Quote(EditorArgs)) +
                                    TEXT(" ") + Arguments;
            FPlatformProcess::CreatePipe(ReadPipe, WritePipe);
            Handle = FPlatformProcess::CreateProc(
                *Python, *Command, false, true, true, nullptr, 0, nullptr, WritePipe);
            if (!Handle.IsValid())
            {
                Test->AddError(TEXT("Could not launch network scenario Python: ") + Python);
                return true;
            }
        }
        Output += FPlatformProcess::ReadPipe(ReadPipe);
        if (FPlatformProcess::IsProcRunning(Handle))
        {
            if (FPlatformTime::Seconds() - Start < Timeout)
                return false;
            FPlatformProcess::TerminateProc(Handle, true);
            FFileHelper::SaveStringToFile(Output, *(Directory + TEXT("-driver.log")));
            FFileHelper::SaveStringToFile(
                FString::Printf(TEXT("{\"passed\":false,\"reason\":\"timeout\",\"budget_seconds\":%.0f}"),
                                Timeout),
                *(Directory / TEXT("timeout.json")));
            Test->AddError(TEXT("Network scenario timeout; logs: ") + Directory);
            return true;
        }
        int32 ExitCode = -1;
        FPlatformProcess::GetProcReturnCode(Handle, &ExitCode);
        Output += FPlatformProcess::ReadPipe(ReadPipe);
        FFileHelper::SaveStringToFile(Output, *(Directory + TEXT("-driver.log")));
        Test->TestEqual(TEXT("Separate-process scenario exit code"), ExitCode, 0);
        FString Json;
        TSharedPtr<FJsonObject> Result;
        if (!FFileHelper::LoadFileToString(Json, *(Directory / TEXT("result.json"))) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Result) || !Result.IsValid())
            Test->AddError(TEXT("Missing valid scenario result and assertion evidence: ") + Directory);
        else
        {
            Test->TestTrue(TEXT("Scenario passed actual behavioral assertions"),
                           Result->GetBoolField(TEXT("passed")));
            Test->TestTrue(TEXT("Scenario executed nonzero assertions"),
                           Result->GetNumberField(TEXT("assertions")) > 0);
        }
        Test->AddInfo(TEXT("Network artifacts: ") + Directory);
        return true;
    }

  private:
    static FString Quote(FString Value)
    {
#if !PLATFORM_WINDOWS
        Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
#endif
        Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
        return TEXT("\"") + Value + TEXT("\"");
    }

    FAutomationTestBase *Test;
    FString Script, Arguments, Directory, Output;
    double Timeout, Start = 0.;
    bool bStarted = false;
    FProcHandle Handle;
    void *ReadPipe = nullptr;
    void *WritePipe = nullptr;
};
