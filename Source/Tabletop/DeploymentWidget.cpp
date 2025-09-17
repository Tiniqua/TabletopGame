
#include "DeploymentWidget.h"

#include "DeployRowWidget.h"
#include "NameUtils.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Controllers/MatchPlayerController.h"
#include "Gamemodes/MatchGameMode.h"
#include "PlayerStates/TabletopPlayerState.h"

AMatchGameState* UDeploymentWidget::GS() const { return GetWorld()? GetWorld()->GetGameState<AMatchGameState>() : nullptr; }
AMatchPlayerController* UDeploymentWidget::MPC() const { return GetOwningPlayer<AMatchPlayerController>(); }

FString UDeploymentWidget::FactionDisplay(EFaction F)
{
    if (const UEnum* E = StaticEnum<EFaction>())
        return E->GetDisplayNameTextByValue((int64)F).ToString();
    return TEXT("None");
}

void UDeploymentWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // Try an initial bind
    if (AMatchGameState* S = GS())
    {
        if (BoundGS.Get() != S)
        {
            if (BoundGS.IsValid())
                BoundGS->OnDeploymentChanged.RemoveDynamic(this, &UDeploymentWidget::OnDeploymentChanged);

            S->OnDeploymentChanged.AddDynamic(this, &UDeploymentWidget::OnDeploymentChanged);
            BoundGS = S;
        }
    }

    if (StartBattleBtn)
        StartBattleBtn->OnClicked.AddDynamic(this, &UDeploymentWidget::OnStartBattleClicked);

    RebuildUnitPanels();
    RefreshFromState();

    // Keep the spin-up loop — it also doubles as a GS-rebind watcher.
    if (UWorld* W = GetWorld())
        W->GetTimerManager().SetTimer(SetupRetryTimer, this, &UDeploymentWidget::TickUntilReady, 0.2f, true, 0.2f);
}

void UDeploymentWidget::NativeDestruct()
{
    if (UWorld* W = GetWorld())
        W->GetTimerManager().ClearTimer(SetupRetryTimer);

    if (BoundGS.IsValid())
    {
        BoundGS->OnDeploymentChanged.RemoveDynamic(this, &UDeploymentWidget::OnDeploymentChanged);
        BoundGS.Reset();
    }
    Super::NativeDestruct();
}

bool UDeploymentWidget::IsReadyToSetup() const
{
    const AMatchGameState* S = GS();
    const APlayerController* OPC = GetOwningPlayer();
    if (!S || !OPC || !OPC->PlayerState) return false;

    const bool bHavePlayers  = (S->P1 != nullptr && S->P2 != nullptr);
    const bool bHaveDeployer = (S->CurrentDeployer != nullptr);
    const bool bInitDone     = S->bTeamsAndTurnsInitialized;

    return bHavePlayers && bHaveDeployer && bInitDone;
}

void UDeploymentWidget::TickUntilReady()
{
    // Rebind if GS changed
    if (AMatchGameState* S = GS())
    {
        if (BoundGS.Get() != S)
        {
            if (BoundGS.IsValid())
                BoundGS->OnDeploymentChanged.RemoveDynamic(this, &UDeploymentWidget::OnDeploymentChanged);

            S->OnDeploymentChanged.AddDynamic(this, &UDeploymentWidget::OnDeploymentChanged);
            BoundGS = S;

            // Immediately re-pull on GS switch
            RebuildUnitPanels();
            RefreshFromState();
        }
    }

    if (bSetupComplete) return;

    if (IsReadyToSetup())
    {
        DoInitialSetup();
        if (UWorld* W = GetWorld())
            W->GetTimerManager().ClearTimer(SetupRetryTimer);
        bSetupComplete = true;
    }
}

void UDeploymentWidget::DoInitialSetup()
{
    RebuildUnitPanels();
    RefreshFromState();
}

void UDeploymentWidget::OnStartBattleClicked()
{
    if (AMatchPlayerController* PC = MPC())
    {
        PC->Server_StartBattle();
    }
}

void UDeploymentWidget::OnDeploymentChanged()
{
    RebuildUnitPanels();
    RefreshFromState();
}

