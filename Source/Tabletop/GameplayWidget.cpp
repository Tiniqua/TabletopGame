
#include "GameplayWidget.h"

#include "NameUtils.h"
#include "TurnContextWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Controllers/MatchPlayerController.h"
#include "Gamemodes/MatchGameMode.h"
#include "PlayerStates/TabletopPlayerState.h"

AMatchGameState* UGameplayWidget::GS() const
{
    return GetWorld() ? GetWorld()->GetGameState<AMatchGameState>() : nullptr;
}

static FString NiceName(APlayerState* PS)
{
    return UNameUtils::GetShortPlayerName(PS); // replaces old logic
}

AMatchPlayerController* UGameplayWidget::MPC() const
{
    return GetOwningPlayer<AMatchPlayerController>();
}

void UGameplayWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (AMatchGameState* S = GS())
    {
        S->OnDeploymentChanged.AddDynamic(this, &UGameplayWidget::OnMatchChanged);
        BoundGS = S;
    }

    if (AMatchPlayerController* P = MPC())
    {
        P->OnSelectedChanged.AddDynamic(this, &UGameplayWidget::OnSelectedChanged);
        BoundPC = P;
    }

    if (!TurnContext && WidgetTree)
    {
        // If you named it in WBP, you can GetWidgetFromName(TEXT("TurnContext"))
        if (UWidget* W = WidgetTree->FindWidget(TEXT("TurnContext")))
        {
            TurnContext = Cast<UTurnContextWidget>(W);
        }
    }
    
    // Initial state
    UpdateTurnContextVisibility();
    RefreshTopBar();
    RefreshBottom();

    if (NextBtn) NextBtn->OnClicked.AddDynamic(this, &UGameplayWidget::OnNextClicked);
}

void UGameplayWidget::NativeDestruct()
{
    if (BoundGS.IsValid())
    {
        BoundGS->OnDeploymentChanged.RemoveDynamic(this, &UGameplayWidget::OnMatchChanged);
        BoundGS.Reset();
    }
    if (BoundPC.IsValid())
    {
        BoundPC->OnSelectedChanged.RemoveDynamic(this, &UGameplayWidget::OnSelectedChanged);
        BoundPC.Reset();
    }
    Super::NativeDestruct();
}

void UGameplayWidget::OnMatchChanged()
{
    UpdateTurnContextVisibility();
    RefreshTopBar();
    RefreshBottom();
}

void UGameplayWidget::OnSelectedChanged(AUnitBase* /*NewSel*/)
{
    // Straightforward: visible only if a unit is selected and we’re in Battle
    UpdateTurnContextVisibility();
}

void UGameplayWidget::UpdateTurnContextVisibility()
{
    if (!TurnContext) return;

    const AMatchGameState* S = GS();
    const AMatchPlayerController* P = MPC();

    const bool bBattle = (S && S->Phase == EMatchPhase::Battle);
    const bool bHasSel = (P && P->SelectedUnit != nullptr);

    // Visible iff battle + a unit selected; else collapse it
    TurnContext->SetVisibility((bBattle && bHasSel) ? ESlateVisibility::Visible
                                                    : ESlateVisibility::Collapsed);
}

static FString FactionName(APlayerState* PS)
{
    if (const ATabletopPlayerState* TPS = Cast<ATabletopPlayerState>(PS))
    {
        if (const UEnum* E = StaticEnum<EFaction>())
            return E->GetDisplayNameTextByValue((int64)TPS->SelectedFaction).ToString();
    }
    return TEXT("");
}

void UGameplayWidget::RefreshTopBar()
{
    AMatchGameState* S = GS();
    if (!S) return;

    // Names/factions/scores
    if (P1Name)    P1Name->SetText(FText::FromString(NiceName(S->P1)));
    if (P1Faction) P1Faction->SetText(FText::FromString(FactionName(S->P1)));
    if (P1Score)   P1Score->SetText(FText::AsNumber(S->ScoreP1));

    if (P2Name)    P2Name->SetText(FText::FromString(NiceName(S->P2)));
    if (P2Faction) P2Faction->SetText(FText::FromString(FactionName(S->P2)));
    if (P2Score)   P2Score->SetText(FText::AsNumber(S->ScoreP2));

    if (RoundLabel)
        RoundLabel->SetText(FText::FromString(FString::Printf(TEXT("Round %d / %d"), (int)S->CurrentRound, (int)S->MaxRounds)));

    // Phase tag under active player only
    auto PhaseToText = [](ETurnPhase P)
    {
        switch (P)
        {
        case ETurnPhase::Move:  return TEXT("Movement");
        case ETurnPhase::Shoot: return TEXT("Shooting");
        default:                return TEXT("");
        }
    };

    const bool P1Active = (S->CurrentTurn == S->P1);
    if (P1PhaseTag) P1PhaseTag->SetText(P1Active ? FText::FromString(PhaseToText(S->TurnPhase)) : FText::GetEmpty());
    if (P2PhaseTag) P2PhaseTag->SetText(!P1Active ? FText::FromString(PhaseToText(S->TurnPhase)) : FText::GetEmpty());
}

void UGameplayWidget::RefreshBottom()
{
    AMatchGameState* S = GS();
    APlayerController* OPC = GetOwningPlayer();
    if (!S || !OPC || !OPC->PlayerState) return;

    const bool bMyTurn = (S->CurrentTurn == OPC->PlayerState);
    const bool bIsLastPhase = (S->TurnPhase == ETurnPhase::Shoot);

    if (NextBtn)      NextBtn->SetIsEnabled(bMyTurn && S->Phase == EMatchPhase::Battle);
    if (NextBtnLabel) NextBtnLabel->SetText(FText::FromString(bIsLastPhase ? TEXT("End Turn") : TEXT("Next Phase")));


    // (Context panel content comes later; for now we only wire the button)
}

void UGameplayWidget::OnNextClicked()
{
    if (AMatchPlayerController* PC = MPC())
        PC->Server_EndPhase();
}