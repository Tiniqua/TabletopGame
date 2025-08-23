#include "TurnContextWidget.h"

#include "Components/WidgetSwitcher.h"
#include "Components/PanelWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

#include "Controllers/MatchPlayerController.h"
#include "Actors/UnitBase.h"
#include "Gamemodes/MatchGameMode.h"   // EMatchPhase, ETurnPhase, AMatchGameState

AMatchGameState* UTurnContextWidget::GS() const
{
    return GetWorld() ? GetWorld()->GetGameState<AMatchGameState>() : nullptr;
}
AMatchPlayerController* UTurnContextWidget::MPC() const
{
    return GetOwningPlayer<AMatchPlayerController>();
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
        case ETurnPhase::Move:   ShowMovement(Sel);      break;
        case ETurnPhase::Shoot:
        case ETurnPhase::Charge:
        case ETurnPhase::Fight:  ShowActionPhases(Sel);  break;
        default:                 ShowBlank();            break;
    }
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
}

void UTurnContextWidget::ShowMovement(AUnitBase* Sel)
{
    if (PhaseSwitcher) PhaseSwitcher->SetActiveWidget(MovementPanel);

    const bool bEnable = IsMyTurn() && Sel && Sel->OwningPS == GetOwningPlayer()->PlayerState && Sel->ModelsCurrent > 0;

    FillAttacker(Sel);
    ClearTargetFields();

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

void UTurnContextWidget::ShowActionPhases(AUnitBase* Sel)
{
    if (PhaseSwitcher) PhaseSwitcher->SetActiveWidget(ActionPanel);

    AMatchGameState* S = GS();
    AMatchPlayerController* P = MPC();
    if (!S || !P) return;

    const bool bMyTurn = IsMyTurn();
    const bool bMine   = bMyTurn && Sel && Sel->OwningPS == P->PlayerState && Sel->ModelsCurrent > 0;

    FillAttacker(Sel);
    ClearTargetFields();

    auto SetPrimary = [&](const TCHAR* Label, bool bEnable)
    {
        if (PrimaryActionLabel) PrimaryActionLabel->SetText(FText::FromString(Label));
        if (PrimaryActionBtn)   PrimaryActionBtn->SetIsEnabled(bEnable);
    };

    // defaults
    if (SelectTargetBtn)  SelectTargetBtn->SetIsEnabled(false);
    if (CancelBtn)        CancelBtn->SetIsEnabled(false);
    SetPrimary(TEXT(""), false);

    const bool bPreviewForMe = (S->Preview.Attacker == Sel) && (S->Preview.Target != nullptr);

    if (bPreviewForMe)
        FillTarget(S->Preview.Target);

    switch (S->TurnPhase)
    {
    case ETurnPhase::Shoot:
    {
        const bool bCanShoot = bMine && Sel && !Sel->bHasShot;
        if (SelectTargetBtn) SelectTargetBtn->SetIsEnabled(bCanShoot && !bPreviewForMe);
        SetPrimary(TEXT("Confirm"), bCanShoot && bPreviewForMe);
        if (CancelBtn)       CancelBtn->SetIsEnabled(bMine && (P->bTargetMode || bPreviewForMe));
        break;
    }
    case ETurnPhase::Charge:
    {
        const bool bCanTryCharge = bMine && Sel && !Sel->bChargeAttempted;
        if (SelectTargetBtn) SelectTargetBtn->SetIsEnabled(bCanTryCharge && !bPreviewForMe);
        SetPrimary(TEXT("Attempt Charge"), bCanTryCharge && bPreviewForMe);
        if (CancelBtn)       CancelBtn->SetIsEnabled(bMine && (P->bTargetMode || bPreviewForMe));
        break;
    }
    case ETurnPhase::Fight:
    {
        const bool bArmed = bMine && Sel && Sel->ModelsCurrent > 0;
        if (SelectTargetBtn) SelectTargetBtn->SetIsEnabled(bArmed && !bPreviewForMe);
        // Require a preview to know which enemy to fight (simplest, server validates anyway)
        SetPrimary(TEXT("Fight"), bArmed && bPreviewForMe);
        if (CancelBtn)       CancelBtn->SetIsEnabled(bMine && (P->bTargetMode || bPreviewForMe));
        break;
    }
    default:
        break;
    }
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
        AttackerNameText->SetText(FText::FromName(U->UnitId));

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

    if (TargetNameText)    TargetNameText->SetText(FText::FromName(U->UnitId));
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

    switch (S->TurnPhase)
    {
        case ETurnPhase::Shoot:
            if (S->Preview.Attacker == P->SelectedUnit && S->Preview.Target)
                P->Server_ConfirmShoot(P->SelectedUnit, S->Preview.Target);
            break;

        case ETurnPhase::Charge:
            if (S->Preview.Attacker == P->SelectedUnit && S->Preview.Target)
                P->Server_AttemptCharge(P->SelectedUnit, S->Preview.Target);
            break;

        case ETurnPhase::Fight:
            if (S->Preview.Attacker == P->SelectedUnit && S->Preview.Target)
                P->Server_Fight(P->SelectedUnit, S->Preview.Target);
            break;

        default:
            break;
    }

    P->ExitTargetMode();
}
