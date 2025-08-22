
#pragma once

#include "CoreMinimal.h"
#include "ArmyData.h"
#include "Blueprint/UserWidget.h"
#include "UnitSelectionWidget.generated.h"

class UTextBlock;
class UButton;
class UPanelWidget;
class UEditableTextBox;

UCLASS()
class TABLETOP_API UUnitSelectionWidget : public UUserWidget
{
	GENERATED_BODY()

	EFaction CachedLocalFaction = EFaction::None;

	UFUNCTION()
	void OnArmySelectionChanged();

	UFUNCTION()
	void MaybeBuildRows(); 
	
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// BindWidget fields
	UPROPERTY(meta=(BindWidget)) UTextBlock* P1Name = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P2Name = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P1PointsText = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P2PointsText = nullptr;
	UPROPERTY(meta=(BindWidget)) UButton* P1ReadyBtn = nullptr;
	UPROPERTY(meta=(BindWidget)) UButton* P2ReadyBtn = nullptr;
	UPROPERTY(meta=(BindWidget)) UButton* BothReady = nullptr;
	UPROPERTY(meta=(BindWidget)) UPanelWidget* UnitsList = nullptr;

	// Row widget class (assign in BP)
	UPROPERTY(EditDefaultsOnly, Category="UI")
	TSubclassOf<UUserWidget> UnitRowEntryClass;

private:
	UFUNCTION() void RefreshFromState();
	UFUNCTION() void OnReadyUpChanged();
	UFUNCTION() void OnRosterChanged();
	UFUNCTION() void OnPhaseChanged();

	UFUNCTION() void OnP1ReadyClicked();
	UFUNCTION() void OnP2ReadyClicked();
	UFUNCTION() void OnBothReadyClicked();
	void BuildUnitRows();

	class ASetupGameState* GS() const;
	class ASetupPlayerController* PC() const;
	

};
