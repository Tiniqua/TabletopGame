
#include "SetupGamemode.h"

#include "GameFramework/PlayerState.h"
#include "GameFramework/SpectatorPawn.h"
#include "Tabletop/MapData.h"
#include "Tabletop/PlayerStates/TabletopPlayerState.h"

static FString SafeName(APlayerState* PS)
{
    if (!PS) return FString();
    FString N = PS->GetPlayerName();
    if (N.IsEmpty())
    {
        if (const ATabletopPlayerState* TPS = Cast<ATabletopPlayerState>(PS))
        {
            N = TPS->DisplayName;
        }
    }
    return N.IsEmpty() ? TEXT("Player") : N;
}

void ASetupGameState::OnRep_Phase()
{
    OnPhaseChanged.Broadcast();
}

void ASetupGameState::OnRep_ArmySelection()
{
    OnArmySelectionChanged.Broadcast();
}

void ASetupGameState::OnRep_PlayerSlots()
{
    OnPlayerSlotsChanged.Broadcast();
}

void ASetupGameState::OnRep_Rosters()
{
    OnRosterChanged.Broadcast();
}

void ASetupGameState::OnRep_SelectedMap()
{
    OnMapSelectionChanged.Broadcast();
}

void ASetupGameState::OnRep_ReadyUp()
{
    OnPlayerReadyUp.Broadcast();
}

void ASetupGamemode::UpdateCachedPlayerNames()
{
    if (ASetupGameState* S = GS())
    {
        const auto SafeName = [](APlayerState* PS){ return PS ? PS->GetPlayerName() : FString(); };

        S->Player1Name = CleanAndClampName(SafeName(S->Player1), MaxPlayerNameChars);
        S->Player2Name = CleanAndClampName(SafeName(S->Player2), MaxPlayerNameChars);

        S->OnPlayerSlotsChanged.Broadcast();
        S->ForceNetUpdate();
    }
}

void ASetupGameState::OnRep_PlayerNames()
{
    OnPlayerSlotsChanged.Broadcast();
}

static int32 FindIndexByUnit(const TArray<FUnitCount>& Roster, FName UnitId)
{
    for (int32 i=0; i<Roster.Num(); ++i)
        if (Roster[i].UnitId == UnitId) return i;
    return INDEX_NONE;
}

static void SetCount(TArray<FUnitCount>& Roster, FName UnitId, int32 NewCount)
{
    const int32 Idx = FindIndexByUnit(Roster, UnitId);
    if (NewCount <= 0)
    {
        if (Idx != INDEX_NONE) Roster.RemoveAt(Idx);
        return;
    }
    if (Idx == INDEX_NONE) { FUnitCount E; E.UnitId=UnitId; E.Count=NewCount; Roster.Add(E); }
    else                   { Roster[Idx].Count = NewCount; }
}

static int32 GetCount(const TArray<FUnitCount>& Roster, FName UnitId)
{
    const int32 Idx = FindIndexByUnit(Roster, UnitId);
    return (Idx != INDEX_NONE) ? Roster[Idx].Count : 0;
}

void ASetupGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ASetupGameState, Player1);
    DOREPLIFETIME(ASetupGameState, Player2);
    DOREPLIFETIME_CONDITION_NOTIFY(ASetupGameState, Player1Name, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(ASetupGameState, Player2Name, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME(ASetupGameState, bP1Ready);
    DOREPLIFETIME(ASetupGameState, bP2Ready);
    DOREPLIFETIME(ASetupGameState, Phase);
    DOREPLIFETIME(ASetupGameState, SelectedMap);
    DOREPLIFETIME(ASetupGameState, P1Faction);
    DOREPLIFETIME(ASetupGameState, P2Faction);
    DOREPLIFETIME(ASetupGameState, FactionsTable);
    DOREPLIFETIME(ASetupGameState, P1Roster);
    DOREPLIFETIME(ASetupGameState, P2Roster);
    DOREPLIFETIME(ASetupGameState, P1Points);
    DOREPLIFETIME(ASetupGameState, P2Points);
    DOREPLIFETIME(ASetupGameState, MapsTable);
    DOREPLIFETIME(ASetupGameState, SelectedMapRow);
}

ASetupGamemode::ASetupGamemode()
{
    PlayerStateClass   = ATabletopPlayerState::StaticClass();
    GameStateClass = ASetupGameState::StaticClass();
    bUseSeamlessTravel = true;
    DefaultPawnClass = ASpectatorPawn::StaticClass();
    bStartPlayersAsSpectators = true;
}

void ASetupGamemode::BeginPlay()
{
    Super::BeginPlay();
    if (ASetupGameState* S = GS())
    {
        UpdateCachedPlayerNames();
        S->FactionsTable = FactionsTable;
        S->MapsTable = MapsTable; 
        S->OnPhaseChanged.Broadcast();
        S->ForceNetUpdate();
    }
}

void ASetupGamemode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    if (ASetupGameState* S = GS())
    {
        // Assign seat
        if (!S->Player1)      S->Player1 = NewPlayer->PlayerState;
        else if (!S->Player2) S->Player2 = NewPlayer->PlayerState;

        // Clamp the PlayerState’s display name (replicates to everyone)
        if (APlayerState* PS = NewPlayer->PlayerState)
        {
            const FString Raw = PS->GetPlayerName();                  // PIE/Steam name
            const FString Clamped = CleanAndClampName(Raw, MaxPlayerNameChars);
            PS->SetPlayerName(Clamped);                               // authoritative, replicates
        }

        // (If you keep a custom DisplayName too)
        if (auto* TPS = NewPlayer->GetPlayerState<ATabletopPlayerState>())
        {
            TPS->DisplayName = CleanAndClampName(TPS->GetPlayerName(), MaxPlayerNameChars);
        }

        UpdateCachedPlayerNames();   // also clamps; see next step
        S->OnPhaseChanged.Broadcast();
        S->ForceNetUpdate();
    }
}

void ASetupGamemode::Logout(AController* Exiting)
{
    Super::Logout(Exiting);
    if (ASetupGameState* S = GS())
    {
        if (S->Player1 == Exiting->PlayerState) { S->Player1 = nullptr; S->bP1Ready = false; }
        if (S->Player2 == Exiting->PlayerState) { S->Player2 = nullptr; S->bP2Ready = false; }
        S->Phase = ESetupPhase::Lobby;
        S->OnArmySelectionChanged.Broadcast();
        S->ForceNetUpdate();
    }
}

bool ASetupGamemode::AllPlayersReady()
{
    const ASetupGameState* S = Cast<ASetupGameState>(GameState);
    return S && S->Player1 && S->Player2 && S->bP1Ready && S->bP2Ready;
}

UDataTable* ASetupGamemode::GetUnitsTableForFaction(EFaction Faction) const
{
    const ASetupGameState* S = GS();
    if (!S || !S->FactionsTable) return nullptr;

    for (const auto& Pair : S->FactionsTable->GetRowMap())
    {
        if (const FFactionRow* Row = reinterpret_cast<const FFactionRow*>(Pair.Value))
        {
            if (Row->Faction == Faction)
            {
                return Row->UnitsTable;
            }
        }
    }
    return nullptr;
}

void ASetupGamemode::HandleSelectMap(APlayerController* PC, FName MapRow)
{
    if (!HasAuthority() || !PC) return;
    if (ASetupGameState* S = GS())
    {
        const bool bIsHost = (PC->PlayerState == S->Player1);
        if (!bIsHost || !S->MapsTable) return;

        if (S->MapsTable->FindRow<FMapRow>(MapRow, TEXT("HandleSelectMap")))
        {
            S->SelectedMapRow = MapRow;
            S->OnMapSelectionChanged.Broadcast(); // host-side update
            S->ForceNetUpdate();
        }
    }
}

FString ASetupGamemode::CleanAndClampName(const FString& In, int32 MaxChars)
{
    FString S = In;
    S.TrimStartAndEndInline();
    // collapse control chars to spaces
    S.ReplaceInline(TEXT("\r"), TEXT(" "));
    S.ReplaceInline(TEXT("\n"), TEXT(" "));
    S.ReplaceInline(TEXT("\t"), TEXT(" "));
    if (S.IsEmpty()) S = TEXT("Player");
    if (MaxChars > 0 && S.Len() > MaxChars)
    {
        S = S.Left(MaxChars); // simple clamp; good enough for dev
    }
    return S;
}

