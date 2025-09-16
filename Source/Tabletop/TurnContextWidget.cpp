#include "TurnContextWidget.h"

#include "ActionButtonWidget.h"
#include "KeywordChipWidget.h"
#include "LibraryHelpers.h"
#include "WeaponKeywordHelpers.h"

#include "Components/PanelWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

#include "Actors/CoverVolume.h"
#include "Actors/UnitAction.h"
#include "Actors/UnitBase.h"
#include "Controllers/MatchPlayerController.h"
#include "Gamemodes/MatchGameMode.h"
#include "PlayerStates/TabletopPlayerState.h"

// ---------------- helpers (unchanged) ----------------

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

static int32 GetInvuln_Client(const AUnitBase* U)      { return TryGetIntProp(U, TEXT("InvulnerableSaveRep"), 7); }
static int32 GetFeelNoPain_Client(const AUnitBase* U)  { return TryGetIntProp(U, TEXT("FeelNoPainRep"),       7); }

static FEffectiveUnitFlags BuildEffective(const AUnitBase* Sel)
{
    FEffectiveUnitFlags E{};
    if (!Sel) return E;

    E.MoveBudgetEffective = Sel->MoveBudgetInches;

    const FWeaponProfile& W = Sel->GetActiveWeaponProfile();
    E.bCanShootAfterAdvance = UWeaponKeywordHelpers::HasKeyword(W, EWeaponKeyword::Assault);

    return E;
}

AMatchGameState* UTurnContextWidget::GS() const { return GetWorld() ? GetWorld()->GetGameState<AMatchGameState>() : nullptr; }
AMatchPlayerController* UTurnContextWidget::MPC() const { return GetOwningPlayer<AMatchPlayerController>(); }

// ---------------- lifecycle ----------------

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

// ---------------- events ----------------

void UTurnContextWidget::OnMatchChanged()                     { Refresh(); }
void UTurnContextWidget::OnSelectedChanged(AUnitBase* /*U*/)  { Refresh(); }

bool UTurnContextWidget::IsMyTurn() const
{
    if (const AMatchGameState* S = GS())
    if (const APlayerController* P = GetOwningPlayer())
        return (S->Phase == EMatchPhase::Battle) && (S->CurrentTurn == P->PlayerState);
    return false;
}

// ---------------- main refresh ----------------

void UTurnContextWidget::Refresh()
{
    AMatchGameState* S = GS();
    AMatchPlayerController* P = MPC();
    AUnitBase* Sel = P ? P->SelectedUnit : nullptr;

    const bool bBattle  = S && S->Phase == EMatchPhase::Battle;
    const ETurnPhase Ph = bBattle ? S->TurnPhase : ETurnPhase::Move;

    // Attacker info + AP
    FillAttacker(Sel);
    UpdateActionPoints();

    // Target/preview text only (no buttons here anymore)
    const bool bPreviewActive =
        (bBattle && Ph == ETurnPhase::Shoot &&
         S->Preview.Attacker == Sel && S->Preview.Target != nullptr);

    if (bPreviewActive)
    {
        FillTarget(S->Preview.Target);
        const int32 HitMod = S->ActionPreview.HitMod;
        const int32 SaveMod = S->ActionPreview.SaveMod;
        const ECoverType Cover = S->ActionPreview.Cover;
        UpdateCombatEstimates(Sel, S->Preview.Target, HitMod, SaveMod, Cover);
    }
    else
    {
        ClearTargetFields();
        ClearEstimateFields();
    }
    
    RebuildActionButtons(Sel);
}

// ---------------- AP + actions ----------------

void UTurnContextWidget::UpdateActionPoints()
{
    if (!APText)
        return;

    AMatchPlayerController* P = MPC();
    AUnitBase* Sel = P ? P->SelectedUnit : nullptr;
    if (!Sel)
    {
        APText->SetText(FText::GetEmpty());
        return;
    }

    if (UActorComponent* C = Sel->GetComponentByClass(UUnitActionResourceComponent::StaticClass()))
    {
        const auto* APComp = Cast<UUnitActionResourceComponent>(C);
        if (APComp)
        {
            APText->SetText(FText::FromString(
                FString::Printf(TEXT("AP: %d / %d"), APComp->CurrentAP, APComp->MaxAP)));
        }
    }
    else
    {
        APText->SetText(FText::GetEmpty());
    }
}

