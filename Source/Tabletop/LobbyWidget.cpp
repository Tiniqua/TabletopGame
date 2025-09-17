
#include "LobbyWidget.h"

#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Controllers/SetupPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Gamemodes/SetupGamemode.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineSessionInterface.h"

static FORCEINLINE const TCHAR* YesNo(bool b) { return b ? TEXT("Yes") : TEXT("No"); }

// Keys we advertised from the MenuWidget (keep names in sync)
static const FName SETTING_MAPNAME(TEXT("MAPNAME"));
static const FName SETTING_PRODUCT(TEXT("PRODUCT"));
static const FName SETTING_BUILDID(TEXT("BUILDID"));
static const FName SEARCH_KEYWORDS(TEXT("SEARCHKEYWORDS"));

void ULobbyWidget::SetLobbyStatus(const FString& Text)
{
    if (LobbyStatusBox) LobbyStatusBox->SetText(FText::FromString(Text));
}

void ULobbyWidget::AppendLobbyStatusLine(const FString& Line)
{
    if (!LobbyStatusBox) return;
    FString Cur = LobbyStatusBox->GetText().ToString();
    if (!Cur.IsEmpty() && !Cur.EndsWith(TEXT("\n"))) Cur += TEXT("\n");
    Cur += Line;
    LobbyStatusBox->SetText(FText::FromString(Cur));
}

FString ULobbyWidget::CurrentWorldPackage() const
{
    if (const UWorld* W = GetWorld())
        if (const UPackage* P = W->GetOutermost())
            return P->GetName(); // e.g. "/Game/Maps/L_PreGame"
    return TEXT("-");
}

/** Called once per second to summarize the active session after Host/Travel */
void ULobbyWidget::UpdateLobbyStatusSummary()
{
    if (!LobbyStatusBox) return;

    FString S;
    IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
    FString OssName = OSS ? OSS->GetSubsystemName().ToString() : TEXT("NULL");

    // Identity
    bool bIdentityReady = false;
    FString Nick = TEXT("-");
    FString Uid  = TEXT("-");
    if (OSS)
    {
        if (IOnlineIdentityPtr Id = OSS->GetIdentityInterface())
        {
            auto U = Id->GetUniquePlayerId(0);
            bIdentityReady = U.IsValid();
            if (bIdentityReady)
            {
                Uid  = U->ToString();
                Nick = Id->GetPlayerNickname(0);
            }
        }
    }

    // Session
    TSharedPtr<IOnlineSession, ESPMode::ThreadSafe> SI = OSS ? OSS->GetSessionInterface() : nullptr;
    FNamedOnlineSession* NS = (SI.IsValid() ? SI->GetNamedSession(NAME_GameSession) : nullptr);

    S += TEXT("[Lobby Session]\n");
    S += FString::Printf(TEXT("OSS            : %s\n"), *OssName);
    S += FString::Printf(TEXT("Identity Ready : %s\n"), YesNo(bIdentityReady));
    S += FString::Printf(TEXT("User           : %s\n"), *Nick);
    S += FString::Printf(TEXT("UniqueId       : %s\n"), *Uid);
    S += FString::Printf(TEXT("World/Map      : %s\n"), *CurrentWorldPackage());

    const UWorld* W = GetWorld();
    bool bHasAuth = (W && (W->GetAuthGameMode() || W->GetNetMode() == NM_ListenServer || W->GetNetMode() == NM_Standalone));
    S += FString::Printf(TEXT("Authority      : %s\n"), YesNo(bHasAuth));

    FString ListenAddr = TEXT("-");
    if (W)
    {
        if (UNetDriver* ND = W->GetNetDriver())
        {
            ListenAddr = ND->LowLevelGetNetworkNumber(); // SteamSockets prints a steam:p2p-ish id or ip:port for IpDriver
            S += FString::Printf(TEXT("NetDriver      : %s\n"), *ND->GetClass()->GetName());
        }
    }
    S += FString::Printf(TEXT("Listen Addr    : %s\n"), *ListenAddr);

    if (NS)
    {
        FString Map;          NS->SessionSettings.Get(SETTING_MAPNAME, Map);
        FString Product;      NS->SessionSettings.Get(SETTING_PRODUCT, Product);
        int32   BuildMeta=-1; NS->SessionSettings.Get(SETTING_BUILDID, BuildMeta);
        FString Tag;          NS->SessionSettings.Get(SEARCH_KEYWORDS, Tag);

        S += TEXT("\n-- NamedSession --\n");
        S += FString::Printf(TEXT("Owner          : %s\n"), *NS->OwningUserName);
        S += FString::Printf(TEXT("Open/Max       : %d / %d\n"), NS->NumOpenPublicConnections, NS->SessionSettings.NumPublicConnections);
        S += FString::Printf(TEXT("BuildUniqueId  : %d (engine)\n"), NS->SessionSettings.BuildUniqueId);
        S += FString::Printf(TEXT("SessionId      : %s\n"), *NS->GetSessionIdStr());
        S += FString::Printf(TEXT("MAPNAME        : %s\n"), *Map);
        S += FString::Printf(TEXT("PRODUCT        : %s\n"), *Product);
        S += FString::Printf(TEXT("BUILDID(meta)  : %d\n"), BuildMeta);
        S += FString::Printf(TEXT("SEARCHKEYWORDS : %s\n"), *Tag);
    }
    else
    {
        S += TEXT("\n-- NamedSession --\nNone\n");
    }

    SetLobbyStatus(S);
}

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

    if (LobbyStatusBox) LobbyStatusBox->SetIsReadOnly(true);

    RefreshFromState();

    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().SetTimer(
            LobbyStatusRefreshHandle,
            this, &ULobbyWidget::UpdateLobbyStatusSummary,
            1.0f, /*bLoop=*/true
        );
    }

    // and show one immediately
    UpdateLobbyStatusSummary();
}

