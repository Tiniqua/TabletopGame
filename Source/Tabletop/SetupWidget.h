
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SetupWidget.generated.h"

class UWidgetSwitcher;

UCLASS()
class TABLETOP_API USetupWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	
	UFUNCTION(BlueprintCallable)
	void GoToNextStep();

	UFUNCTION(BlueprintCallable)
	void SetStepIndex(int32 Index);
	
	UPROPERTY(meta = (BindWidget))
	UWidgetSwitcher* ContentSwitcher;

private:
	UFUNCTION()
	void HandlePhaseChanged();
};
