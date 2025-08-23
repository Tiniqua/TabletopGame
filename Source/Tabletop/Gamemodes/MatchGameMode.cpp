
#include "MatchGameMode.h"

#include "EngineUtils.h"
#include "SetupGamemode.h"
#include "Net/UnrealNetwork.h"
#include "Tabletop/Actors/DeploymentZone.h"
#include "Tabletop/Actors/UnitBase.h"
#include "Tabletop/Characters/TabletopCharacter.h"
#include "Tabletop/Controllers/MatchPlayerController.h"
#include "Tabletop/PlayerStates/TabletopPlayerState.h"

namespace
{
    FORCEINLINE float CmToInches(float Cm) { return Cm / 2.54f; }
    FORCEINLINE float DistInches(const FVector& A, const FVector& B) { return FVector::Dist(A, B) / 2.54f; }

    // When moving into melee, keep a little gap to avoid direct overlap. Tweak to your base size.
    constexpr float BaseToBaseGapCm = 50.f; // ~19.7 inches if using big base; adjust as needed
}


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

void AMatchGameMode::ResetTurnFor(APlayerState* PS)
{
    for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
    {
        if (It->OwningPS == PS)
        {
            It->MoveBudgetInches = It->MoveMaxInches;
            It->bHasShot = false;
            It->bChargeAttempted = false;
            // It->bEngaged stays true/false based on melee state
            It->ForceNetUpdate();
        }
    }
}

bool AMatchGameMode::ValidateMove(AUnitBase* U, const FVector& Dest, float& OutSpentTabletopInches) const
{
    OutSpentTabletopInches = 0.f;
    if (!U) return false;

    UWorld* World = GetWorld();
    const FVector Start = U->GetActorLocation();

    // Convert UE centimeters → tabletop inches using the global scale
    const float distCm   = FVector::Dist(Start, Dest);
    const float cmPerTTI = CmPerTabletopInch();              // e.g. 50.8 or 50.0
    const float distTTIn = distCm / cmPerTTI;                // “tabletop inches”
    OutSpentTabletopInches = distTTIn;

    const bool bAllowed = (distTTIn <= U->MoveBudgetInches);

#if !(UE_BUILD_SHIPPING)
    const FColor Col = bAllowed ? FColor::Green : FColor::Red;

    if (World)
    {
        DrawDebugSphere(World, Start, 25.f, 16, Col, false, 10.f);
        DrawDebugSphere(World, Dest,  25.f, 16, Col, false, 10.f);
        DrawDebugLine  (World, Start, Dest, Col, false, 10.f, 0, 2.f);
    }
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            -1, 10.f, Col,
            FString::Printf(TEXT("[MoveCheck] Unit=%s  Dist=%.0f cm (%.1f TT-in)  Budget=%.1f TT-in  Max=%.1f TT-in  Scale=%.2f UE-in/TT-in (%.2f cm/TT-in)  -> %s"),
                *U->GetName(), distCm, distTTIn, U->MoveBudgetInches, U->MoveMaxInches,
                TabletopToUnrealInchScale, cmPerTTI,
                bAllowed ? TEXT("ALLOW") : TEXT("DENY")));
    }
#endif

    return bAllowed;
}

void AMatchGameMode::Handle_MoveUnit(AMatchPlayerController* PC, AUnitBase* Unit, const FVector& Dest)
{
    if (!HasAuthority() || !PC || !Unit) return;

    // Only during Move phase and only owner may move (your existing guards)
    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle || S->TurnPhase != ETurnPhase::Move) return;
    if (PC->PlayerState != S->CurrentTurn) return;
    if (Unit->OwningPS != PC->PlayerState) return;

    float spentTTIn = 0.f;
    if (!ValidateMove(Unit, Dest, spentTTIn))
    {
        // Over budget → tell the initiating client to deselect
        if (IsValid(PC))
        {
            PC->Client_OnMoveDenied_OverBudget(Unit, spentTTIn, Unit->MoveBudgetInches);
        }
        return;
    }

    // Spend the tabletop-inches budget and move
    Unit->MoveBudgetInches = FMath::Max(0.f, Unit->MoveBudgetInches - spentTTIn);
    Unit->SetActorLocation(Dest);
    Unit->ForceNetUpdate();

