#include "TurnContextWidget.h"

#include "Components/WidgetSwitcher.h"
#include "Components/PanelWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Actors/CoverVolume.h"
#include "Controllers/MatchPlayerController.h"
#include "Actors/UnitBase.h"
#include "Gamemodes/MatchGameMode.h"   // EMatchPhase, ETurnPhase, AMatchGameState

namespace
{
    inline float ProbAtLeast(int32 Need) {
        if (Need <= 1) return 1.f;
        if (Need >= 7) return 0.f;
        return float(7 - Need) / 6.f;
    }
    inline int32 ToWoundTarget(int32 S, int32 T) {
        if (S >= 2*T) return 2; if (S > T) return 3; if (S == T) return 4;
        if (2*S <= T) return 6; return 5;
    }
    inline int32 ModifiedSaveNeed(int32 BaseSave, int32 AP) {
        return FMath::Clamp(BaseSave + FMath::Max(0, AP), 2, 7);
    }
    inline const TCHAR* CoverTypeToText(ECoverType C) {
        switch (C) { case ECoverType::Low: return TEXT("in cover (low)");
        case ECoverType::High:return TEXT("in cover (high)");
        default:               return TEXT("no cover"); }
    }
}


AMatchGameState* UTurnContextWidget::GS() const
{
    return GetWorld() ? GetWorld()->GetGameState<AMatchGameState>() : nullptr;
}
AMatchPlayerController* UTurnContextWidget::MPC() const
{
    return GetOwningPlayer<AMatchPlayerController>();
}

void UTurnContextWidget::ClearEstimateFields()
{
    if (HitChanceText)   HitChanceText->SetText(FText::GetEmpty());
    if (WoundChanceText) WoundChanceText->SetText(FText::GetEmpty());
    if (SaveFailText)    SaveFailText->SetText(FText::GetEmpty());
    if (EstDamageText)   EstDamageText->SetText(FText::GetEmpty());
    if (CoverStatusText) CoverStatusText->SetText(FText::GetEmpty());
}

void UTurnContextWidget::UpdateCombatEstimates(AUnitBase* Attacker, AUnitBase* Target,
                                               int32 HitMod, int32 SaveMod, ECoverType CoverType)
{
    if (!Attacker || !Target) { ClearEstimateFields(); return; }

    const int32 models    = FMath::Max(0, Attacker->ModelsCurrent);
    const int32 atkPM     = FMath::Max(0, Attacker->GetAttacks());
    const int32 totalAtk  = models * atkPM;

    const int32 dmg       = FMath::Max(1, Attacker->GetDamage());            // or WeaponDamageRep
    const int32 sVal      = Attacker->WeaponStrengthRep;                         // or WeaponStrengthRep
    const int32 tVal      = Target->GetToughness();
    const int32 ap        = FMath::Max(0, Attacker->WeaponAPRep);                // or WeaponAPRep
    const int32 baseToHit = FMath::Clamp(Attacker->WeaponSkillToHitRep, 2, 6);        // or WeaponSkillToHitRep
    const int32 baseSave  = Target->GetSave();

    // Same sign convention as your server code:
    // High cover sets HitMod=-1 → need increases by +1 (i.e., +(-HitMod))
    const int32 hitNeed  = FMath::Clamp(baseToHit + (HitMod * -1), 2, 6);
    const int32 woundNeed= ToWoundTarget(sVal, tVal);
    int32 saveNeed       = ModifiedSaveNeed(baseSave, ap);
    saveNeed             = FMath::Clamp(saveNeed - SaveMod, 2, 7);

    const float pHit   = ProbAtLeast(hitNeed);
    const float pWound = ProbAtLeast(woundNeed);
    const float pSave  = (saveNeed <= 6) ? ProbAtLeast(saveNeed) : 0.f;
    const float pFail  = 1.f - pSave;

    const float pUnsavedPerAtk = pHit * pWound * pFail;
    const float evDamage       = float(totalAtk) * pUnsavedPerAtk * float(dmg);

    if (HitChanceText)
        HitChanceText->SetText(FText::FromString(
            FString::Printf(TEXT("Hit: %d%% (need %d+)"), FMath::RoundToInt(pHit*100.f), hitNeed)));
    if (WoundChanceText)
        WoundChanceText->SetText(FText::FromString(
            FString::Printf(TEXT("Wound: %d%% (need %d+)"), FMath::RoundToInt(pWound*100.f), woundNeed)));
    if (SaveFailText)
    {
        if (saveNeed <= 6)
            SaveFailText->SetText(FText::FromString(
                FString::Printf(TEXT("Fail Save: %d%% (save %d+)"), FMath::RoundToInt(pFail*100.f), saveNeed)));
        else
            SaveFailText->SetText(FText::FromString(TEXT("Fail Save: 100% (no save)")));
    }
    if (EstDamageText)
        EstDamageText->SetText(FText::FromString(
            FString::Printf(TEXT("Est. Dmg: %.1f"), evDamage)));

    if (CoverStatusText)
        CoverStatusText->SetText(FText::FromString(CoverTypeToText(CoverType)));
}

void UTurnContextWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (AMatchGameState* S = GS())
    {
        S->OnDeploymentChanged.AddDynamic(this, &UTurnContextWidget::OnMatchChanged);
        BoundGS = S;
    }
    if (AMatchPlayerController* P = MPC())
    {
        P->OnSelectedChanged.AddDynamic(this, &UTurnContextWidget::OnSelectedChanged);
        BoundPC = P;
    }

    if (SelectTargetBtn)  SelectTargetBtn->OnClicked.AddDynamic(this, &UTurnContextWidget::OnSelectTargetClicked);
    if (PrimaryActionBtn) PrimaryActionBtn->OnClicked.AddDynamic(this, &UTurnContextWidget::OnPrimaryActionClicked);
    if (CancelBtn)        CancelBtn->OnClicked.AddDynamic(this, &UTurnContextWidget::OnCancelClicked);

    Refresh();
}

void UTurnContextWidget::NativeDestruct()
{
    if (BoundGS.IsValid())
    {
        BoundGS->OnDeploymentChanged.RemoveDynamic(this, &UTurnContextWidget::OnMatchChanged);
        BoundGS.Reset();
    }
    if (BoundPC.IsValid())
    {
        BoundPC->OnSelectedChanged.RemoveDynamic(this, &UTurnContextWidget::OnSelectedChanged);
        BoundPC.Reset();
    }
    Super::NativeDestruct();
}

void UTurnContextWidget::OnMatchChanged()
{
    Refresh();
}
void UTurnContextWidget::OnSelectedChanged(AUnitBase* /*NewSel*/)
{
    Refresh();
}

bool UTurnContextWidget::IsMyTurn() const
{
    if (const AMatchGameState* S = GS())
    if (const APlayerController* P = GetOwningPlayer())
        return (S->Phase == EMatchPhase::Battle) && (S->CurrentTurn == P->PlayerState);
    return false;
}

void UTurnContextWidget::Refresh()
{
    AMatchGameState* S = GS();
    AMatchPlayerController* P = MPC();
    if (!S || !P || !PhaseSwitcher) return;

    if (S->Phase != EMatchPhase::Battle)
    {
        ShowBlank();
        return;
    }

    AUnitBase* Sel = P->SelectedUnit;

    switch (S->TurnPhase)
    {
    case ETurnPhase::Move:  ShowMovement(Sel);   break;
    case ETurnPhase::Shoot: ShowShootPhase(Sel); break;
    default:                ShowBlank();         break;
    }
}

