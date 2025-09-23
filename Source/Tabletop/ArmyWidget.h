
#pragma once

#include "CoreMinimal.h"
#include "ArmyData.h"
#include "Blueprint/UserWidget.h"
#include "ArmyWidget.generated.h"

class UUniformGridPanel;
class UTextBlock;
class UComboBoxString;
class UButton;

UCLASS()
class TABLETOP_API UArmyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void OnReadyUpChanged();

	
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta=(BindWidget)) UTextBlock* P1Name = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P2Name = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P1PickText = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P2PickText = nullptr;
	UPROPERTY(meta=(BindWidget)) UButton* P1ReadyBtn = nullptr;
	UPROPERTY(meta=(BindWidget)) UButton* P2ReadyBtn = nullptr;
	UPROPERTY(meta=(BindWidget)) UButton* BothReady = nullptr;
	UPROPERTY(meta=(BindWidget)) UUniformGridPanel* FactionGrid = nullptr;

private:
	UFUNCTION()
	void RefreshFromState();
	UFUNCTION()
	void OnFactionChanged(FString Selected, ESelectInfo::Type Type);
	UFUNCTION()
	void OnP1ReadyClicked();
	UFUNCTION()
	void OnP2ReadyClicked();
	UFUNCTION()
	void OnBothReadyClicked();

	TMap<UButton*, EFaction> ButtonToFaction;

	UFUNCTION()
	void HandleFactionTileClicked();

	void BuildFactionGrid();

	class ASetupGameState* GS() const;
	class ASetupPlayerController* PC() const;
};