#if !(UE_BUILD_SHIPPING)
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            -1, 5.f, FColor::Yellow,
            FString::Printf(TEXT("[MoveApply] %s  Spent=%.1f TT-in  NewBudget=%.1f TT-in"),
                *Unit->GetName(), spentTTIn, Unit->MoveBudgetInches));
    }
#endif

    // Tell the initiating client to clear selection
    if (IsValid(PC))
    {
        PC->Client_OnUnitMoved(Unit, spentTTIn, Unit->MoveBudgetInches);
    }

}



bool AMatchGameMode::ValidateShoot(AUnitBase* Attacker, AUnitBase* Target) const
{
    if (!Attacker || !Target) return false;
    if (Attacker == Target)   return false;
    if (Attacker->ModelsCurrent <= 0 || Target->ModelsCurrent <= 0) return false;

    // Ownership: cannot shoot friendlies
    if (Attacker->OwningPS == Target->OwningPS) return false;

    // One shot per unit per turn
    if (Attacker->bHasShot) return false;

    const float dist = DistInches(Attacker->GetActorLocation(), Target->GetActorLocation());
    const float rng  = Attacker->GetWeaponRange(); // implement in AUnitBase from weapon profile
    if (rng <= 0.f) return false;

    return dist <= rng;
}

void AMatchGameMode::Handle_SelectTarget(AMatchPlayerController* PC, AUnitBase* Attacker, AUnitBase* Target)
{
    if (!HasAuthority() || !PC || !Attacker) return;

    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle || S->TurnPhase != ETurnPhase::Shoot) return;

    // Only current player, and they can only select a target for their own attacker
    if (PC->PlayerState != S->CurrentTurn) return;
    if (Attacker->OwningPS != PC->PlayerState) return;

    // Null target → treat as cancel
    if (!Target || !ValidateShoot(Attacker, Target))
    {
        if (S->Preview.Attacker == Attacker)
        {
            S->Preview.Attacker = nullptr;
            S->Preview.Target   = nullptr;
            S->Preview.Phase    = S->TurnPhase;
        }
        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
        return;
    }

    S->Preview.Attacker = Attacker;
    S->Preview.Target   = Target;
    S->Preview.Phase    = S->TurnPhase;

    S->OnDeploymentChanged.Broadcast();
    S->ForceNetUpdate();
}

void AMatchGameMode::Handle_ConfirmShoot(AMatchPlayerController* PC, AUnitBase* Attacker, AUnitBase* Target)
{
    if (!HasAuthority() || !PC || !Attacker || !Target) return;

    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle || S->TurnPhase != ETurnPhase::Shoot) return;
    if (PC->PlayerState != S->CurrentTurn) return;
    if (Attacker->OwningPS != PC->PlayerState) return;

    // Re-validate at confirm time
    if (!ValidateShoot(Attacker, Target)) return;

    // --- Minimal resolution stub ---
    // TODO: replace with proper to-hit / wound / save. For now, remove 1 model.
    const int32 ModelsLost = 1;
    Target->ModelsCurrent = FMath::Max(0, Target->ModelsCurrent - ModelsLost);
    Attacker->bHasShot = true;

    // Clear preview
    if (S->Preview.Attacker == Attacker)
    {
        S->Preview.Attacker = nullptr;
        S->Preview.Target   = nullptr;
        S->Preview.Phase    = S->TurnPhase;
    }

    // Disengage if wiped; additional logic can go here
    if (Target->ModelsCurrent <= 0)
    {
        Target->bEngaged = false;
        // (Optional) Destroy actor or leave carcass; up to you
    }

    Attacker->ForceNetUpdate();
    Target->ForceNetUpdate();
    S->OnDeploymentChanged.Broadcast();
    S->ForceNetUpdate();
}

