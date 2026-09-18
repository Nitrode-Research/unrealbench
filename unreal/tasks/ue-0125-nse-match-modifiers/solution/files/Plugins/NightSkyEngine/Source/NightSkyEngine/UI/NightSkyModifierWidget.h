#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NightSkyModifierWidget.generated.h"
class UTextBlock;
UCLASS()
class NIGHTSKYENGINE_API UNightSkyModifierWidget : public UUserWidget
{
	GENERATED_BODY()
  public:
	UFUNCTION(BlueprintCallable)
	void RefreshRules();
	UFUNCTION(BlueprintPure)
	FText GetRenderedRuleText() const;
	UPROPERTY(BlueprintReadOnly)
	FString DisplayedRules;
	UPROPERTY(BlueprintReadOnly)
	int32 DisplayedRound = 0;
	UPROPERTY(BlueprintReadOnly)
	int32 DisplayedFrame = -1;

  protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	UPROPERTY()
	UTextBlock* Label;
};
