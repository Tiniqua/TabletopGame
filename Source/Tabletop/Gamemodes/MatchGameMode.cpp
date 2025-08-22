
#include "MatchGameMode.h"

#include "SetupGamemode.h"
#include "Net/UnrealNetwork.h"
#include "Tabletop/Actors/DeploymentZone.h"
#include "Tabletop/Characters/TabletopCharacter.h"
#include "Tabletop/Controllers/MatchPlayerController.h"
#include "Tabletop/PlayerStates/TabletopPlayerState.h"


void AMatchGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AMatchGameState, Phase);
    DOREPLIFETIME(AMatchGameState, CurrentDeployer);
    DOREPLIFETIME(AMatchGameState, P1Remaining);
    DOREPLIFETIME(AMatchGameState, P2Remaining);
    DOREPLIFETIME(AMatchGameState, bDeploymentComplete);
    DOREPLIFETIME(AMatchGameState, P1);
    DOREPLIFETIME(AMatchGameState, P2);
    DOREPLIFETIME(AMatchGameState, bTeamsAndTurnsInitialized);
    DOREPLIFETIME(AMatchGameState, CurrentRound);
    DOREPLIFETIME(AMatchGameState, MaxRounds);
    DOREPLIFETIME(AMatchGameState, TurnInRound);
    DOREPLIFETIME(AMatchGameState, TurnPhase);
    DOREPLIFETIME(AMatchGameState, CurrentTurn);
    DOREPLIFETIME(AMatchGameState, ScoreP1);
    DOREPLIFETIME(AMatchGameState, ScoreP2);
    
}

AMatchGameMode::AMatchGameMode()
{
    GameStateClass = AMatchGameState::StaticClass();
    PlayerStateClass   = ATabletopPlayerState::StaticClass();
    DefaultPawnClass = ATabletopCharacter::StaticClass();
    bUseSeamlessTravel = true;
}

void AMatchGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
    Super::HandleSeamlessTravelPlayer(C);
    if (!C) return;
    
    FinalizePlayerJoin(Cast<APlayerController>(C)); 
}

void AMatchGameMode::BeginPlay()
{
    Super::BeginPlay();
}

void AMatchGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    
    if (AMatchGameState* S = GS())
    {
        if (!S->P1)      S->P1 = NewPlayer->PlayerState;
        else if (!S->P2) S->P2 = NewPlayer->PlayerState;

        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
    }
    
    FinalizePlayerJoin(NewPlayer); // leave the heavy init here

}

APlayerState* AMatchGameMode::OtherPlayer(APlayerState* PS) const
{
    if (const AMatchGameState* S = GS())
        return (PS == S->P1) ? S->P2 : S->P1;
    return nullptr;
}

int32 AMatchGameMode::FindIdx(TArray<FUnitCount>& Arr, FName Unit)
{
    for (int32 i=0;i<Arr.Num();++i) if (Arr[i].UnitId == Unit) return i;
    return INDEX_NONE;
}

bool AMatchGameMode::CanDeployAt(APlayerController* PC, const FVector& Location) const
{
    if (!PC) return false;

    const AMatchGameState* S = GetGameState<AMatchGameState>();
    if (!S || S->Phase != EMatchPhase::Deployment) return false;

    // (Optional) enforce alternating: it must be this PC’s turn
    if (S->CurrentDeployer && PC->PlayerState != S->CurrentDeployer)
        return false;

    // Team gate
    const ATabletopPlayerState* TPS = PC->GetPlayerState<ATabletopPlayerState>();
    const int32 Team = TPS ? TPS->TeamNum : 0;
    if (Team <= 0) return false;

    return ADeploymentZone::IsLocationAllowedForTeam(GetWorld(), Team, Location);
}


void AMatchGameMode::CopyRostersFromPlayerStates()
{
    if (AMatchGameState* S = GS())
    {
        S->P1Remaining.Empty();
        S->P2Remaining.Empty();

        if (auto* P1PS = Cast<ATabletopPlayerState>(S->P1))
            S->P1Remaining = P1PS->Roster;

        if (auto* P2PS = Cast<ATabletopPlayerState>(S->P2))
            S->P2Remaining = P2PS->Roster;
    }
}