void UDeploymentWidget::RebuildUnitPanels()
{
    if (!LocalUnitsPanel) return;

    AMatchGameState* S = GS();
    APlayerController* OPC = GetOwningPlayer();
    if (!S || !OPC || !OPC->PlayerState) return;

    // Clear previous entries
    LocalUnitsPanel->ClearChildren();
    if (OppUnitsPanel) OppUnitsPanel->ClearChildren();

    const bool bIsLocalP1 = (OPC->PlayerState == S->P1);

    // ---------- LOCAL SIDE ----------
    {
        const TArray<FRosterEntry>& LocalRem = bIsLocalP1 ? S->P1Remaining : S->P2Remaining;

        // Resolve Units DT for the local player's faction (if available on this machine)
        UDataTable* LocalUnitsDT = nullptr;
        if (AMatchGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AMatchGameMode>() : nullptr)
        {
            if (const ATabletopPlayerState* LocalTPS = OPC->GetPlayerState<ATabletopPlayerState>())
            {
                LocalUnitsDT = GM->UnitsForFaction(LocalTPS->SelectedFaction);
            }
        }

        for (const FRosterEntry& E : LocalRem)
        {
            if (E.Count <= 0) continue;

            FString Label = E.UnitId.ToString();

            if (LocalUnitsDT)
            {
                if (const FUnitRow* Row = LocalUnitsDT->FindRow<FUnitRow>(E.UnitId, TEXT("DeployLabel_Local")))
                {
                    if (Row->Weapons.IsValidIndex(E.WeaponIndex))
                    {
                        Label += FString::Printf(TEXT(" — %s"), *Row->Weapons[E.WeaponIndex].WeaponId.ToString());
                    }
                }
            }

            if (UDeployRowWidget* RowW = CreateWidget<UDeployRowWidget>(GetOwningPlayer(), DeployRowClass))
            {
                RowW->InitDisplay(FText::FromString(Label), /*Icon*/nullptr, E.Count);
                RowW->SetDeployPayload(E.UnitId, E.WeaponIndex);
                LocalUnitsPanel->AddChild(RowW);
            }
        }
    }

    // ---------- OPPONENT SIDE ----------
    if (OppUnitsPanel)
    {
        const TArray<FRosterEntry>& OppRem = bIsLocalP1 ? S->P2Remaining : S->P1Remaining;

        // Resolve Units DT for the opponent's faction (if available on this machine)
        UDataTable* OppUnitsDT = nullptr;
        if (AMatchGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AMatchGameMode>() : nullptr)
        {
            const ATabletopPlayerState* OppTPS = bIsLocalP1
                ? (S->P2 ? Cast<ATabletopPlayerState>(S->P2) : nullptr)
                : (S->P1 ? Cast<ATabletopPlayerState>(S->P1) : nullptr);

            if (OppTPS)
            {
                OppUnitsDT = GM->UnitsForFaction(OppTPS->SelectedFaction);
            }
        }

        for (const FRosterEntry& E : OppRem)
        {
            if (E.Count <= 0) continue;

            FString Label = E.UnitId.ToString();

            if (OppUnitsDT)
            {
                if (const FUnitRow* Row = OppUnitsDT->FindRow<FUnitRow>(E.UnitId, TEXT("DeployLabel_Opp")))
                {
                    if (Row->Weapons.IsValidIndex(E.WeaponIndex))
                    {
                        Label += FString::Printf(TEXT(" — %s"), *Row->Weapons[E.WeaponIndex].WeaponId.ToString());
                    }
                }
            }

            if (UDeployRowWidget* RowW = CreateWidget<UDeployRowWidget>(GetOwningPlayer(), DeployRowClass))
            {
                RowW->InitDisplay(FText::FromString(Label), /*Icon*/nullptr, E.Count);

                // Show as read-only on the opponent side (no deploying from this list)
                RowW->SetIsEnabled(false);

                // (Don’t set deploy payload, since this list is informational)
                OppUnitsPanel->AddChild(RowW);
            }
        }
    }
}


void UDeploymentWidget::RefreshFromState()
{
    AMatchGameState* S = GS();
    APlayerController* OPC = GetOwningPlayer();
    if (!S || !OPC || !OPC->PlayerState) return;

    auto NiceName = [](APlayerState* PS)->FString
    {
        return UNameUtils::GetShortPlayerName(PS);
    };

    auto FactionLabel = [&](APlayerState* PS)->FText
    {
        if (const ATabletopPlayerState* TPS = PS ? Cast<ATabletopPlayerState>(PS) : nullptr)
            return FText::FromString(FactionDisplay(TPS->SelectedFaction));
        return FText::FromString(FactionDisplay((EFaction)0)); // None
    };

    if (P1Name)        P1Name->SetText(FText::FromString(S->P1 ? NiceName(S->P1) : TEXT("")));
    if (P2Name)        P2Name->SetText(FText::FromString(S->P2 ? NiceName(S->P2) : TEXT("")));
    if (P1FactionText) P1FactionText->SetText(S->P1 ? FactionLabel(S->P1) : FText::GetEmpty());
    if (P2FactionText) P2FactionText->SetText(S->P2 ? FactionLabel(S->P2) : FText::GetEmpty());
    // -----------------------------------------------------

    const ATabletopPlayerState* LocalPS = OPC->GetPlayerState<ATabletopPlayerState>();

    // My turn? Compare team numbers (more robust than pointer equality)
    // bool bMyTurn = false;
    // if (LocalPS && LocalPS->TeamNum > 0 && S->CurrentDeployer)
    // {
    //     if (const ATabletopPlayerState* CurPS = Cast<ATabletopPlayerState>(S->CurrentDeployer))
    //     {
    //         bMyTurn = (CurPS->TeamNum == LocalPS->TeamNum);
    //     }
    // }

    const bool bMyTurn = (S->CurrentDeployer == OPC->PlayerState);


    if (TurnBanner)
        TurnBanner->SetText(FText::FromString(bMyTurn ? TEXT("Your turn to deploy")
                                                      : TEXT("Opponent is deploying")));

    if (LocalUnitsPanel)
        LocalUnitsPanel->SetIsEnabled(bMyTurn && (S->Phase == EMatchPhase::Deployment) && !S->bDeploymentComplete);

    const bool bHost = (OPC->PlayerState == S->P1);
    if (StartBattleBtn)
        StartBattleBtn->SetIsEnabled(bHost && S->bDeploymentComplete && S->Phase == EMatchPhase::Deployment);
}