void ASetupGamemode::TryAdvanceFromMapSelection()
{
    if (!HasAuthority()) return;
    if (ASetupGameState* S = GS())
    {
        // Persist per-player data into PlayerState (this survives travel)
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            if (APlayerController* PC = It->Get())
            {
                if (ATabletopPlayerState* TPS = PC->GetPlayerState<ATabletopPlayerState>())
                {
                    if (PC->PlayerState == S->Player1)
                    {
                        TPS->SelectedFaction = S->P1Faction;
                        TPS->Roster          = S->P1Roster;
                        UE_LOG(LogTemp, Log, TEXT("Copy P1 -> PS: Faction=%d, Roster=%d"), (int)TPS->SelectedFaction, TPS->Roster.Num());
                    }
                    else if (PC->PlayerState == S->Player2)
                    {
                        TPS->SelectedFaction = S->P2Faction;
                        TPS->Roster          = S->P2Roster;
                        UE_LOG(LogTemp, Log, TEXT("Copy P2 -> PS: Faction=%d, Roster=%d"), (int)TPS->SelectedFaction, TPS->Roster.Num());
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("PlayerState is NOT ATabletopPlayerState — will lose picks on travel!"));
                }
            }
        }

        if (S->Phase == ESetupPhase::MapSelection &&
            S->bP1Ready && S->bP2Ready &&
            !S->SelectedMapRow.IsNone() && S->MapsTable)
        {
            if (const FMapRow* Row = S->MapsTable->FindRow<FMapRow>(S->SelectedMapRow, TEXT("StartMatch")))
            {
                const FString URL = BuildListenURL(Row->Level);
                if (!URL.IsEmpty())
                {
                    // Clients will follow automatically
                    GetWorld()->ServerTravel(URL, /*bAbsolute*/ true);
                    return;
                }
                UE_LOG(LogTemp, Error, TEXT("Map row has empty/invalid Level path."));
            }
        }
    }
}

int32 ASetupGamemode::ComputeRosterPoints(UDataTable* UnitsTable, const TMap<FName,int32>& Roster) const
{
    if (!UnitsTable) return 0;
    int32 Total = 0;
    for (const auto& It : Roster)
    {
        const FUnitRow* Row = UnitsTable->FindRow<FUnitRow>(It.Key, TEXT("ComputeRosterPoints(Server)"));
        if (Row && It.Value > 0)
        {
            Total += Row->Points * It.Value;
        }
    }
    return Total;
}

void ASetupGamemode::HandleSetUnitCount(APlayerController* PC, FName UnitRow, int32 Count)
{
    if (!HasAuthority() || !PC) return;
    if (ASetupGameState* S = GS())
    {
        Count = FMath::Clamp(Count, 0, 99);

        const bool bIsP1 = (PC->PlayerState == S->Player1);
        const bool bIsP2 = (PC->PlayerState == S->Player2);
        if (!bIsP1 && !bIsP2) return;

        const EFaction Faction = bIsP1 ? S->P1Faction : S->P2Faction;
        UDataTable* Units = GetUnitsTableForFaction(Faction);
        if (!Units) return;

        if (!Units->FindRow<FUnitRow>(UnitRow, TEXT("HandleSetUnitCount"))) return;

        // Update GS roster & points
        TArray<FUnitCount>& R = bIsP1 ? S->P1Roster : S->P2Roster;
        SetCount(R, UnitRow, Count);

        int32& Points = bIsP1 ? S->P1Points : S->P2Points;
        Points = 0;
        for (const FUnitCount& E : R)
            if (const FUnitRow* Row = Units->FindRow<FUnitRow>(E.UnitId, TEXT("Recompute")))
                if (E.Count > 0) Points += Row->Points * E.Count;

        const int32 Cap = 2000;
        if (bIsP1 && S->bP1Ready && Points > Cap) S->bP1Ready = false;
        if (bIsP2 && S->bP2Ready && Points > Cap) S->bP2Ready = false;

        // Mirror into PlayerState (so it’s always up-to-date pre-travel)
        if (ATabletopPlayerState* TPS = PC->GetPlayerState<ATabletopPlayerState>())
        {
            TPS->Roster = R; // copy array of FUnitCount
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("HandleSetUnitCount: PlayerState is not ATabletopPlayerState"));
        }

        S->OnRosterChanged.Broadcast();
        S->OnPlayerReadyUp.Broadcast();
        S->ForceNetUpdate();
    }
}

void ASetupGamemode::TryAdvanceFromUnitSelection()
{
    if (!HasAuthority()) return;
    if (ASetupGameState* S = GS())
    {
        const int32 Cap = 2000;
        const bool bBothUnderCap = (S->P1Points <= Cap) && (S->P2Points <= Cap);
        if (S->Phase == ESetupPhase::UnitSelection && AllPlayersReady() && bBothUnderCap)
        {
            S->bP1Ready = S->bP2Ready = false;
            S->Phase = ESetupPhase::MapSelection;
            S->OnPhaseChanged.Broadcast(); // host
            S->ForceNetUpdate();
        }
    }
}