void UTurnContextWidget::RebuildActionButtons(AUnitBase* Sel)
{
    if (!ActionsPanel) return;
    ActionsPanel->ClearChildren();

    AMatchGameState* S  = GS();
    AMatchPlayerController* PC = MPC();
    if (!S || !PC || !Sel) return;
    if (Sel->ModelsCurrent <= 0) return; // dead unit: nothing to show

    // ---- tolerant ownership/turn checks (avoid strict pointer identity issues) ----
    const auto* MyTPS  = Cast<ATabletopPlayerState>(PC->PlayerState);
    const auto* SelTPS = Cast<ATabletopPlayerState>(Sel->OwningPS);
    const auto* CurTPS = Cast<ATabletopPlayerState>(S->CurrentTurn);

    const bool bOwner =
        (Sel->OwningPS == PC->PlayerState) ||
        (MyTPS && SelTPS && MyTPS->TeamNum > 0 && SelTPS->TeamNum == MyTPS->TeamNum);

    if (!bOwner) return; // don’t show actions for enemy units

    const bool bBattle = (S->Phase == EMatchPhase::Battle);
    const bool bTurn   =
        bBattle &&
        (S->CurrentTurn == PC->PlayerState ||
         (MyTPS && CurTPS && MyTPS->TeamNum > 0 && CurTPS->TeamNum == MyTPS->TeamNum));

    const ETurnPhase Ph = bBattle ? S->TurnPhase : ETurnPhase::Move;

    TSubclassOf<UActionButtonWidget> RowClass =
        ActionButtonClass ? ActionButtonClass
                          : TSubclassOf<UActionButtonWidget>(UActionButtonWidget::StaticClass());

    for (UUnitAction* Act : Sel->GetActions())
    {
        if (!Act) continue;
        if (Act->Desc.Phase != Ph) continue; // only actions for current phase

        // Build preview args. Only stamp InstigatorPC when it's actually our turn;
        // this controls CanExecute() for actions that require a PC (e.g., Advance/Shoot).
        FActionRuntimeArgs PreviewArgs;
        PreviewArgs.InstigatorPC = bTurn ? PC : nullptr;

        if (S->Preview.Attacker == Sel && S->Preview.Target)
            PreviewArgs.TargetUnit = S->Preview.Target;

        // Enable only if it's our turn; server will still enforce AP/phase.
        const bool bCan = bTurn ? Act->CanExecute(Sel, PreviewArgs) : false;

        UActionButtonWidget* Row = CreateWidget<UActionButtonWidget>(GetOwningPlayer(), RowClass);
        if (!Row) continue;

        Row->Init(Act, Act->Desc.DisplayName, bCan);
        Row->OnActionClicked.AddDynamic(this, &UTurnContextWidget::HandleDynamicActionClicked);
        ActionsPanel->AddChild(Row);
    }
}


void UTurnContextWidget::HandleDynamicActionClicked(UUnitAction* Act)
{
    AMatchPlayerController* PC = MPC();
    if (!PC || !PC->SelectedUnit || !Act) return;

    // Ground-click actions: set pending id and wait for ground click
    if (Act->Desc.bRequiresGroundClick)
    {
        PC->PendingGroundActionId = Act->Desc.ActionId;
        PC->PendingActionUnit     = PC->SelectedUnit;
        return;
    }

    // Enemy-target actions: if preview exists for my selected unit -> execute; else enter target mode
    if (Act->Desc.bRequiresEnemyTarget)
    {
        AMatchGameState* S = GS();
        const bool bPreviewReady = S && (S->Preview.Attacker == PC->SelectedUnit) && S->Preview.Target;
        if (bPreviewReady)
        {
            FActionRuntimeArgs Args;
            Args.TargetUnit     = S->Preview.Target;
            // Server stamps InstigatorPC
            PC->Server_ExecuteAction(PC->SelectedUnit, Act->Desc.ActionId, Args);
        }
        else
        {
            PC->EnterTargetMode();
        }
        return;
    }

    // Instant actions
    FActionRuntimeArgs Args;
    PC->Server_ExecuteAction(PC->SelectedUnit, Act->Desc.ActionId, Args);
}