void ULobbyWidget::NativeDestruct()
{
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(LobbyStatusRefreshHandle);
    }
    Super::NativeDestruct();
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
    ASetupGameState* S  = GetSetupGS();
    ASetupPlayerController* PC = GetSetupPC();
    if (!S || !PC) return;

    // Names: prefer replicated strings; fall back to PlayerState names when present
    FString P1 = S->Player1Name.IsEmpty() ? TEXT("Waiting...") : S->Player1Name;
    FString P2 = S->Player2Name.IsEmpty() ? TEXT("Waiting...") : S->Player2Name;

    if (S->Player1)
    {
        const FString Name = S->Player1->GetPlayerName();
        if (!Name.IsEmpty()) P1 = Name;
    }
    if (S->Player2)
    {
        const FString Name = S->Player2->GetPlayerName();
        if (!Name.IsEmpty()) P2 = Name;
    }

    if (P1Name) P1Name->SetText(FText::FromString(P1));
    if (P2Name) P2Name->SetText(FText::FromString(P2));

    // ---- Robust seat ownership check ----
    auto SameNetId = [](const APlayerState* A, const APlayerState* B)
    {
        if (!A || !B) return false;
        const FUniqueNetIdRepl& IdA = A->GetUniqueId();
        const FUniqueNetIdRepl& IdB = B->GetUniqueId();
        return IdA.IsValid() && IdB.IsValid() && (*IdA == *IdB);
    };

    APlayerState* LocalPS = PC->PlayerState;
    bool bLocalIsP1 = false;
    bool bLocalIsP2 = false;

    if (S->Player1) bLocalIsP1 = SameNetId(LocalPS, S->Player1);
    if (S->Player2) bLocalIsP2 = SameNetId(LocalPS, S->Player2);

    // Listen-server fallback: right after host travels, S->Player1 may not be set yet on the client side.
    // If we have authority, treat local as P1 until replication catches up.
    if (!bLocalIsP1 && !S->Player1 && PC->HasAuthority())
    {
        bLocalIsP1 = true;
    }

    if (P1ReadyBtn) P1ReadyBtn->SetIsEnabled(bLocalIsP1);
    if (P2ReadyBtn) P2ReadyBtn->SetIsEnabled(bLocalIsP2);

    // Host-only advance button
    if (BothReady)
    {
        BothReady->SetIsEnabled(bLocalIsP1 && S->bP1Ready && S->bP2Ready && S->Phase == ESetupPhase::Lobby);
    }
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