#pragma once
#include "RoomPythonAutomation.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"

// Call AddSample only after the enclosing latent scenario has settled rendering.
// This helper reads the exact supplied texture, never a replacement capture of
// candidate actors. The candidate argument must be PresentationTexture().
struct FRoomRenderedEvidence
{
    FString Directory = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("Automation") /
        (TEXT("RoomRendered-") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    TMap<FString, TArray<TSharedPtr<FJsonValue>>> Groups;
    TArray<TSharedPtr<FJsonValue>> Requirements;

    bool AddSample(FAutomationTestBase &Test, const FString &Group,
                   UTextureRenderTarget2D *Texture)
    {
        if (!Test.TestNotNull(TEXT("actual rendered evidence texture exists"), Texture))
            return false;
        if (!Test.TestTrue(TEXT("actual rendered evidence has positive dimensions"),
                           Texture->SizeX > 0 && Texture->SizeY > 0))
            return false;
        auto *Resource = Texture->GameThread_GetRenderTargetResource();
        if (!Test.TestNotNull(TEXT("actual rendered evidence resource exists"), Resource))
            return false;
        TArray<FColor> Pixels;
        const bool Read = Resource->ReadPixels(Pixels);
        if (!Test.TestTrue(TEXT("actual rendered evidence readback succeeds"),
                           Read && int64(Pixels.Num()) == int64(Texture->SizeX) * Texture->SizeY))
            return false;
        IFileManager::Get().MakeDirectory(*Directory, true);
        const FString Path = Directory / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".bmp"));
        if (!Test.TestTrue(TEXT("actual rendered evidence saved for blind review"),
                           FFileHelper::CreateBitmap(*Path, Texture->SizeX, Texture->SizeY, Pixels.GetData())))
            return false;
        Groups.FindOrAdd(Group).Add(MakeShared<FJsonValueString>(Path));
        return true;
    }

    void Require(const FString &Field, const FString &First,
                 const FString &Second = FString(), bool Expected = true)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("field"), Field);
        Row->SetBoolField(TEXT("expected"), Expected);
        TArray<TSharedPtr<FJsonValue>> Names{MakeShared<FJsonValueString>(First)};
        if (!Second.IsEmpty())
            Names.Add(MakeShared<FJsonValueString>(Second));
        Row->SetArrayField(TEXT("groups"), Names);
        Requirements.Add(MakeShared<FJsonValueObject>(Row));
    }

    bool Finish(FAutomationTestBase &Test)
    {
        auto Manifest = MakeShared<FJsonObject>();
        auto Images = MakeShared<FJsonObject>();
        for (const auto &Group : Groups)
            Images->SetArrayField(Group.Key, Group.Value);
        Manifest->SetObjectField(TEXT("groups"), Images);
        Manifest->SetArrayField(TEXT("requirements"), Requirements);
        FString Json;
        FJsonSerializer::Serialize(Manifest, TJsonWriterFactory<>::Create(&Json));
        const FString Path = Directory / TEXT("private-manifest.json");
        if (!Test.TestTrue(TEXT("rendered observation manifest saved"),
                           FFileHelper::SaveStringToFile(Json, *Path)))
            return false;
        ADD_LATENT_AUTOMATION_COMMAND(FRoomPythonCommand(
            &Test, TEXT("room_render_review.py"),
            FString::Printf(TEXT("--observations \"%s\""), *Path), 115.));
        return true;
    }
};
