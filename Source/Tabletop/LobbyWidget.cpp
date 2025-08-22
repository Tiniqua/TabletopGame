
#include "LobbyWidget.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Controllers/SetupPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Gamemodes/SetupGamemode.h"

void ULobbyWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (ASetupGameState* GS = GetSetupGS())
    {
        GS->OnPlayerSlotsChanged.AddDynamic(this, &ULobbyWidget::RefreshFromState);
    }
    
    if (P1ReadyBtn)   P1ReadyBtn->OnClicked.AddDynamic(this, &ULobbyWidget::OnP1ReadyClicked);
    if (P2ReadyBtn)   P2ReadyBtn->OnClicked.AddDynamic(this, &ULobbyWidget::OnP2ReadyClicked);
    if (BothReady)    BothReady->OnClicked.AddDynamic(this, &ULobbyWidget::OnBothReadyClicked);

    // Names should be read-only (UI just displays)
    if (P1Name) P1Name->SetIsReadOnly(true);
    if (P2Name) P2Name->SetIsReadOnly(true);

    RefreshFromState();
}

void ULobbyWidget::NativeDestruct()
{
    Super::NativeDestruct();
    // (No dynamic unbind needed for buttons; safe to leave)
}

void ULobbyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    RefreshFromState();  // simple & reliable for MVP; you can switch to events later
}

ASetupGameState* ULobbyWidget::GetSetupGS() const
{
    return GetWorld() ? GetWorld()->GetGameState<ASetupGameState>() : nullptr;
}

ASetupPlayerController* ULobbyWidget::GetSetupPC() const
{
    return GetOwningPlayer<ASetupPlayerController>();
}

void ULobbyWidget::RefreshFromState()
{
    ASetupGameState* S = GetSetupGS();
    ASetupPlayerController* PC = GetSetupPC();
    if (!S || !PC) return;

    // Defensive null-checks
    FString P1 = TEXT("---");
    FString P2 = TEXT("---");

    if (S->Player1)
    {
        const FString Name = S->Player1->GetPlayerName();
        P1 = Name.IsEmpty() ? TEXT("Waiting...") : Name;
    }

    if (S->Player2)
    {
        const FString Name = S->Player2->GetPlayerName();
        P2 = Name.IsEmpty() ? TEXT("Waiting...") : Name;
    }

    if (P1Name) P1Name->SetText(FText::FromString(P1));
    if (P2Name) P2Name->SetText(FText::FromString(P2));

    // Enable/disable buttons as before
    APlayerState* LocalPS = PC->PlayerState;
    const bool bLocalIsP1 = (LocalPS && LocalPS == S->Player1);
    const bool bLocalIsP2 = (LocalPS && LocalPS == S->Player2);

    if (P1ReadyBtn) P1ReadyBtn->SetIsEnabled(bLocalIsP1);
    if (P2ReadyBtn) P2ReadyBtn->SetIsEnabled(bLocalIsP2);

    const bool bHostSeat = bLocalIsP1;
    if (BothReady) BothReady->SetIsEnabled(bHostSeat && S->bP1Ready && S->bP2Ready && S->Phase == ESetupPhase::Lobby);
}

void ULobbyWidget::ApplySeatPermissions()
{
    if (ASetupGameState* S = GetSetupGS())
    {
        ASetupPlayerController* PC = GetSetupPC();
        if (!PC) return;
        APlayerState* LocalPS = PC->PlayerState;

        const bool bLocalIsP1 = (LocalPS && LocalPS == S->Player1);
        const bool bLocalIsP2 = (LocalPS && LocalPS == S->Player2);

        if (P1ReadyBtn) P1ReadyBtn->SetIsEnabled(bLocalIsP1);
        if (P2ReadyBtn) P2ReadyBtn->SetIsEnabled(bLocalIsP2);
    }
}

void ULobbyWidget::OnP1ReadyClicked()
{
    if (ASetupGameState* S = GetSetupGS())
        if (ASetupPlayerController* PC = GetSetupPC())
            if (PC->PlayerState == S->Player1)
                PC->Server_SetReady(!S->bP1Ready);
}

void ULobbyWidget::OnP2ReadyClicked()
{
    if (ASetupGameState* S = GetSetupGS())
        if (ASetupPlayerController* PC = GetSetupPC())
            if (PC->PlayerState == S->Player2)
                PC->Server_SetReady(!S->bP2Ready);
}

void ULobbyWidget::OnBothReadyClicked()
{
    if (ASetupGameState* S = GetSetupGS())
        if (ASetupPlayerController* PC = GetSetupPC())
            if (PC->PlayerState == S->Player1 && S->bP1Ready && S->bP2Ready)
                PC->Server_AdvanceFromLobby();
}