void AMatchGameMode::Handle_CancelPreview(AMatchPlayerController* PC, AUnitBase* Attacker)
{
    if (!HasAuthority() || !PC || !Attacker) return;

    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle) return;

    // Only the active player, and only for their own unit
    if (PC->PlayerState != S->CurrentTurn) return;
    if (Attacker->OwningPS != PC->PlayerState) return;

    // Only clear if the current preview is for this attacker
    if (S->Preview.Attacker == Attacker)
    {
        S->Preview.Attacker = nullptr;
        S->Preview.Target   = nullptr;
        S->Preview.Phase    = S->TurnPhase; // keep phase consistent

        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
    }
}


bool AMatchGameMode::ValidateCharge(AUnitBase* Attacker, AUnitBase* Target) const
{
    if (!Attacker || !Target) return false;
    if (Attacker->OwningPS == Target->OwningPS) return false;
    if (Attacker->ModelsCurrent <= 0 || Target->ModelsCurrent <= 0) return false;

    // One attempt per turn
    if (Attacker->bChargeAttempted) return false;

    // Simple range gate — replace with Attacker->GetChargeRange() if you add it
    constexpr float ChargeRangeInches = 12.f;
    const float dist = DistInches(Attacker->GetActorLocation(), Target->GetActorLocation());
    return dist <= ChargeRangeInches;
}

void AMatchGameMode::Handle_AttemptCharge(AMatchPlayerController* PC, AUnitBase* Attacker, AUnitBase* Target)
{
    if (!HasAuthority() || !PC || !Attacker || !Target) return;

    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle || S->TurnPhase != ETurnPhase::Charge) return;
    if (PC->PlayerState != S->CurrentTurn) return;
    if (Attacker->OwningPS != PC->PlayerState) return;

    const bool bValid = ValidateCharge(Attacker, Target);
    Attacker->bChargeAttempted = true;

    if (bValid)
    {
        // Move to base-to-base (simple straight-line approach with a gap)
        const FVector ToTarget = Target->GetActorLocation() - Attacker->GetActorLocation();
        const FVector Dir      = ToTarget.GetSafeNormal();
        const FVector Dest     = Target->GetActorLocation() - Dir * BaseToBaseGapCm;

        Attacker->SetActorLocation(Dest, /*bSweep=*/false, /*OutHit=*/nullptr, ETeleportType::TeleportPhysics);
        Attacker->bEngaged = true;
        Target->bEngaged   = true;
    }

    Attacker->ForceNetUpdate();
    Target->ForceNetUpdate();
    S->OnDeploymentChanged.Broadcast();
    S->ForceNetUpdate();
}

bool AMatchGameMode::ValidateFight(AUnitBase* Attacker, AUnitBase* Target) const
{
    if (!Attacker || !Target) return false;
    if (Attacker->OwningPS == Target->OwningPS) return false;
    if (Attacker->ModelsCurrent <= 0 || Target->ModelsCurrent <= 0) return false;

    // Must be in melee / engaged; also check a small distance
    const float dist = DistInches(Attacker->GetActorLocation(), Target->GetActorLocation());
    const bool bInMelee = (dist <= 2.f) || (Attacker->bEngaged && Target->bEngaged);
    return bInMelee;
}

void AMatchGameMode::Handle_Fight(AMatchPlayerController* PC, AUnitBase* Attacker, AUnitBase* Target)
{
    if (!HasAuthority() || !PC || !Attacker || !Target) return;

    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle || S->TurnPhase != ETurnPhase::Fight) return;
    if (PC->PlayerState != S->CurrentTurn) return;
    if (Attacker->OwningPS != PC->PlayerState) return;

    if (!ValidateFight(Attacker, Target)) return;

    // --- Minimal resolution stub ---
    // TODO: replace with proper melee math; remove 1 model for now.
    Target->ModelsCurrent = FMath::Max(0, Target->ModelsCurrent - 1);

    if (Target->ModelsCurrent <= 0)
    {
        Target->bEngaged = false;
        Attacker->bEngaged = false;
    }

    Attacker->ForceNetUpdate();
    Target->ForceNetUpdate();
    S->OnDeploymentChanged.Broadcast();
    S->ForceNetUpdate();
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

