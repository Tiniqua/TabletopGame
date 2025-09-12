
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

    // ✅ P1/P2 is the ground truth for these arrays
    const bool bIsLocalP1 = (OPC->PlayerState == S->P1);

    const TArray<FUnitCount>& LocalRem = bIsLocalP1 ? S->P1Remaining : S->P2Remaining;
    const TArray<FUnitCount>& OppRem   = bIsLocalP1 ? S->P2Remaining : S->P1Remaining;

    // (Optional) debug to catch accidental regressions:
    if (const ATabletopPlayerState* LocalPS = OPC->GetPlayerState<ATabletopPlayerState>())
    {
        if (LocalPS->TeamNum == 1 && !bIsLocalP1)
        {
            UE_LOG(LogTemp, Warning, TEXT("DeploymentWidget: Local is Team 1 but NOT P1. "
                   "Using pointer mapping (correct). This is expected when P2 is Team 1."));
        }
        if (LocalPS->TeamNum == 2 && bIsLocalP1)
        {
            UE_LOG(LogTemp, Warning, TEXT("DeploymentWidget: Local is Team 2 but IS P1. "
                   "Using pointer mapping (correct). This is expected when P1 is Team 2."));
        }
    }

    LocalUnitsPanel->ClearChildren();
    if (OppUnitsPanel) OppUnitsPanel->ClearChildren();

    for (const FUnitCount& E : LocalRem)
    {
        if (E.Count <= 0) continue;
        if (UDeployRowWidget* Row = CreateWidget<UDeployRowWidget>(GetOwningPlayer(), DeployRowClass))
        {
            Row->Init(E.UnitId, FText::FromString(E.UnitId.ToString()), nullptr, E.Count);
            LocalUnitsPanel->AddChild(Row);
        }
    }

    if (OppUnitsPanel)
    {
        for (const FUnitCount& E : OppRem)
        {
            if (E.Count <= 0) continue;
            if (UDeployRowWidget* Row = CreateWidget<UDeployRowWidget>(GetOwningPlayer(), DeployRowClass))
            {
                Row->Init(E.UnitId, FText::FromString(E.UnitId.ToString()), nullptr, E.Count);
                if (Row->SelectBtn) Row->SelectBtn->SetIsEnabled(false);
                OppUnitsPanel->AddChild(Row);
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

