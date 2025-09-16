#include "TurnContextWidget.h"

#include "KeywordChipWidget.h"
#include "LibraryHelpers.h"
#include "WeaponKeywordHelpers.h"
#include "Components/WidgetSwitcher.h"
#include "Components/PanelWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Actors/CoverVolume.h"
#include "Controllers/MatchPlayerController.h"
#include "Actors/UnitBase.h"
#include "Gamemodes/MatchGameMode.h"   // EMatchPhase, ETurnPhase, AMatchGameState

struct FEffectiveUnitFlags
{
    bool  bCanShootAfterAdvance = false;
    float MoveBudgetEffective   = 0.f;
};

static int32 TryGetIntProp(const AUnitBase* U, const TCHAR* PropName, int32 DefaultIfMissing = 7)
{
    if (!U) return DefaultIfMissing;
    if (const FProperty* P = U->GetClass()->FindPropertyByName(FName(PropName)))
        if (const FIntProperty* IP = CastField<FIntProperty>(P))
            return IP->GetPropertyValue_InContainer(U);
    return DefaultIfMissing;
}

static int32 GetInvuln_Client(const AUnitBase* U)
{
    return TryGetIntProp(U, TEXT("InvulnerableSaveRep"), 7);
}

static int32 GetFeelNoPain_Client(const AUnitBase* U)
{
    return TryGetIntProp(U, TEXT("FeelNoPainRep"),       7);
}

static FEffectiveUnitFlags BuildEffective(const AUnitBase* Sel)
{
    FEffectiveUnitFlags E{};
    if (!Sel) return E;

    E.MoveBudgetEffective = Sel->MoveBudgetInches;

    // Weapon permission (Assault)
    const FWeaponProfile& W = Sel->GetActiveWeaponProfile();
    E.bCanShootAfterAdvance = UWeaponKeywordHelpers::HasKeyword(W, EWeaponKeyword::Assault);

    // Mods may also set similar permission (if you introduce such a flag in Mods)
    // Example: if you decide to provide bCanShootAfterAdvance in FRollModifiers or a separate tag,
    // read and OR it here.

    return E;
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

    const int32 dmg       = FMath::Max(1, Attacker->GetDamage());
    const int32 sVal      = Attacker->WeaponStrengthRep;
    const int32 tVal      = Target->GetToughness();
    const int32 ap        = FMath::Max(0, Attacker->WeaponAPRep);
    const int32 baseToHit = FMath::Clamp(Attacker->WeaponSkillToHitRep, 2, 6);
    const int32 baseSave  = Target->GetSave();

    // Cover/hit math matches server’s sign convention
    const int32 hitNeed   = FMath::Clamp(baseToHit + (HitMod * -1), 2, 6);
    const int32 woundNeed = CombatMath::ToWoundTarget(sVal, tVal);

    // Start from AP-modified save and cover bonus
    int32 saveNeed = CombatMath::ModifiedSaveNeed(baseSave, ap);
    saveNeed       = FMath::Clamp(saveNeed - SaveMod, 2, 7);

    // ---- NEW: cap by invulnerable (if any) ----
    const int32 invTN = GetInvuln_Client(Target); // 2..6, 7 means none
    if (invTN >= 2 && invTN <= 6)
        saveNeed = FMath::Min(saveNeed, invTN);

    // Core probabilities
    const float pHit   = CombatMath::ProbAtLeast(hitNeed);
    const float pWound = CombatMath::ProbAtLeast(woundNeed);
    const float pSave  = (saveNeed <= 6) ? CombatMath::ProbAtLeast(saveNeed) : 0.f;
    const float pFail  = 1.f - pSave;

    const float pUnsavedPerAtk = pHit * pWound * pFail;
    float evDamage             = float(totalAtk) * pUnsavedPerAtk * float(dmg);

    // ---- NEW: Feel No Pain expected reduction ----
    const int32 fnpTN = GetFeelNoPain_Client(Target); // 2..6, 7 means none
    if (fnpTN >= 2 && fnpTN <= 6)
    {
        const float pFNP = CombatMath::ProbAtLeast(fnpTN); // chance each damage point is negated
        evDamage *= (1.f - pFNP);
    }

    // --- UI ---
    if (HitChanceText)
        HitChanceText->SetText(FText::FromString(
            FString::Printf(TEXT("Hit: %d%% (need %d+)"), FMath::RoundToInt(pHit*100.f), hitNeed)));

    if (WoundChanceText)
        WoundChanceText->SetText(FText::FromString(
            FString::Printf(TEXT("Wound: %d%% (need %d+)"), FMath::RoundToInt(pWound*100.f), woundNeed)));

    if (SaveFailText)
    {
        if (saveNeed <= 6)
        {
            // Annotate when Invulnerable is capping
            if (invTN >= 2 && invTN <= 6 && saveNeed == invTN)
            {
                SaveFailText->SetText(FText::FromString(
                    FString::Printf(TEXT("Fail Save: %d%% (save %d+, inv %d+)"),
                                    FMath::RoundToInt(pFail*100.f), saveNeed, invTN)));
            }
            else
            {
                SaveFailText->SetText(FText::FromString(
                    FString::Printf(TEXT("Fail Save: %d%% (save %d+)"),
                                    FMath::RoundToInt(pFail*100.f), saveNeed)));
            }
        }
        else
        {
            SaveFailText->SetText(FText::FromString(TEXT("Fail Save: 100% (no save)")));
        }
    }

    if (EstDamageText)
    {
        if (fnpTN >= 2 && fnpTN <= 6)
        {
            EstDamageText->SetText(FText::FromString(
                FString::Printf(TEXT("Est. Dmg: %.1f (FNP %d++)"), evDamage, fnpTN)));
        }
        else
        {
            EstDamageText->SetText(FText::FromString(
                FString::Printf(TEXT("Est. Dmg: %.1f"), evDamage)));
        }
    }

    if (CoverStatusText)
        CoverStatusText->SetText(FText::FromString(CombatMath::CoverTypeToText(CoverType)));
}

