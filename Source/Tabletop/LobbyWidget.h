
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LobbyWidget.generated.h"

class UButton;
class UEditableTextBox;
UCLASS()
class TABLETOP_API ULobbyWidget : public UUserWidget
{
	GENERATED_BODY()
	
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// --- BindWidget: MUST match your UMG names ---
	UPROPERTY(meta=(BindWidget)) UButton* P1ReadyBtn = nullptr;
	UPROPERTY(meta=(BindWidget)) UButton* P2ReadyBtn = nullptr;
	UPROPERTY(meta=(BindWidget)) UEditableTextBox* P1Name = nullptr;
	UPROPERTY(meta=(BindWidget)) UEditableTextBox* P2Name = nullptr;
	UPROPERTY(meta=(BindWidget)) UButton* BothReady = nullptr;
	
	UPROPERTY(meta=(BindWidgetOptional)) UEditableTextBox* LobbyStatusBox = nullptr;

private:
	UFUNCTION() void OnP1ReadyClicked();
	UFUNCTION() void OnP2ReadyClicked();
	UFUNCTION() void OnBothReadyClicked();
	
	class ASetupGameState* GetSetupGS() const;
	class ASetupPlayerController* GetSetupPC() const;

	FTimerHandle LobbyStatusRefreshHandle;

	void UpdateLobbyStatusSummary();
	void SetLobbyStatus(const FString& Text);
	void AppendLobbyStatusLine(const FString& Line);
	FString CurrentWorldPackage() const;
	
	UFUNCTION()
	void RefreshFromState();      // updates all texts / enables
	void ApplySeatPermissions();  // who can click what

	// cache to avoid spamming SetText every tick
	FString CachedP1Name, CachedP2Name;
	bool bCachedP1Ready = false;
	bool bCachedP2Ready = false;
	
};