// ---------------- UI fills & math (unchanged from your version) ----------------

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

    const int32 hitNeed   = FMath::Clamp(baseToHit + (HitMod * -1), 2, 6);
    const int32 woundNeed = CombatMath::ToWoundTarget(sVal, tVal);

    int32 saveNeed = CombatMath::ModifiedSaveNeed(baseSave, ap);
    saveNeed       = FMath::Clamp(saveNeed - SaveMod, 2, 7);

    const int32 invTN = GetInvuln_Client(Target);
    if (invTN >= 2 && invTN <= 6)
        saveNeed = FMath::Min(saveNeed, invTN);

    const float pHit   = CombatMath::ProbAtLeast(hitNeed);
    const float pWound = CombatMath::ProbAtLeast(woundNeed);
    const float pSave  = (saveNeed <= 6) ? CombatMath::ProbAtLeast(saveNeed) : 0.f;
    const float pFail  = 1.f - pSave;

    const float pUnsavedPerAtk = pHit * pWound * pFail;
    float evDamage             = float(totalAtk) * pUnsavedPerAtk * float(dmg);

    const int32 fnpTN = GetFeelNoPain_Client(Target);
    if (fnpTN >= 2 && fnpTN <= 6)
    {
        const float pFNP = CombatMath::ProbAtLeast(fnpTN);
        evDamage *= (1.f - pFNP);
    }

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

// ---------------- fills ----------------

void UTurnContextWidget::FillAttacker(AUnitBase* U)
{
    if (!U)
    {
        if (AttackerNameText)    AttackerNameText->SetText(FText::GetEmpty());
        if (AttackerMembersText) AttackerMembersText->SetText(FText::GetEmpty());
        if (AttackerRangeText)   AttackerRangeText->SetText(FText::GetEmpty());
        if (AttackerAttacksText) AttackerAttacksText->SetText(FText::GetEmpty());
        if (AttackerDamageText)  AttackerDamageText->SetText(FText::GetEmpty());
        UpdateActionPoints();
        return;
    }

    if (AttackerNameText)    AttackerNameText->SetText(U->UnitName);
    if (AttackerMembersText) AttackerMembersText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d models"), U->ModelsCurrent, U->ModelsMax)));
    if (AttackerRangeText)   AttackerRangeText->SetText(FText::FromString(FString::Printf(TEXT("Range: %d\""), (int32)U->GetWeaponRange())));
    if (AttackerAttacksText) AttackerAttacksText->SetText(FText::FromString(FString::Printf(TEXT("Attacks: %d"), U->GetAttacks() * FMath::Max(0, U->ModelsCurrent))));
    if (AttackerDamageText)  AttackerDamageText->SetText(FText::FromString(FString::Printf(TEXT("Damage: %d"), U->GetDamage())));

    UpdateActionPoints();
}

void UTurnContextWidget::FillTarget(AUnitBase* U)
{
    if (!U) { ClearTargetFields(); return; }

    if (TargetNameText)    TargetNameText->SetText(U->UnitName);
    if (TargetMembersText) TargetMembersText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d models"), U->ModelsCurrent, U->ModelsMax)));
    if (TargetToughText)   TargetToughText->SetText(FText::FromString(FString::Printf(TEXT("T: %d"), U->GetToughness())));

    const int32 invTN = GetInvuln_Client(U);
    const int32 fnpTN = GetFeelNoPain_Client(U);

    FString extras;
    if (invTN >= 2 && invTN <= 6) extras += FString::Printf(TEXT("  | Inv %d+"), invTN);
    if (fnpTN >= 2 && fnpTN <= 6) extras += FString::Printf(TEXT("  | FNP %d++"), fnpTN);

    if (TargetWoundsText)
        TargetWoundsText->SetText(FText::FromString(
            FString::Printf(TEXT("W: %d%s"), U->GetWounds(), *extras)));

    UpdateActionPoints();
}

void UTurnContextWidget::ClearTargetFields()
{
    if (TargetNameText)    TargetNameText->SetText(FText::GetEmpty());
    if (TargetMembersText) TargetMembersText->SetText(FText::GetEmpty());
    if (TargetToughText)   TargetToughText->SetText(FText::GetEmpty());
    if (TargetWoundsText)  TargetWoundsText->SetText(FText::GetEmpty());
}

