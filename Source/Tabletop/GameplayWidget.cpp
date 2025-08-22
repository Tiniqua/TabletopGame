
#include "GameplayWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Controllers/MatchPlayerController.h"
#include "Gamemodes/MatchGameMode.h"
#include "PlayerStates/TabletopPlayerState.h"

AMatchGameState* UGameplayWidget::GS() const { return GetWorld()? GetWorld()->GetGameState<AMatchGameState>() : nullptr; }
AMatchPlayerController* UGameplayWidget::MPC() const { return GetOwningPlayer<AMatchPlayerController>(); }

void UGameplayWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (AMatchGameState* S = GS())
    {
        S->OnDeploymentChanged.AddDynamic(this, &UGameplayWidget::OnMatchChanged);
        BoundGS = S;
    }

    if (NextBtn) NextBtn->OnClicked.AddDynamic(this, &UGameplayWidget::OnNextClicked);

    RefreshTopBar();
    RefreshBottom();
}

void UGameplayWidget::NativeDestruct()
{
    if (BoundGS.IsValid())
    {
        BoundGS->OnDeploymentChanged.RemoveDynamic(this, &UGameplayWidget::OnMatchChanged);
        BoundGS.Reset();
    }
    Super::NativeDestruct();
}

void UGameplayWidget::OnMatchChanged()
{
    RefreshTopBar();
    RefreshBottom();
}

static FString NiceName(APlayerState* PS)
{
    if (const ATabletopPlayerState* TPS = Cast<ATabletopPlayerState>(PS))
        return TPS->DisplayName.IsEmpty() ? PS->GetPlayerName() : TPS->DisplayName;
    return PS ? PS->GetPlayerName() : TEXT("");
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
        case ETurnPhase::Move:   return TEXT("Movement");
        case ETurnPhase::Shoot:  return TEXT("Shooting");
        case ETurnPhase::Charge: return TEXT("Charge");
        case ETurnPhase::Fight:  return TEXT("Fight");
        default:                 return TEXT("");
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
    const bool bIsFight = (S->TurnPhase == ETurnPhase::Fight);

    if (NextBtn)       NextBtn->SetIsEnabled(bMyTurn && S->Phase == EMatchPhase::Battle);
    if (NextBtnLabel)  NextBtnLabel->SetText(FText::FromString(bIsFight ? TEXT("End Turn") : TEXT("Next Phase")));

    // (Context panel content comes later; for now we only wire the button)
}

void UGameplayWidget::OnNextClicked()
{
    if (AMatchPlayerController* PC = MPC())
        PC->Server_EndPhase();
}