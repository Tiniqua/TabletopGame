#pragma once

#include "CoreMinimal.h"
#include "ActionButtonWidget.h"
#include "Blueprint/UserWidget.h"
#include "TurnContextWidget.generated.h"

struct FKeywordUIInfo;
enum class ECoverType : uint8;

class UPanelWidget;
class UButton;
class UTextBlock;

class AMatchGameState;
class AMatchPlayerController;
class AUnitBase;

UCLASS()
class TABLETOP_API UTurnContextWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    UPROPERTY(EditDefaultsOnly, Category="UI")
    TSubclassOf<UActionButtonWidget> ActionButtonClass = UActionButtonWidget::StaticClass();

protected:
    // ---------- Bindings (assign in UMG) ----------
    // Attacker (always shown if a unit is selected)
    UPROPERTY(meta=(BindWidget)) UTextBlock* AttackerNameText    = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* AttackerMembersText = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* AttackerRangeText   = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* AttackerAttacksText = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* AttackerDamageText  = nullptr;

    UPROPERTY(meta=(BindWidgetOptional)) UTextBlock* WeaponNameText       = nullptr;
    UPROPERTY(meta=(BindWidgetOptional)) UTextBlock* WeaponStatsText      = nullptr;
    UPROPERTY(meta=(BindWidgetOptional)) UTextBlock* WeaponKeywordsText   = nullptr;
    UPROPERTY(meta=(BindWidgetOptional)) UTextBlock* WeaponAbilitiesText  = nullptr;
    UPROPERTY(meta=(BindWidgetOptional)) UTextBlock* UnitAbilitiesText    = nullptr;

    UPROPERTY(meta=(BindWidgetOptional)) UTextBlock*  APText       = nullptr;
    UPROPERTY(meta=(BindWidgetOptional)) UPanelWidget* ActionsPanel = nullptr;

    UPROPERTY(meta=(BindWidgetOptional)) UPanelWidget* Panel_Attacker = nullptr;
    UPROPERTY(meta=(BindWidgetOptional)) UPanelWidget* Panel_Target   = nullptr;
    UPROPERTY(meta=(BindWidgetOptional)) UPanelWidget* Panel_Movement = nullptr;

    // AP
    UPROPERTY(meta=(BindWidgetOptional)) UPanelWidget*   APBar = nullptr;
    UPROPERTY(EditDefaultsOnly, Category="UI|AP") int32  MaxAPIcons = 4; // “out of 4”
    UPROPERTY(EditDefaultsOnly, Category="UI|AP") UTexture2D* AP_Pip_Active = nullptr;
    UPROPERTY(EditDefaultsOnly, Category="UI|AP") UTexture2D* AP_Pip_Inactive = nullptr;

    // Movement-specific
    UPROPERTY(meta=(BindWidget)) UTextBlock* MoveBudgetText = nullptr;

    // NEW: bind the label you have in UMG
    UPROPERTY(meta=(BindWidgetOptional)) UTextBlock* MoveLabel = nullptr;

    // Target + preview block
    UPROPERTY(meta=(BindWidget)) UTextBlock* TargetNameText     = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* TargetMembersText  = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* TargetToughText    = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* TargetWoundsText   = nullptr;

    UPROPERTY(meta=(BindWidget)) UTextBlock* HitChanceText    = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* WoundChanceText  = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* SaveFailText     = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* EstDamageText    = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* CoverStatusText  = nullptr;

    UPROPERTY(meta=(BindWidget)) class UPanelWidget* KeywordPanel = nullptr;
    UPROPERTY(EditDefaultsOnly)  TSubclassOf<class UKeywordChipWidget> KeywordChipClass;

    // ---------- Callbacks ----------
    UFUNCTION() void OnMatchChanged();
    UFUNCTION() void OnSelectedChanged(class AUnitBase* NewSel);

    // ---------- Helpers ----------
    AMatchGameState* GS() const;
    AMatchPlayerController* MPC() const;

    void Refresh();
    void UpdateActionPoints();
    void RebuildActionButtons(AUnitBase* Sel);

    void FillAttacker(AUnitBase* U);
    void FillWeaponLoadout(AUnitBase* U);
    void FillTarget(AUnitBase* U);
    void ClearTargetFields();

    UFUNCTION() void HandleSelectedUnitMoveChanged();  // delegate target
    void UpdateMovementUI(class AUnitBase* Sel);

    // track which unit we’re listening to
    TWeakObjectPtr<class AUnitBase> BoundSel;

    void UpdateCombatEstimates(AUnitBase* Attacker, AUnitBase* Target,
                               int32 HitMod, int32 SaveMod, ECoverType CoverType);
    void RebuildKeywordChips(const TArray<FKeywordUIInfo>& Infos);
    void ClearEstimateFields();

    bool IsMyTurn() const;
    
    UFUNCTION()
    void HandleDynamicActionClicked(class UUnitAction* Act);

private:
    TWeakObjectPtr<class AMatchGameState> BoundGS;
    TWeakObjectPtr<class AMatchPlayerController> BoundPC;
};