bool AMatchGameMode::AnyRemainingFor(APlayerState* PS) const
{
    if (const AMatchGameState* S = GS())
    {
        const bool bIsP1 = (PS == S->P1);
        const TArray<FUnitCount>& R = bIsP1 ? S->P1Remaining : S->P2Remaining;
        for (const FUnitCount& E : R) if (E.Count > 0) return true;
    }
    return false;
}

bool AMatchGameMode::DecrementOne(APlayerState* PS, FName UnitId)
{
    if (AMatchGameState* S = GS())
    {
        const bool bIsP1 = (PS == S->P1);
        TArray<FUnitCount>& R = bIsP1 ? S->P1Remaining : S->P2Remaining;
        if (int32 Idx = FindIdx(R, UnitId); Idx != INDEX_NONE && R[Idx].Count > 0)
        {
            R[Idx].Count -= 1;
            if (R[Idx].Count <= 0) R.RemoveAt(Idx);
            return true;
        }
    }
    return false;
}

UDataTable* AMatchGameMode::UnitsForFaction(EFaction Faction) const
{
    const AMatchGameState* S = GS();
    if (!S || !FactionsTable) return nullptr;
    for (const auto& Pair : FactionsTable->GetRowMap())
    {
        if (const FFactionRow* FR = reinterpret_cast<const FFactionRow*>(Pair.Value))
            if (FR->Faction == Faction) return FR->UnitsTable;
    }
    return nullptr;
}

TSubclassOf<AActor> AMatchGameMode::UnitClassFor(APlayerState* ForPS, FName UnitId) const
{
    const TSubclassOf<AActor> DefaultClass = AActor::StaticClass();

    const ATabletopPlayerState* TPS = ForPS ? Cast<ATabletopPlayerState>(ForPS) : nullptr;
    if (!TPS) return DefaultClass;

    UDataTable* Units = UnitsForFaction(TPS->SelectedFaction);
    if (!Units) return DefaultClass;

    if (const FUnitRow* Row = Units->FindRow<FUnitRow>(UnitId, TEXT("UnitClassFor")))
    {
        return Row->UnitActorClass ? Row->UnitActorClass : DefaultClass;
    }
    return DefaultClass;
}

void AMatchGameMode::HandleRequestDeploy(APlayerController* PC, FName UnitId, const FTransform& Where)
{
    if (!HasAuthority() || !PC) return;
    if (AMatchGameState* S = GS())
    {
        // Compare raw pointers
        if (PC->PlayerState.Get() != S->CurrentDeployer) return; // not your turn

        if (!CanDeployAt(PC, Where.GetLocation()))
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy denied: outside deployment zone or not your turn."));
            // (Optional) tell client so UI can flash red, etc.
            // PC->Client_DeployDenied();
            return;
        }

        // Validate & decrement from remaining
        if (!DecrementOne(PC->PlayerState.Get(), UnitId)) return;

        // Spawn class lookup
        TSubclassOf<AActor> SpawnClass = UnitClassFor(PC->PlayerState.Get(), UnitId);
        if (!*SpawnClass) { SpawnClass = AActor::StaticClass(); }

        FActorSpawnParameters Params; Params.Owner = PC;
        GetWorld()->SpawnActor<AActor>(SpawnClass, Where, Params);

        // Alternate or keep turn
        APlayerState* Other = (PC->PlayerState.Get() == S->P1) ? S->P2 : S->P1;
        const bool bSelfLeft  = AnyRemainingFor(PC->PlayerState.Get());
        const bool bOtherLeft = AnyRemainingFor(Other);

        if (!bSelfLeft && !bOtherLeft)
        {
            FinishDeployment();
            return;
        }

        S->CurrentDeployer = bOtherLeft ? Other : PC->PlayerState.Get();

        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
    }
}

void AMatchGameMode::FinishDeployment()
{
    if (AMatchGameState* S = GS())
    {
        S->bDeploymentComplete = true;
        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
    }
}

