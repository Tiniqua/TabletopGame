
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DeploymentWidget.generated.h"

enum class EFaction : uint8;
class UTextBlock;
class UPanelWidget;
class UButton;

UCLASS()
class TABLETOP_API UDeploymentWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void OnStartBattleClicked();

	bool bSetupComplete = false;
	bool bBoundToGameState = false;
	FTimerHandle SetupRetryTimer;

	UFUNCTION() void TickUntilReady();
	bool IsReadyToSetup() const;
	void DoInitialSetup();
	
public:
	UPROPERTY(meta=(BindWidget)) UTextBlock* P1Name = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P2Name = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P1FactionText = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* P2FactionText = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* TurnBanner = nullptr;

	UPROPERTY(meta=(BindWidget)) UPanelWidget* LocalUnitsPanel = nullptr;
	UPROPERTY(meta=(BindWidget)) UPanelWidget* OppUnitsPanel   = nullptr;

	UPROPERTY(meta=(BindWidget)) UButton* StartBattleBtn = nullptr;

	UPROPERTY(EditDefaultsOnly) TSubclassOf<class UDeployRowWidget> DeployRowClass;
	
	
	UFUNCTION() void RefreshFromState();
	UFUNCTION() void OnDeploymentChanged();

	class AMatchGameState* GS() const;
	class AMatchPlayerController* MPC() const;

	void RebuildUnitPanels();
	static FString FactionDisplay(EFaction F);
	
	TWeakObjectPtr<AMatchGameState> BoundGS;
	
};
