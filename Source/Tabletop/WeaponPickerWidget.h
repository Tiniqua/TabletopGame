#pragma once
#include "WeaponRowWidget.h"
#include "Blueprint/UserWidget.h"

#include "WeaponPickerWidget.generated.h"

UCLASS()
class TABLETOP_API UWeaponPickerWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(meta=(BindWidget)) UPanelWidget* ListPanel = nullptr;
	UPROPERTY(EditDefaultsOnly) TSubclassOf<UWeaponRowWidget> RowClass;

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPick, FName /*UnitId*/, int32 /*WeaponIndex*/);
	FOnPick OnPick;

	UFUNCTION(BlueprintCallable)
	void Init(FName UnitId, const TArray<FWeaponProfile>& Weapons)
	{
		PendingUnitId = UnitId;
		if (!ListPanel) return;
		ListPanel->ClearChildren();

		for (int32 i=0;i<Weapons.Num();++i)
		{
			if (UWeaponRowWidget* R = CreateWidget<UWeaponRowWidget>(GetOwningPlayer(), RowClass))
			{
				R->Init(Weapons[i], i);
				R->OnChoose.AddLambda([this](int32 Idx){ OnPick.Broadcast(PendingUnitId, Idx); RemoveFromParent(); });
				ListPanel->AddChild(R);
			}
		}
	}
private:
	FName PendingUnitId = NAME_None;
};