void UTurnContextWidget::RebuildKeywordChips(const TArray<FKeywordUIInfo>& Infos)
{
    if (!KeywordPanel || !KeywordChipClass) return;

    KeywordPanel->ClearChildren();

    for (const FKeywordUIInfo& I : Infos)
    {
        UKeywordChipWidget* Chip = CreateWidget<UKeywordChipWidget>(GetOwningPlayer(), KeywordChipClass);
        if (!ensure(Chip)) continue;

        Chip->InitFromInfo(I);
        KeywordPanel->AddChild(Chip);
    }
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
    if (AdvanceBtn) AdvanceBtn->OnClicked.AddDynamic(this, &UTurnContextWidget::OnAdvanceClicked);

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

        const int32 HitMod = S->ActionPreview.HitMod;
        const int32 SaveMod = S->ActionPreview.SaveMod;
        const ECoverType Cover = S->ActionPreview.Cover;

        FRollModifiers AttackerHitMods  = Sel->CollectStageMods(ECombatEvent::PreHitCalc, true,  S->Preview.Target);
        FRollModifiers AttackerSaveMods = Sel->CollectStageMods(ECombatEvent::PreSavingThrows, true,  S->Preview.Target);

        // If you also model target-side mods when being attacked:
        FRollModifiers TargetHitMods    = S->Preview.Target ? S->Preview.Target->CollectStageMods(ECombatEvent::PreHitCalc, false, Sel) : FRollModifiers{};
        FRollModifiers TargetSaveMods   = S->Preview.Target ? S->Preview.Target->CollectStageMods(ECombatEvent::PreSavingThrows, false, Sel) : FRollModifiers{};

        // Compose into the preview values coming from the server (HitMod/SaveMod)
        const int32 HitModTotal  = HitMod  + AttackerHitMods.HitNeedOffset  + TargetHitMods.HitNeedOffset;
        const int32 SaveModTotal = SaveMod + (AttackerSaveMods.bIgnoreCover ? 1 : 0) + (TargetSaveMods.bIgnoreCover ? -1 : 0);
        // ^ adjust this to your sign conventions; the idea is: fold the booleans/offsets into the display

        UpdateCombatEstimates(Sel, S->Preview.Target, HitModTotal, SaveModTotal, Cover);

        TArray<FKeywordUIInfo> Infos;
        UWeaponKeywordHelpers::BuildKeywordUIInfos(Sel, S->Preview.Target, HitMod, SaveMod, Infos);
        RebuildKeywordChips(Infos);
    }
    else
    {
        ClearEstimateFields();
        if (KeywordPanel) KeywordPanel->ClearChildren();
    }

    if(Sel)
    {
        const bool bCanShootBase = bMine && Sel && !Sel->bHasShot;

        const auto Eff = BuildEffective(Sel);
        const bool bBlockedByAdvance = Sel->bAdvancedThisTurn && !Eff.bCanShootAfterAdvance;

        const bool bCanShoot = bCanShootBase && !bBlockedByAdvance;

        if (SelectTargetBtn) SelectTargetBtn->SetIsEnabled(bCanShoot && !bPreviewForMe);
        SetPrimary(TEXT("Confirm"), bCanShoot && bPreviewForMe);
        if (CancelBtn)       CancelBtn->SetIsEnabled(bMine && (P->bTargetMode || bPreviewForMe));

        // Optional: show a hint that advancing blocked shooting
        if (bBlockedByAdvance && PrimaryActionLabel)
        {
            PrimaryActionLabel->SetText(FText::FromString(TEXT("Cannot shoot: advanced")));
        }
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
    ClearEstimateFields();
}

