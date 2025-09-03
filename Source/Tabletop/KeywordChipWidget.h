// KeywordChipWidget.h
#pragma once
#include "CoreMinimal.h"
#include "WeaponKeywordHelpers.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "KeywordChipWidget.generated.h"

class UImage;
class UTextBlock;
class UBorder;

UCLASS()

class UKeywordChipWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable)
	void SetLabel(const FText& InText);
	
	void SetTooltip(const FText& InText);
	
	UFUNCTION(BlueprintCallable)
	void SetState(bool bActiveNow, bool bConditional);
	
	UFUNCTION(BlueprintCallable)
	void SetIconForKeyword(EWeaponKeyword Keyword);

protected:
	UPROPERTY(meta=(BindWidget)) UTextBlock* Label = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) UImage*   Icon = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) UBorder*  Pill = nullptr; // for background color
};