void ASetupGamemode::SnapshotPlayerToPS(APlayerController* PC)
{
    if (!HasAuthority() || !PC) return;
    if (ASetupGameState* S = GS())
        if (ATabletopPlayerState* TPS = PC->GetPlayerState<ATabletopPlayerState>())
        {
            if (PC->PlayerState == S->Player1)
            {
                TPS->SelectedFaction = S->P1Faction;
                TPS->Roster          = S->P1Roster;
            }
            else if (PC->PlayerState == S->Player2)
            {
                TPS->SelectedFaction = S->P2Faction;
                TPS->Roster          = S->P2Roster;
            }
            UE_LOG(LogTemp, Log, TEXT("Snapshot to PS: Faction=%d, Roster=%d"),
                   (int)TPS->SelectedFaction, TPS->Roster.Num());
        }
}


void ASetupGamemode::HandleSelectFaction(APlayerController* PC, EFaction Faction)
{
    if (!HasAuthority() || !PC) return;
    if (ASetupGameState* S = GS())
    {
        const bool bIsP1 = (PC->PlayerState == S->Player1);
        const bool bIsP2 = (PC->PlayerState == S->Player2);

        if (bIsP1) S->P1Faction = Faction;
        if (bIsP2) S->P2Faction = Faction;

        // Mirror into PlayerState too
        if (ATabletopPlayerState* TPS = PC->GetPlayerState<ATabletopPlayerState>())
        {
            TPS->SelectedFaction = Faction;
            
            // Keep GS roster consistent if you cleared PS roster
            if (bIsP1) S->P1Roster.Reset();
            if (bIsP2) S->P2Roster.Reset();
            S->P1Points = bIsP1 ? 0 : S->P1Points;
            S->P2Points = bIsP2 ? 0 : S->P2Points;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("HandleSelectFaction: PlayerState is not ATabletopPlayerState"));
        }

        S->OnArmySelectionChanged.Broadcast();
        S->ForceNetUpdate();
    }
}

void ASetupGamemode::TryAdvanceFromArmySelection()
{
    if (!HasAuthority()) return;
    if (ASetupGameState* S = GS())
    {
        const bool bBothPicked = (S->P1Faction != EFaction::None) && (S->P2Faction != EFaction::None);
        if (S->Phase == ESetupPhase::ArmySelection && AllPlayersReady() && bBothPicked)
        {
            S->bP1Ready = S->bP2Ready = false; // reset for next page
            S->Phase = ESetupPhase::UnitSelection;
            S->OnPhaseChanged.Broadcast();     // host update
            S->ForceNetUpdate();
        }
    }
}

void ASetupGamemode::Server_SelectMap(const FName MapRowOrLevel)
{
    if (!HasAuthority()) return;
    if (ASetupGameState* S = GS())
    {
        S->SelectedMap = MapRowOrLevel;
    }
}

void ASetupGamemode::StartMatchTravel()
{
    if (!HasAuthority()) return;
    if (const ASetupGameState* S = GS())
    {
        if (S->Phase == ESetupPhase::ReadyToStart && GetNumPlayers() >= 2 && !S->SelectedMap.IsNone())
        {
            // Seamless travel into match map
            FString URL = S->SelectedMap.ToString(); // Level name
            // You can append options like ?listen to host, or player-selected options.
            GetWorld()->ServerTravel(URL + TEXT("?listen"), true);
        }
    }
}

void ASetupGamemode::HandlePlayerReady(APlayerController* PC, bool bReady)
{
    if (!HasAuthority() || !PC) return;
    if (ASetupGameState* S = GS())
    {
        if (PC->PlayerState == S->Player1) S->bP1Ready = bReady;
        if (PC->PlayerState == S->Player2) S->bP2Ready = bReady;
        
        S->OnPlayerReadyUp.Broadcast();
        S->ForceNetUpdate(); 
    }
}

void ASetupGamemode::TryAdvanceFromLobby()
{
    if (!HasAuthority()) return;
    if (ASetupGameState* S = GS())
    {
        if (S->Phase == ESetupPhase::Lobby && AllPlayersReady())
        {
            S->bP1Ready = S->bP2Ready = false;
            S->Phase = ESetupPhase::ArmySelection;

            S->OnPhaseChanged.Broadcast();
        }
    }
}