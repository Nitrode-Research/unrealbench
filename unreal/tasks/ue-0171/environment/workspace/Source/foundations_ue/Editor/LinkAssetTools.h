#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LinkAssetTools.generated.h"

class UBlendSpace;
class USkeletalMesh;
class UStaticMesh;

/** Editor production hooks; no gameplay graph is required or created. */
UCLASS()
class FOUNDATIONS_UE_API ULinkAssetTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Foundations|Editor", meta=(DevelopmentOnly))
    static bool FinalizeBlendSpace(UBlendSpace* BlendSpace);
    UFUNCTION(BlueprintCallable, Category="Foundations|Editor", meta=(DevelopmentOnly))
    static bool CreateChainSocket(USkeletalMesh* Mesh, float AlongCalf);
    /** Build a native static mesh from the validated Unity export. Bake the linear
     * transform, retain its pivot on the actor, and preserve all mesh channels. */
    UFUNCTION(BlueprintCallable, Category="Foundations|Editor", meta=(DevelopmentOnly))
    static UStaticMesh* BuildSourceMesh(const FString& JsonFile, const FString& AssetPath, const TArray<double>& UnityMatrix, bool bCollision);
    UFUNCTION(BlueprintCallable, Category="Foundations|Editor", meta=(DevelopmentOnly))
    static bool ApplySourceBoxTransform(AActor* Actor, const TArray<double>& UnityBoxMatrix);
    UFUNCTION(BlueprintCallable, Category="Foundations|Editor", meta=(DevelopmentOnly))
    static FString InspectSourceMesh(UStaticMesh* Mesh);
    UFUNCTION(BlueprintCallable, Category="Foundations|Editor", meta=(DevelopmentOnly))
    static void SetSourceNavigation(AActor* Actor, bool bRelevant, bool bExcluded);
};
