#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WeaponKeywordHelpers.h" // for EKeywordUIState, FKeywordUIInfo
#include "KeywordChipWidget.generated.h"

class UBorder;
class UTextBlock;
class UImage;

UCLASS()
class TABLETOP_API UKeywordChipWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable) void InitFromInfo(const FKeywordUIInfo& Info);
	UFUNCTION(BlueprintCallable) void SetState(EKeywordUIState InState);
	UFUNCTION(BlueprintCallable) void SetLabel(const FText& InText);
	
	void SetTooltip(const FText& InText);

protected:
	virtual void NativeConstruct() override;

	void ApplyVisuals();

	UPROPERTY(meta=(BindWidget)) UBorder*    ChipBorder = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* LabelText  = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) UImage*     StateIcon  = nullptr; // optional

	UPROPERTY(EditDefaultsOnly, Category="Style") FLinearColor ActiveBg     = FLinearColor(0.12f,0.62f,0.30f,1.f);
	UPROPERTY(EditDefaultsOnly, Category="Style") FLinearColor ActiveText   = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, Category="Style") FLinearColor ConditionalBg   = FLinearColor(0.18f,0.45f,0.72f,1.f);
	UPROPERTY(EditDefaultsOnly, Category="Style") FLinearColor ConditionalText = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, Category="Style") FLinearColor InactiveBg   = FLinearColor(0.2f,0.2f,0.2f,1.f);
	UPROPERTY(EditDefaultsOnly, Category="Style") FLinearColor InactiveText = FLinearColor(0.75f,0.75f,0.75f,1.f);

	UPROPERTY(EditDefaultsOnly, Category="Style") float ActiveOpacity      = 1.0f;
	UPROPERTY(EditDefaultsOnly, Category="Style") float ConditionalOpacity = 0.8f;
	UPROPERTY(EditDefaultsOnly, Category="Style") float InactiveOpacity    = 0.4f;

private:
	EKeywordUIState State = EKeywordUIState::Inactive;
};
