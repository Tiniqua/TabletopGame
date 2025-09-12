#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Gamemodes/MatchGameMode.h"
#include "MatchSummaryWidget.generated.h"

class UTextBlock;
class UButton;
class UPanelWidget;

// MatchSummaryWidget.h
UCLASS()
class TABLETOP_API UMatchSummaryWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable) void RefreshFromState(class AMatchGameState* GS);

	UPROPERTY(meta=(BindWidget)) class UTextBlock* P1ScoreText = nullptr;
	UPROPERTY(meta=(BindWidget)) class UTextBlock* P2ScoreText = nullptr;
	UPROPERTY(meta=(BindWidget)) class UTextBlock* RoundsText  = nullptr;

	// Replaces the list views:
	UPROPERTY(meta=(BindWidget)) class UTextBlock* P1SurvivorsText = nullptr;
	UPROPERTY(meta=(BindWidget)) class UTextBlock* P2SurvivorsText = nullptr;

	UPROPERTY(meta=(BindWidget)) class UButton* BtnReturnToMenu = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Summary|Return")
	TSoftObjectPtr<UWorld> MainMenuMapAsset;

	// Optional: if you want to drop a level reference directly while testing PIE
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Summary|Return")
	ULevel* MainMenuLevelOverride = nullptr;

	// Fallback if no asset is provided (should be a long package path)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Summary|Return")
	FString MainMenuMapPath = TEXT("/Game/Maps/MainMenu");

protected:
	virtual void NativeConstruct() override;

private:
	static FString MakeSurvivorLines(const struct FMatchSummary& S, int32 TeamNum);

	UFUNCTION() void OnReturnToMenuClicked();

	void LeaveOrDestroySessionThenReturn();
	void ReturnToMainMenu();
	FString ResolveReturnMapPath() const;

	// Session delegates
	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);
};
