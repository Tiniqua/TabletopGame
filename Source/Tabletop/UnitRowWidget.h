
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UnitRowWidget.generated.h"

class UTextBlock;
class UButton;

UCLASS()
class TABLETOP_API UUnitRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Init from parent after CreateWidget
	void Init(FName InUnitId, const FText& InDisplayName, int32 InUnitPoints);

	UFUNCTION(BlueprintCallable)
	void OnWeaponChanged(FString Selected, ESelectInfo::Type SelectionType);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// BindWidget: create these in your Row BP (NameText, PointsText, CountText, MinusBtn, PlusBtn)
	UPROPERTY(meta=(BindWidget)) UTextBlock* NameText   = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* PointsText = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* CountText  = nullptr;
	UPROPERTY(meta=(BindWidget)) UButton*    MinusBtn   = nullptr;
	UPROPERTY(meta=(BindWidget)) UButton*    PlusBtn    = nullptr;

	UPROPERTY(meta=(BindWidget))
	class UComboBoxString* WeaponCombo;

	int32 CurrentWeaponIdx = 0;
	TArray<FName> WeaponDisplay;

private:
	// data
	FName  UnitId = NAME_None;
	int32  UnitPoints = 0;

	// callbacks
	UFUNCTION() void HandleMinus();
	UFUNCTION() void HandlePlus();
	UFUNCTION() void HandleRosterChanged();

	// helpers
	class ASetupGameState* GS() const;
	class ASetupPlayerController* PC() const;
	int32 GetLocalSeatCount() const;        // read current count from GS (local seat)
	void  ApplyCountToUI(int32 Count) const;// update CountText
	void  SendCountToServer(int32 NewCount) const;
};
