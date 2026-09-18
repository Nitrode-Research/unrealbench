#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NightSkyEngine/Battle/MatchModifiers.h"
#include "ModifierSetupWidget.generated.h"
class UTextBlock;
class SVerticalBox;
class UNightSkyGameInstance;
class ANightSkyGameState;
/** Native authoring screen; all validation and event behavior belong to the runtime. */
UCLASS()
class NIGHTSKYENGINE_API UModifierSetupWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintCallable) void ConfigureForMatch(const FModifierConfiguration& Configuration);
 UFUNCTION(BlueprintCallable) bool SetRuleEnabled(const FString& Identifier, bool Enabled);
 UFUNCTION(BlueprintCallable) bool SetInterval(int32 Index, int32 Round, int32 Start, int32 Duration);
 UFUNCTION(BlueprintCallable) bool StartSelectedMatch();
 UFUNCTION(BlueprintCallable) bool StartNetworkMatch(bool Host, const FString& HostAddress);
 UFUNCTION(BlueprintPure) FText GetSetupSummary() const;
 UFUNCTION(BlueprintPure) FText GetSetupError() const;
 UFUNCTION(BlueprintPure) FModifierConfiguration GetSelection() const;
 // Only the standalone authoring arena installs newly authored definitions. Online/replay validation is unchanged.
 FModifierConfiguration GetAuthoredConfiguration() const { return Candidate; }
 bool AllowAuthoring = false;
 TFunction<void(const FModifierConfiguration&)> OnAccepted;
 TFunction<void(const FModifierConfiguration&, bool, const FString&)> OnNetwork;
 TFunction<void()> OnReplay;
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
private:
 UPROPERTY() FModifierConfiguration Candidate;
 UPROPERTY() TArray<FString> EnabledIds;
 UPROPERTY() TObjectPtr<UTextBlock> SummaryLabel;
 UPROPERTY() TObjectPtr<UTextBlock> ErrorLabel;
 TSharedPtr<SVerticalBox> RuleList;
 bool Configured = false;
 FString Address = TEXT("127.0.0.1:7777");
 FString Error;
 bool Enabled(const FString& Id) const;
 void BuildCards();
 void RefreshSummary();
 void ShowError(const FString& Reason);
 bool ValidateSelection(FModifierConfiguration& Selection);
};
