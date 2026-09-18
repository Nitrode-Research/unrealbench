#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NightSkyEngine/Battle/MatchModifiers.h"
#include "ModifierSetupWidget.generated.h"
class UCheckBox;
class USpinBox;
class UTextBlock;
/** Native sample setup adapter. Validation and gameplay remain in the battle runtime. */
UCLASS()
class NIGHTSKYENGINE_API UModifierSetupWidget : public UUserWidget
{
	GENERATED_BODY()
  public:
	UFUNCTION(BlueprintCallable, Category = "Match modifiers")
	void ConfigureForMatch(const FModifierConfiguration& Configuration);
	UFUNCTION(BlueprintCallable, Category = "Match modifiers")
	bool SetRuleEnabled(const FString& Identifier, bool Enabled);
	UFUNCTION(BlueprintCallable, Category = "Match modifiers")
	bool SetInterval(int32 Index, int32 Round, int32 Start, int32 Duration);
	UFUNCTION(BlueprintCallable, Category = "Match modifiers")
	bool StartSelectedMatch();
	UFUNCTION(BlueprintPure, Category = "Match modifiers")
	FText GetSetupSummary() const;
	UFUNCTION(BlueprintPure, Category = "Match modifiers")
	FText GetSetupError() const;

  protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};
