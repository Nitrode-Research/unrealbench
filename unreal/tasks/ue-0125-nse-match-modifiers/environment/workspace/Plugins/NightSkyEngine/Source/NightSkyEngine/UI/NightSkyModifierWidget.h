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

  protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
};