void AMatchGameMode::HandleStartBattle(APlayerController* PC)
{
    if (!HasAuthority() || !PC) return;
    if (AMatchGameState* S = GS())
    {
        if (PC->PlayerState != S->P1) return; // host-only
        if (!S->bDeploymentComplete)  return;

        S->Phase = EMatchPhase::Battle;
        S->CurrentRound= 1;
        S->MaxRounds   = 5;
        S->TurnInRound = 0;
        S->TurnPhase   = ETurnPhase::Move;

        // TODO Random??
        S->CurrentTurn = S->CurrentDeployer;
        
        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();

        // Kick both clients to refresh (optional)
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
            if (AMatchPlayerController* MPC = Cast<AMatchPlayerController>(*It))
                MPC->Client_KickPhaseRefresh();
    }
}

void AMatchGameMode::HandleEndPhase(APlayerController* PC)
{
    if (!HasAuthority() || !PC) return;
    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle) return;

    // Only the active player may advance
    if (PC->PlayerState != S->CurrentTurn) return;

    auto Broadcast = [&]()
    {
        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
    };

    // Phase step
    if (S->TurnPhase != ETurnPhase::Fight)
    {
        // Move -> Shoot -> Charge -> Fight
        switch (S->TurnPhase)
        {
        case ETurnPhase::Move:   S->TurnPhase = ETurnPhase::Shoot;  break;
        case ETurnPhase::Shoot:  S->TurnPhase = ETurnPhase::Charge; break;
        case ETurnPhase::Charge: S->TurnPhase = ETurnPhase::Fight;  break;
        default: break;
        }
        Broadcast();
        return;
    }

    // End of Fight -> end of this player's turn
    if (S->TurnInRound == 0)
    {
        // Switch to other player, start at Move
        S->TurnInRound = 1;
        S->CurrentTurn = OtherPlayer(S->CurrentTurn);
        S->TurnPhase   = ETurnPhase::Move;
        Broadcast();
    }
    else
    {
        // Finished both turns in this round
        S->CurrentRound = FMath::Clamp<uint8>(S->CurrentRound + 1, 1, S->MaxRounds);
        S->TurnInRound  = 0;

        if (S->CurrentRound > S->MaxRounds)
        {
            S->Phase = EMatchPhase::EndGame;
            Broadcast();
            return;
        }

        // Next round starts with the other player than who just ended
        S->CurrentTurn = OtherPlayer(S->CurrentTurn);
        S->TurnPhase   = ETurnPhase::Move;
        Broadcast();
    }
}


void AMatchGameMode::FinalizePlayerJoin(APlayerController* PC)
{
    if (!PC) return;
    if (AMatchGameState* S = GS())
    {
        // Make sure P1/P2 are set (idempotent)
        if (!S->P1) S->P1 = PC->PlayerState;
        else if (!S->P2 && S->P1 != PC->PlayerState) S->P2 = PC->PlayerState;

        // One-time init only when both present and not yet initialized
        if (S->P1 && S->P2 && !S->bTeamsAndTurnsInitialized)
        {
            // Copy rosters once
            CopyRostersFromPlayerStates();

            // Randomize or keep your existing function:
            const bool bP1First = FMath::RandBool();
            S->CurrentDeployer = bP1First ? S->P1 : S->P2;

            if (ATabletopPlayerState* P1PS = Cast<ATabletopPlayerState>(S->P1))
                P1PS->TeamNum = bP1First ? 1 : 2;
            if (ATabletopPlayerState* P2PS = Cast<ATabletopPlayerState>(S->P2))
                P2PS->TeamNum = bP1First ? 2 : 1;

            S->Phase = EMatchPhase::Deployment;
            S->bTeamsAndTurnsInitialized = true;

            if (ATabletopPlayerState* P1PS = Cast<ATabletopPlayerState>(S->P1)) P1PS->ForceNetUpdate();
            if (ATabletopPlayerState* P2PS = Cast<ATabletopPlayerState>(S->P2)) P2PS->ForceNetUpdate();
            S->ForceNetUpdate();
        }

        // Let clients refresh UI when any of the replicated props land
        S->OnDeploymentChanged.Broadcast();
    }

    if (AMatchPlayerController* MPC = Cast<AMatchPlayerController>(PC))
    {
        MPC->Client_KickUIRefresh();
    }
}

