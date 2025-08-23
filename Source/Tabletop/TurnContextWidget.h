#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TurnContextWidget.generated.h"

class UWidgetSwitcher;
class UPanelWidget;
class UButton;
class UTextBlock;

class AMatchGameState;
class AMatchPlayerController;
class AUnitBase;

/**
 * Bottom context panel shown during Battle.
 * Shows per-phase controls for the local player, and mirrored preview for the defender.
 * Uses a single, shared set of controls across Shoot/Charge/Fight.
 */
UCLASS()
class TABLETOP_API UTurnContextWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

protected:
    // ---------- Bindings (assign in UMG) ----------
    UPROPERTY(meta=(BindWidget)) UWidgetSwitcher* PhaseSwitcher = nullptr;

    // Pages in the switcher (3 states: Blank, Movement, ActionPhases)
    UPROPERTY(meta=(BindWidget)) UPanelWidget* BlankPanel     = nullptr;
    UPROPERTY(meta=(BindWidget)) UPanelWidget* MovementPanel  = nullptr;
    UPROPERTY(meta=(BindWidget)) UPanelWidget* ActionPanel    = nullptr; // shared for Shoot/Charge/Fight

    // Common attacker UI
    UPROPERTY(meta=(BindWidget)) UTextBlock* AttackerNameText    = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* AttackerMembersText = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* AttackerRangeText   = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* AttackerAttacksText = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* AttackerDamageText  = nullptr;

    // Movement-specific
    UPROPERTY(meta=(BindWidget)) UTextBlock* MoveBudgetText      = nullptr;

    // Common target UI (mirrored preview for enemy)
    UPROPERTY(meta=(BindWidget)) UTextBlock* TargetNameText      = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* TargetMembersText   = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* TargetToughText     = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* TargetWoundsText    = nullptr;

    // Shared action row
    UPROPERTY(meta=(BindWidget)) UButton*    SelectTargetBtn     = nullptr; // enter target mode
    UPROPERTY(meta=(BindWidget)) UButton*    PrimaryActionBtn    = nullptr; // Confirm / Attempt Charge / Fight
    UPROPERTY(meta=(BindWidget)) UTextBlock* PrimaryActionLabel  = nullptr; // label for PrimaryActionBtn
    UPROPERTY(meta=(BindWidget)) UButton*    CancelBtn           = nullptr; // cancel target mode / preview

    // ---------- Callbacks ----------
    UFUNCTION() void OnMatchChanged();
    UFUNCTION() void OnSelectedChanged(AUnitBase* NewSel);

    UFUNCTION() void OnSelectTargetClicked();
    UFUNCTION() void OnPrimaryActionClicked();
    UFUNCTION() void OnCancelClicked();

    // ---------- Helpers ----------
    AMatchGameState* GS() const;
    AMatchPlayerController* MPC() const;

    void Refresh();
    void ShowBlank();
    void ShowMovement(AUnitBase* Sel);
    void ShowActionPhases(AUnitBase* Sel); // handles Shoot/Charge/Fight in one place

    void FillAttacker(AUnitBase* U);
    void FillTarget(AUnitBase* U);
    void ClearTargetFields();

    bool IsMyTurn() const;

private:
    TWeakObjectPtr<AMatchGameState> BoundGS;
    TWeakObjectPtr<AMatchPlayerController> BoundPC;
};
