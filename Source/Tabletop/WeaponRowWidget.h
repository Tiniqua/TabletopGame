#pragma once
#include "ArmyData.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

#include "WeaponRowWidget.generated.h"

UCLASS()
class TABLETOP_API UWeaponRowWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(meta=(BindWidget)) UButton*    ChooseBtn = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* NameText  = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* StatsText = nullptr;
    
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnChoose, int32 /*WeaponIndex*/);
	FOnChoose OnChoose;

	int32 Index = 0;

	UFUNCTION(BlueprintCallable)
	void Init(const FWeaponProfile& W, int32 InIndex)
	{
		Index = InIndex;
		if (NameText)  NameText->SetText(FText::FromName(W.WeaponId));
		if (StatsText) StatsText->SetText(FText::FromString(
			FString::Printf(TEXT("Rng %d\"  A %d  S %d  AP %d  D %d"),
				W.RangeInches, W.Attacks, W.Strength, W.AP, W.Damage)));
		if (ChooseBtn)
			ChooseBtn->OnClicked.AddDynamic(this, &UWeaponRowWidget::HandleChoose);
	}
private:
	UFUNCTION() void HandleChoose() { OnChoose.Broadcast(Index); }
};