void AMatchGameMode::ResetMoveBudgetsFor(APlayerState* TurnOwner)
{
    if (!HasAuthority() || !TurnOwner) return;

    UWorld* World = GetWorld();
    for (TActorIterator<AUnitBase> It(World); It; ++It)
    {
        if (AUnitBase* U = *It)
        {
            if (U->OwningPS == TurnOwner)
            {
                U->MoveBudgetInches = U->MoveMaxInches;   // reset to full budget for this move phase
                U->ForceNetUpdate();
#if !(UE_BUILD_SHIPPING)
                if (GEngine)
                {
                    GEngine->AddOnScreenDebugMessage(
                        -1, 5.f, FColor::Cyan,
                        FString::Printf(TEXT("[MoveReset] %s Budget=%.1f\" (Max=%.1f\")"),
                            *U->GetName(), U->MoveBudgetInches, U->MoveMaxInches));
                }
#endif
            }
        }
    }
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
        // Must be your turn to deploy
        if (PC->PlayerState.Get() != S->CurrentDeployer) return;

        if (!CanDeployAt(PC, Where.GetLocation()))
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy denied: outside deployment zone or not your turn."));
            return;
        }

        // Validate & decrement from remaining pool
        if (!DecrementOne(PC->PlayerState.Get(), UnitId))
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy denied: unit %s not available in remaining roster."), *UnitId.ToString());
            return;
        }

        // ---- Resolve the unit row from the player's faction table ----
        const ATabletopPlayerState* TPS = Cast<ATabletopPlayerState>(PC->PlayerState.Get());
        if (!TPS)
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy denied: PlayerState is not ATabletopPlayerState."));
            return;
        }

        UDataTable* UnitsDT = UnitsForFaction(TPS->SelectedFaction);
        if (!UnitsDT)
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy denied: No units DataTable for faction %d."), (int32)TPS->SelectedFaction);
            return;
        }

        const FUnitRow* Row = UnitsDT->FindRow<FUnitRow>(UnitId, TEXT("HandleRequestDeploy"));
        if (!Row)
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy denied: UnitId %s not found in faction table."), *UnitId.ToString());
            return;
        }

        // ---- Spawn the actor (class already resolved by faction/unit) ----
        TSubclassOf<AActor> SpawnClass = UnitClassFor(PC->PlayerState.Get(), UnitId);
        if (!*SpawnClass)
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy warning: UnitClassFor returned null for %s; defaulting to AActor."), *UnitId.ToString());
            SpawnClass = AActor::StaticClass();
        }

        FActorSpawnParameters Params;
        Params.Owner = PC;
        AActor* Spawned = GetWorld()->SpawnActor<AActor>(SpawnClass, Where, Params);
        if (!Spawned)
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy failed: spawn returned null for %s."), *UnitId.ToString());
            return;
        }

        // ---- Initialize runtime state if it's a UnitBase ----
        if (AUnitBase* UB = Cast<AUnitBase>(Spawned))
        {
            // Use DefaultWeaponIndex from the row (validated inside Server_InitFromRow if needed)
            UB->Server_InitFromRow(PC->PlayerState.Get(), *Row, Row->DefaultWeaponIndex);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy note: Spawned class %s is not AUnitBase for %s; skipping runtime init."),
                   *SpawnClass->GetName(), *UnitId.ToString());
        }

        // ---- Alternate or keep turn based on remaining pools ----
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
        ResetMoveBudgetsFor(S->CurrentTurn);
        
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
        ResetMoveBudgetsFor(S->CurrentTurn);
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
        ResetMoveBudgetsFor(S->CurrentTurn);
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