void UTurnContextWidget::ShowMovement(AUnitBase* Sel)
{
    if (PhaseSwitcher) PhaseSwitcher->SetActiveWidget(MovementPanel);

    const bool bEnable = IsMyTurn() && Sel && Sel->OwningPS == GetOwningPlayer()->PlayerState && Sel->ModelsCurrent > 0;

    FillAttacker(Sel);
    ClearTargetFields();
    ClearEstimateFields();

    if (MoveBudgetText)
    {
        const int32 Budget = Sel ? FMath::Max(0, (int32)FMath::RoundToInt(Sel->MoveBudgetInches)) : 0;
        MoveBudgetText->SetText(FText::FromString(FString::Printf(TEXT("Move left: %d\""), Budget)));
    }

    // --- Advance button state ---
    if (AdvanceBtn)
    {
        const bool bCanAdvance = bEnable && Sel && !Sel->bAdvancedThisTurn && (Sel->MoveMaxInches > 0.f);
        AdvanceBtn->SetIsEnabled(bCanAdvance);
    }
    if (AdvanceLabel)
    {
        if (Sel && Sel->bAdvancedThisTurn)
        {
            // After server applies, we show the rolled bonus (set from client callback below too)
            // If you want to show a cached last roll, store it on the Unit or Widget.
            AdvanceLabel->SetText(FText::FromString(TEXT("Advanced")));
        }
        else
        {
            AdvanceLabel->SetText(FText::FromString(TEXT("Advance?")));
        }
    }

    // Movement has no other buttons
    if (SelectTargetBtn)  SelectTargetBtn->SetIsEnabled(false);
    if (PrimaryActionBtn) PrimaryActionBtn->SetIsEnabled(false);
    if (CancelBtn)        CancelBtn->SetIsEnabled(false);
}

void UTurnContextWidget::OnAdvanceClicked()
{
    AMatchPlayerController* P = MPC();
    if (!P) return;

    if (AUnitBase* Sel = P->SelectedUnit)
    {
        P->Server_RequestAdvance(Sel);
        // Optional: optimistic UI
        if (AdvanceBtn)   AdvanceBtn->SetIsEnabled(false);
        if (AdvanceLabel) AdvanceLabel->SetText(FText::FromString(TEXT("Rolling...")));
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
    
    // Append invuln/FNP summary to wounds line
    const int32 invTN = GetInvuln_Client(U);
    const int32 fnpTN = GetFeelNoPain_Client(U);

    FString extras;
    if (invTN >= 2 && invTN <= 6) extras += FString::Printf(TEXT("  | Inv %d+"), invTN);
    if (fnpTN >= 2 && fnpTN <= 6) extras += FString::Printf(TEXT("  | FNP %d++"), fnpTN);

    if (TargetWoundsText)
        TargetWoundsText->SetText(FText::FromString(
            FString::Printf(TEXT("W: %d%s"), U->GetWounds(), *extras)));
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

