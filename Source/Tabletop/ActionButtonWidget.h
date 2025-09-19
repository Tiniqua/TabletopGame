#pragma once
#include "CoreMinimal.h"
#include "Actors/UnitAction.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "ActionButtonWidget.generated.h"

class UButton;
class UTextBlock;
class UUnitAction;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActionClicked, class UUnitAction*, Act);

UCLASS()
class TABLETOP_API UActionButtonWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable)
	void Init(class UUnitAction* InAction, FText InLabel, bool bEnabled);

	// NEW:
	UFUNCTION(BlueprintCallable)
	void SetCostPips(int32 Cost, UTexture2D* Active, UTexture2D* Inactive);

	UPROPERTY(meta=(BindWidget))
	class UTextBlock* LabelText = nullptr;
	UPROPERTY(meta=(BindWidgetOptional))
	class UPanelWidget* CostPipsBox = nullptr;

	UPROPERTY()
	class UUnitAction* Action = nullptr;
	UPROPERTY()
	class AUnitBase* Owner = nullptr;
	UPROPERTY(BlueprintAssignable)
	FOnActionClicked OnActionClicked;

protected:
	UPROPERTY(meta=(BindWidget)) class UButton* Button = nullptr;

	virtual void NativeConstruct() override;
	UFUNCTION()
	void HandleClicked();
};