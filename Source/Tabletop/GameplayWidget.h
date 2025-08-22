
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayWidget.generated.h"

class UTextBlock;
class UButton;
class AMatchGameState;
class AMatchPlayerController;

UCLASS()
class TABLETOP_API UGameplayWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION() void OnMatchChanged();
	UFUNCTION() void OnNextClicked();

	AMatchGameState* GS() const;
	AMatchPlayerController* MPC() const;

	void RefreshTopBar();
	void RefreshBottom();

public:
	// Top-left (P1)
	UPROPERTY(meta=(BindWidget)) UTextBlock* P1Name = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P1Faction = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P1Score = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P1PhaseTag = nullptr;

	// Top-right (P2)
	UPROPERTY(meta=(BindWidget)) UTextBlock* P2Name = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P2Faction = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P2Score = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P2PhaseTag = nullptr;

	// Center
	UPROPERTY(meta=(BindWidget)) UTextBlock* RoundLabel = nullptr;

	// Bottom action
	UPROPERTY(meta=(BindWidget)) UButton* NextBtn = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* NextBtnLabel = nullptr;

private:
	TWeakObjectPtr<AMatchGameState> BoundGS;
};
