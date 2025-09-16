#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "ActionButtonWidget.generated.h"

class UButton;
class UTextBlock;
class UUnitAction;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActionButtonClicked, UUnitAction*, Action);

UCLASS()
class TABLETOP_API UActionButtonWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	// Assign in UMG or create at runtime
	UPROPERTY(meta=(BindWidget)) UButton*    Button = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* Label  = nullptr;

	// What this row represents
	UPROPERTY() UUnitAction* Action = nullptr;

	// Parent binds to this to learn which action was clicked
	UPROPERTY(BlueprintAssignable) FOnActionButtonClicked OnActionClicked;

	// Call after CreateWidget
	UFUNCTION(BlueprintCallable)
	void Init(UUnitAction* InAction, const FText& InLabel, bool bEnabled)
	{
		Action = InAction;
		if (Label) Label->SetText(InLabel);
		if (Button) Button->SetIsEnabled(bEnabled);
	}

	virtual void NativeConstruct() override
	{
		Super::NativeConstruct();
		if (Button)
			Button->OnClicked.AddDynamic(this, &UActionButtonWidget::HandleClicked);
	}

private:
	UFUNCTION() void HandleClicked()
	{
		OnActionClicked.Broadcast(Action);
	}
};
