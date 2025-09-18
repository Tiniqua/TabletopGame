// WeaponLoadoutRowWidget.h (add optional WL_PointsText if you have it in UMG)
#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ArmyData.h"
#include "WeaponLoadoutRowWidget.generated.h"

class UTextBlock;
class UButton;

UCLASS()
class TABLETOP_API UWeaponLoadoutRowWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void Init(FName InUnitId, int32 InWeaponIndex, const FWeaponProfile& W);
	void RefreshFromState();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta=(BindWidget)) UTextBlock* WL_NameText      = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* WL_StatsText     = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* WL_KeywordsText  = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* WL_AbilitiesText = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* WL_CountText     = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* WL_PointsText    = nullptr; // optional if present in BP

	UPROPERTY(meta=(BindWidget)) UButton*    WL_MinusBtn      = nullptr;
	UPROPERTY(meta=(BindWidget)) UButton*    WL_PlusBtn       = nullptr;

private:
	FName         UnitId = NAME_None;
	int32         WeaponIndex = 0;
	FWeaponProfile CachedWeapon;

	UFUNCTION() void HandleMinus();
	UFUNCTION() void HandlePlus();
	UFUNCTION() void HandleRosterChanged();

	class ASetupGameState* GS() const;
	class ASetupPlayerController* PC() const;

	int32 GetLocalSeatCount() const;
	void  SendCountToServer(int32 NewCount) const;

	static FString FormatWeaponStats(const FWeaponProfile& W);
};