void UTurnContextWidget::ShowShootPhase(AUnitBase* Sel)
{
    if (PhaseSwitcher) PhaseSwitcher->SetActiveWidget(ActionPanel);

    AMatchGameState* S = GS();
    AMatchPlayerController* P = MPC();
    if (!S || !P) return;

    const bool bMyTurn = IsMyTurn();
    const bool bMine   = bMyTurn && Sel && Sel->OwningPS == P->PlayerState && Sel->ModelsCurrent > 0;

    FillAttacker(Sel);
    ClearTargetFields();
    ClearEstimateFields(); // <— clear by default

    auto SetPrimary = [&](const TCHAR* Label, bool bEnable)
    {
        if (PrimaryActionLabel) PrimaryActionLabel->SetText(FText::FromString(Label));
        if (PrimaryActionBtn)   PrimaryActionBtn->SetIsEnabled(bEnable);
    };

    if (SelectTargetBtn)  SelectTargetBtn->SetIsEnabled(false);
    if (CancelBtn)        CancelBtn->SetIsEnabled(false);
    SetPrimary(TEXT(""), false);

    const bool bPreviewForMe =
        (S->TurnPhase == ETurnPhase::Shoot) &&
        (S->Preview.Attacker == Sel) &&
        (S->Preview.Target   != nullptr);

    if (bPreviewForMe)
    {
        FillTarget(S->Preview.Target);

        const int32    HitMod = S->ActionPreview.HitMod;
        const int32    SaveMod = S->ActionPreview.SaveMod;
        const ECoverType Cover = S->ActionPreview.Cover;

        UpdateCombatEstimates(Sel, S->Preview.Target, HitMod, SaveMod, Cover);
    }
    else
    {
        ClearEstimateFields();
    }

    const bool bCanShoot = bMine && Sel && !Sel->bHasShot;
    if (SelectTargetBtn) SelectTargetBtn->SetIsEnabled(bCanShoot && !bPreviewForMe);
    SetPrimary(TEXT("Confirm"), bCanShoot && bPreviewForMe);
    if (CancelBtn)       CancelBtn->SetIsEnabled(bMine && (P->bTargetMode || bPreviewForMe));
}



void UTurnContextWidget::ShowBlank()
{
    if (PhaseSwitcher) PhaseSwitcher->SetActiveWidget(BlankPanel);

    // disable buttons
    if (SelectTargetBtn)  SelectTargetBtn->SetIsEnabled(false);
    if (PrimaryActionBtn) PrimaryActionBtn->SetIsEnabled(false);
    if (CancelBtn)        CancelBtn->SetIsEnabled(false);

    // clear texts
    if (PrimaryActionLabel) PrimaryActionLabel->SetText(FText::GetEmpty());

    if (AttackerNameText)    AttackerNameText->SetText(FText::GetEmpty());
    if (AttackerMembersText) AttackerMembersText->SetText(FText::GetEmpty());
    if (AttackerRangeText)   AttackerRangeText->SetText(FText::GetEmpty());
    if (AttackerAttacksText) AttackerAttacksText->SetText(FText::GetEmpty());
    if (AttackerDamageText)  AttackerDamageText->SetText(FText::GetEmpty());

    if (MoveBudgetText)      MoveBudgetText->SetText(FText::GetEmpty());

    ClearTargetFields();
    ClearEstimateFields();
}

void UTurnContextWidget::ShowMovement(AUnitBase* Sel)
{
    if (PhaseSwitcher) PhaseSwitcher->SetActiveWidget(MovementPanel);

    const bool bEnable = IsMyTurn() && Sel && Sel->OwningPS == GetOwningPlayer()->PlayerState && Sel->ModelsCurrent > 0;

    FillAttacker(Sel);
    ClearTargetFields();
    ClearEstimateFields();

    // Movement HUD
    if (MoveBudgetText)
    {
        const int32 Budget = Sel ? FMath::Max(0, (int32)FMath::RoundToInt(Sel->MoveBudgetInches)) : 0;
        MoveBudgetText->SetText(FText::FromString(FString::Printf(TEXT("Move left: %d\""), Budget)));
    }

    // Buttons: none for movement
    if (SelectTargetBtn)  SelectTargetBtn->SetIsEnabled(false);
    if (PrimaryActionBtn) PrimaryActionBtn->SetIsEnabled(false);
    if (CancelBtn)        CancelBtn->SetIsEnabled(false);

    // Optional: grey panel in BP when !bEnable or no budget
    (void)bEnable;
}



