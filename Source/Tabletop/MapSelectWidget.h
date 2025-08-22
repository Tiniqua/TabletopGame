
#pragma once

#include "CoreMinimal.h"
#include "ArmyData.h"
#include "Blueprint/UserWidget.h"

#include "Components/ComboBoxString.h"
#include "Types/SlateEnums.h"

#include "MapSelectWidget.generated.h"

class UTextBlock;
class UComboBoxString;
class UImage;
class UButton;
class UPanelWidget;

UCLASS()
class TABLETOP_API UMapSelectWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta=(BindWidget)) UTextBlock* P1Name = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P2Name = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P1FactionText = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P2FactionText = nullptr;
	UPROPERTY(meta=(BindWidget)) UPanelWidget* P1RosterList = nullptr;
	UPROPERTY(meta=(BindWidget)) UPanelWidget* P2RosterList = nullptr;
	UPROPERTY(meta=(BindWidget)) UComboBoxString* MapDropdown = nullptr;
	UPROPERTY(meta=(BindWidget)) UImage* MapPreview = nullptr;
	UPROPERTY(meta=(BindWidget)) UButton* P1ReadyBtn = nullptr;
	UPROPERTY(meta=(BindWidget)) UButton* P2ReadyBtn = nullptr;
	UPROPERTY(meta=(BindWidget)) UButton* BothReady = nullptr;

	// Map RowName lookup by display string
	TMap<FString, FName> DisplayToRow;

private:
	UFUNCTION() void RefreshFromState();
	UFUNCTION() void OnPhaseChanged();
	UFUNCTION() void OnReadyUpChanged();
	UFUNCTION() void OnRosterChanged();
	UFUNCTION() void OnArmyChanged();
	UFUNCTION() void OnMapChanged();

	UFUNCTION()
	void OnMapDropdownChanged(FString Selected, ESelectInfo::Type SelectionType);



	UFUNCTION() void OnP1ReadyClicked();
	UFUNCTION() void OnP2ReadyClicked();
	UFUNCTION() void OnBothReadyClicked();

	class ASetupGameState* GS() const;
	class ASetupPlayerController* PC() const;

	void BuildMapDropdown();          // host builds options
	void RebuildRosterPanels();       // list both rosters
	static FString FactionDisplay(EFaction F);
};
