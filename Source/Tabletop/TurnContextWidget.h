#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TurnContextWidget.generated.h"

struct FKeywordUIInfo;
enum class ECoverType : uint8;
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

    // Pages in the switcher (3 states: Blank, Movement, Shoot)
    UPROPERTY(meta=(BindWidget)) UPanelWidget* BlankPanel     = nullptr;
    UPROPERTY(meta=(BindWidget)) UPanelWidget* MovementPanel  = nullptr;
    UPROPERTY(meta=(BindWidget)) UPanelWidget* ActionPanel    = nullptr; // reused for Shoot

    // Common attacker UI
    UPROPERTY(meta=(BindWidget)) UTextBlock* AttackerNameText    = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* AttackerMembersText = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* AttackerRangeText   = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* AttackerAttacksText = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* AttackerDamageText  = nullptr;

    // Movement-specific
    UPROPERTY(meta=(BindWidget)) UTextBlock* MoveBudgetText      = nullptr;

    // Common target UI
    UPROPERTY(meta=(BindWidget)) UTextBlock* TargetNameText      = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* TargetMembersText   = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* TargetToughText     = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* TargetWoundsText    = nullptr;

    // Shared action row (Shoot)
    UPROPERTY(meta=(BindWidget)) UButton*    SelectTargetBtn     = nullptr; // enter target mode
    UPROPERTY(meta=(BindWidget)) UButton*    PrimaryActionBtn    = nullptr; // Confirm
    UPROPERTY(meta=(BindWidget)) UTextBlock* PrimaryActionLabel  = nullptr; // "Confirm"
    UPROPERTY(meta=(BindWidget)) UButton*    CancelBtn           = nullptr; // cancel target mode / preview

    UPROPERTY(meta=(BindWidget)) UTextBlock* HitChanceText     = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* WoundChanceText   = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* SaveFailText      = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* EstDamageText     = nullptr;
    UPROPERTY(meta=(BindWidget)) UTextBlock* CoverStatusText = nullptr;

    // ---------- Callbacks ----------
    UFUNCTION() void OnMatchChanged();
    UFUNCTION() void OnSelectedChanged(class AUnitBase* NewSel);

    UFUNCTION() void OnSelectTargetClicked();
    UFUNCTION() void OnPrimaryActionClicked();
    UFUNCTION() void OnCancelClicked();

    // ---------- Helpers ----------
    class AMatchGameState* GS() const;
    class AMatchPlayerController* MPC() const;

    void Refresh();
    void ShowBlank();
    void ShowMovement(class AUnitBase* Sel);
    void ShowShootPhase(class AUnitBase* Sel);   // <— renamed, only Shoot now

    void FillAttacker(class AUnitBase* U);
    void FillTarget(class AUnitBase* U);
    void ClearTargetFields();

    void UpdateCombatEstimates(class AUnitBase* Attacker, class AUnitBase* Target);

    void UpdateCombatEstimates(class AUnitBase* Attacker, class AUnitBase* Target,
                           int32 HitMod, int32 SaveMod, ECoverType CoverType);

    UPROPERTY(meta=(BindWidget)) class UPanelWidget* KeywordPanel = nullptr;
    UPROPERTY(EditDefaultsOnly)  TSubclassOf<class UKeywordChipWidget> KeywordChipClass;

    void RebuildKeywordChips(const TArray<FKeywordUIInfo>& Infos);

    // Clear those fields (when no target / wrong phase)
    void ClearEstimateFields();

    bool IsMyTurn() const;

private:
    TWeakObjectPtr<class AMatchGameState> BoundGS;
    TWeakObjectPtr<class AMatchPlayerController> BoundPC;
};