void UTurnContextWidget::FillAttacker(AUnitBase* U)
{
    if (!U)
    {
        if (AttackerNameText)    AttackerNameText->SetText(FText::GetEmpty());
        if (AttackerMembersText) AttackerMembersText->SetText(FText::GetEmpty());
        if (AttackerRangeText)   AttackerRangeText->SetText(FText::GetEmpty());
        if (AttackerAttacksText) AttackerAttacksText->SetText(FText::GetEmpty());
        if (AttackerDamageText)  AttackerDamageText->SetText(FText::GetEmpty());
        return;
    }

    if (AttackerNameText)
        AttackerNameText->SetText(U->UnitName);

    if (AttackerMembersText)
        AttackerMembersText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d models"), U->ModelsCurrent, U->ModelsMax)));

    if (AttackerRangeText)
        AttackerRangeText->SetText(FText::FromString(FString::Printf(TEXT("Range: %d\""), (int32)U->GetWeaponRange())));

    if (AttackerAttacksText)
        AttackerAttacksText->SetText(FText::FromString(FString::Printf(TEXT("Attacks: %d"), U->GetAttacks() * FMath::Max(0, U->ModelsCurrent))));

    if (AttackerDamageText)
        AttackerDamageText->SetText(FText::FromString(FString::Printf(TEXT("Damage: %d"), U->GetDamage())));
}

void UTurnContextWidget::FillTarget(AUnitBase* U)
{
    if (!U) { ClearTargetFields(); return; }

    if (TargetNameText)    TargetNameText->SetText(U->UnitName);
    if (TargetMembersText) TargetMembersText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d models"), U->ModelsCurrent, U->ModelsMax)));
    if (TargetToughText)   TargetToughText->SetText(FText::FromString(FString::Printf(TEXT("T: %d"), U->GetToughness())));
    if (TargetWoundsText)  TargetWoundsText->SetText(FText::FromString(FString::Printf(TEXT("W: %d"), U->GetWounds())));
}

void UTurnContextWidget::ClearTargetFields()
{
    if (TargetNameText)    TargetNameText->SetText(FText::GetEmpty());
    if (TargetMembersText) TargetMembersText->SetText(FText::GetEmpty());
    if (TargetToughText)   TargetToughText->SetText(FText::GetEmpty());
    if (TargetWoundsText)  TargetWoundsText->SetText(FText::GetEmpty());
}

// ---------- Buttons ----------

void UTurnContextWidget::OnSelectTargetClicked()
{
    if (AMatchPlayerController* P = MPC())
        P->EnterTargetMode();
}

void UTurnContextWidget::OnCancelClicked()
{
    AMatchGameState* S = GS();
    AMatchPlayerController* P = MPC();
    if (!S || !P) return;

    if (P->SelectedUnit)
        P->Server_CancelPreview(P->SelectedUnit);

    P->ExitTargetMode();
}

void UTurnContextWidget::OnPrimaryActionClicked()
{
    AMatchGameState* S = GS();
    AMatchPlayerController* P = MPC();
    if (!S || !P || !P->SelectedUnit) return;

    if (S->TurnPhase == ETurnPhase::Shoot)
    {
        if (S->Preview.Attacker == P->SelectedUnit && S->Preview.Target)
        {
            P->Server_ConfirmShoot(P->SelectedUnit, S->Preview.Target);
        }
    }

    P->ExitTargetMode();
}

