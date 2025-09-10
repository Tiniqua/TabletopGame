#include "MenuWidget.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"

#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Online/OnlineSessionNames.h"

static TSharedPtr<const FUniqueNetId> GetLocalUserId(UWorld* World)
{
    if (!World) return nullptr;
    if (ULocalPlayer* LP = World->GetFirstLocalPlayerFromController())
    {
        FUniqueNetIdRepl Repl = LP->GetPreferredUniqueNetId();
        return Repl.GetUniqueNetId();
    }
    return nullptr;
}

static FORCEINLINE const TCHAR* YesNo(bool b) { return b ? TEXT("Yes") : TEXT("No"); }


static void HostTravelToListen(UWorld* World, const FString& LongPackageName)
{
    if (!World || LongPackageName.IsEmpty()) return;

    // Always use OpenLevel with "listen" to ensure a listen net driver is created,
    // in Editor Standalone *and* Packaged builds.
    UGameplayStatics::OpenLevel(World, FName(*LongPackageName), /*bAbsolute=*/true, TEXT("listen"));
}

FString UMenuWidget::JoinResultToString(EOnJoinSessionCompleteResult::Type Result) const
{
    switch (Result)
    {
        case EOnJoinSessionCompleteResult::Success:           return TEXT("Success");
        case EOnJoinSessionCompleteResult::SessionIsFull:     return TEXT("SessionIsFull");
        case EOnJoinSessionCompleteResult::SessionDoesNotExist:return TEXT("SessionDoesNotExist");
        case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:return TEXT("CouldNotRetrieveAddress");
        case EOnJoinSessionCompleteResult::AlreadyInSession:  return TEXT("AlreadyInSession");
        case EOnJoinSessionCompleteResult::UnknownError:      return TEXT("UnknownError");
        default:                                              return FString::Printf(TEXT("Result=%d"), (int32)Result);
    }
}

FString UMenuWidget::SummarizeSearchResult(const FOnlineSessionSearchResult& R) const
{
    const auto& S = R.Session;
    FString MapVal; S.SessionSettings.Get(SETTING_MAPNAME, MapVal);

    FString Product; S.SessionSettings.Get(SETTING_PRODUCT, Product);
    int32   BuildMeta = -1; S.SessionSettings.Get(SETTING_BUILDID, BuildMeta);
    FString Tag; S.SessionSettings.Get(SEARCH_KEYWORDS, Tag);

    return FString::Printf(
        TEXT("Owner=%s Open=%d/%d Ping=%d BuildMeta=%d Tag=%s Product=%s Id=%s Map=%s"),
        *S.OwningUserName,
        S.NumOpenPublicConnections, S.SessionSettings.NumPublicConnections,
        R.PingInMs,
        BuildMeta, *Tag, *Product,
        *S.GetSessionIdStr(), *MapVal
    );
}

void UMenuWidget::SetOssStatus(const FString& Text)
{
    if (OssStatusBox) { OssStatusBox->SetText(FText::FromString(Text)); }
}

void UMenuWidget::AppendOssStatusLine(const FString& Line)
{
    if (!OssStatusBox) return;
    FString Cur = OssStatusBox->GetText().ToString();
    if (!Cur.IsEmpty() && !Cur.EndsWith(TEXT("\n"))) Cur += TEXT("\n");
    Cur += Line;
    OssStatusBox->SetText(FText::FromString(Cur));
}

/** Captures a one-shot snapshot of OSS state + search results to help diagnose joins */
void UMenuWidget::ShowJoinDebugSnapshot(const TCHAR* Context, int32 DesiredIndex)
{
    FString S;

    // Identity / OSS
    IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
    bool bIdentityReady = false;
    FString UidStr = TEXT("-");
    FString Nick   = TEXT("-");
    FString Oss    = Subsystem ? Subsystem->GetSubsystemName().ToString() : TEXT("NULL");

    if (Subsystem)
    {
        if (IOnlineIdentityPtr Id = Subsystem->GetIdentityInterface())
        {
            bIdentityReady = Id->GetUniquePlayerId(0).IsValid();
            if (bIdentityReady) { UidStr = Id->GetUniquePlayerId(0)->ToString(); Nick = Id->GetPlayerNickname(0); }
        }
    }

    // Any existing named session?
    bool bHadExisting = false;
    if (SessionInterface.IsValid())
    {
        bHadExisting = SessionInterface->GetNamedSession(SessionName) != nullptr;
    }

    // Build header
    S += FString::Printf(TEXT("[JoinDiag %s]\n"), Context);
    S += FString::Printf(TEXT("OSS=%s  IdentityReady=%s  Nick=%s  Id=%s\n"),
                         *Oss, bIdentityReady ? TEXT("Yes") : TEXT("No"), *Nick, *UidStr);
    S += FString::Printf(TEXT("PendingJoinIndex=%d  NamedSessionExists=%s\n"),
                         DesiredIndex, bHadExisting ? TEXT("Yes") : TEXT("No"));

    // Search results
    int32 Num = LastSearchResults.Num();
    S += FString::Printf(TEXT("SearchResults=%d\n"), Num);

    const int32 MaxDump = 6; // don’t spam UI
    for (int32 i = 0; i < Num && i < MaxDump; ++i)
    {
        S += FString::Printf(TEXT("  [%d] %s\n"), i, *SummarizeSearchResult(LastSearchResults[i]));
    }
    if (Num > MaxDump) { S += FString::Printf(TEXT("  ... (%d more)\n"), Num - MaxDump); }

    // Desired index preview
    if (DesiredIndex >= 0 && LastSearchResults.IsValidIndex(DesiredIndex))
    {
        S += FString::Printf(TEXT("Chosen[%d]: %s\n"),
                             DesiredIndex, *SummarizeSearchResult(LastSearchResults[DesiredIndex]));
    }

    SetOssStatus(S);
}

void UMenuWidget::UpdateOssStatusSummary()
{
    if (bShuttingDownForTravel || bHoldStatusRefresh) return;
    if (!IsValid(this) || !IsValid(OssStatusBox)) return;
    
    if (bHoldStatusRefresh) return;
    
    if (!OssStatusBox) return;

    // ---------- Gather basics ----------
    FString SubsystemName   = TEXT("NULL");
    FString UserNick        = TEXT("-");
    FString UserIdStr       = TEXT("-");
    bool    bIdentityReady  = false;

    int32 DevAppIdCfg       = 0;             // From INI
    const int32 BuildIdIntended = IntendedBuildId; // Your compile-time/runtime intended build
    int32 BuildIdCurrent    = -1;            // From active session (if any)

    // Read SteamDevAppId from INI (works Editor & Packaged)
    if (GConfig)
    {
        // Preferred modern path
        GConfig->GetInt(TEXT("/Script/OnlineSubsystemSteam.OnlineSubsystemSteam"),
                        TEXT("SteamDevAppId"), DevAppIdCfg, GEngineIni);

        // Some projects still use the legacy section name
        if (DevAppIdCfg == 0)
        {
            GConfig->GetInt(TEXT("OnlineSubsystemSteam"),
                            TEXT("SteamDevAppId"), DevAppIdCfg, GEngineIni);
        }
    }

    // Online subsystem + identity
    IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
    if (OSS)
    {
        SubsystemName   = OSS->GetSubsystemName().ToString();
        SessionInterface = OSS->GetSessionInterface();

        if (IOnlineIdentityPtr Identity = OSS->GetIdentityInterface())
        {
            TSharedPtr<const FUniqueNetId> Uid = Identity->GetUniquePlayerId(0);
            bIdentityReady = Uid.IsValid();
            if (bIdentityReady)
            {
                UserIdStr = Uid->ToString();
                UserNick  = Identity->GetPlayerNickname(0);
            }
        }
    }

    // Inspect any active named session (safe at any time)
    FNamedOnlineSession* NS = nullptr;
    if (SessionInterface.IsValid())
    {
        NS = SessionInterface->GetNamedSession(NAME_GameSession);
        if (NS)
        {
            BuildIdCurrent = NS->SessionSettings.BuildUniqueId;
        }
    }

    // What our CreateSession uses (keep these in sync with your CreateSession code)
    const bool bWillUsePresence = true;
    const bool bWillUseLobbies  = true;
    const bool bIsLan           = false;

    // Planned advertised map (never empty with your ResolveLobbyMapPath fix)
    const FString MapToAdvertise = ResolveLobbyMapPath();

    // Optional: live AppID (guarded so it compiles without Steam headers)
    FString LiveAppId = TEXT("-");
    /*
    #if WITH_STEAMWORKS
        if (SteamUtils())
        {
            LiveAppId = FString::FromInt((int32)SteamUtils()->GetAppID());
        }
    #endif
    */

    // ---------- Build the summary ----------
    FString S;
    S += FString::Printf(TEXT("OSS            : %s\n"), *SubsystemName);
    S += FString::Printf(TEXT("Identity Ready : %s\n"), YesNo(bIdentityReady));
    S += FString::Printf(TEXT("User           : %s\n"), *UserNick);
    S += FString::Printf(TEXT("UniqueId       : %s\n"), *UserIdStr);
    S += FString::Printf(TEXT("DevAppId.ini   : %d\n"), DevAppIdCfg);
    if (LiveAppId != TEXT("-"))
        S += FString::Printf(TEXT("Steam AppID    : %s\n"), *LiveAppId);
    S += FString::Printf(TEXT("Intended Build : %d\n"), BuildIdIntended);
    S += FString::Printf(TEXT("Session Build  : %d\n"), BuildIdCurrent);
    S += FString::Printf(TEXT("Presence/Lobby : %s / %s\n"),
                         YesNo(bWillUsePresence), YesNo(bWillUseLobbies));
    S += FString::Printf(TEXT("LAN            : %s\n"), YesNo(bIsLan));
    S += FString::Printf(TEXT("Map (advert)   : %s\n"), *MapToAdvertise);

    // If a session exists, append a concise live snapshot
    if (NS)
    {
        FString MapKey;      NS->SessionSettings.Get(SETTING_MAPNAME, MapKey);
        const FString Owner = NS->OwningUserName;
        const int32   Open  = NS->NumOpenPublicConnections;
        const int32   Max   = NS->SessionSettings.NumPublicConnections;
        const FString SessId = NS->GetSessionIdStr();

        S += TEXT("\n-- Active Session --\n");
        S += FString::Printf(TEXT("Owner          : %s\n"), *Owner);
        S += FString::Printf(TEXT("Open/Max       : %d / %d\n"), Open, Max);
        S += FString::Printf(TEXT("MAPNAME        : %s\n"), *MapKey);
        S += FString::Printf(TEXT("SessionId      : %s\n"), *SessId);
    }
    else
    {
        S += TEXT("\n-- Active Session --\nNone\n");
    }

    OssStatusBox->SetText(FText::FromString(S));
}

FName UMenuWidget::NormalizeLevelName(const FString& LevelNameOrPath)
{
    FString Trim = LevelNameOrPath;
    Trim.TrimStartAndEndInline();
    if (Trim.IsEmpty()) return NAME_None;

    int32 QIdx;
    if (Trim.FindChar(TEXT('?'), QIdx))
    {
        Trim.LeftInline(QIdx, false);
    }
    return FName(*Trim);
}

FName UMenuWidget::PackageNameFromLevel(const ULevel* Level)
{
    const UPackage* Pkg = Level ? Level->GetOutermost() : nullptr;
    return Pkg ? Pkg->GetFName() : NAME_None;
}

void UMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (HostButton) HostButton->OnClicked.AddDynamic(this, &UMenuWidget::OnHostClicked);
    if (JoinButton) JoinButton->OnClicked.AddDynamic(this, &UMenuWidget::OnJoinClicked);
    if (QuitButton) QuitButton->OnClicked.AddDynamic(this, &UMenuWidget::OnQuitClicked);

    if (GEngine)
    {
        GEngine->OnNetworkFailure().AddUObject(this, &UMenuWidget::HandleNetworkFailure);
        GEngine->OnTravelFailure().AddUObject(this, &UMenuWidget::HandleTravelFailure);
    }

    // status box read-only
    if (OssStatusBox) OssStatusBox->SetIsReadOnly(true);

    // initial populate + periodic refresh (once per second is plenty)
    UpdateOssStatusSummary();
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().SetTimer(
            OssStatusRefreshHandle,
            this, &UMenuWidget::UpdateOssStatusSummary,
            1.0f, /*bLoop=*/true
        );
    }
}

void UMenuWidget::NativeDestruct()
{
    if (GEngine)
    {
        GEngine->OnTravelFailure().RemoveAll(this);
        GEngine->OnNetworkFailure().RemoveAll(this);
    }
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(JoinTimeoutHandle);
        W->GetTimerManager().ClearTimer(OssStatusRefreshHandle);
    }
    Super::NativeDestruct();
}

/* -------------------- Hosting -------------------- */

void UMenuWidget::OpenAsListen_ByLevel(ULevel* Level)
{
    if (!Level) { UE_LOG(LogTemp, Warning, TEXT("OpenAsListen_ByLevel: Level is null.")); return; }

    const UWorld* OwningWorld = Level->GetTypedOuter<UWorld>();
    if (!OwningWorld) { UE_LOG(LogTemp, Warning, TEXT("OpenAsListen_ByLevel: No owning world.")); return; }

    if (OwningWorld->PersistentLevel != Level)
    {
        UE_LOG(LogTemp, Warning, TEXT("OpenAsListen_ByLevel: Provided sub-level; using persistent world instead."));
    }

    const UPackage* Pkg = OwningWorld->GetOutermost();
    const FString LongPackageName = Pkg ? Pkg->GetName() : FString(); // e.g. "/Game/Maps/Lobby"
    if (LongPackageName.IsEmpty()) { UE_LOG(LogTemp, Warning, TEXT("OpenAsListen_ByLevel: Could not resolve package name.")); return; }

    if (UWorld* World = GetWorld())
    {
        CurrentMapName = FName(*LongPackageName);
        HostTravelToListen(World, LongPackageName);   // <-- change
    }
}

void UMenuWidget::OpenAsListen_ByWorld(TSoftObjectPtr<UWorld> MapAsset)
{
    if (UWorld* World = GetWorld())
    {
        FString LongPackageName;
        if (!MapAsset.IsNull())
        {
            LongPackageName = MapAsset.ToSoftObjectPath().GetLongPackageName(); // "/Game/Maps/Lobby"
        }

        if (!LongPackageName.IsEmpty())
        {
            CurrentMapName = FName(*LongPackageName);
            HostTravelToListen(World, LongPackageName);  // <-- change
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("OpenAsListen_ByWorld: MapAsset is null/invalid."));
        }
    }
}


void UMenuWidget::OpenAsListen_ByName(const FString& LevelNameOrPath)
{
    if (UWorld* World = GetWorld())
    {
        const FName MapName = NormalizeLevelName(LevelNameOrPath); // accepts long package or short name
        if (!MapName.IsNone())
        {
            const FString LongPackageName = MapName.ToString(); // prefer long name like "/Game/Maps/Lobby"
            CurrentMapName = MapName;
            HostTravelToListen(World, LongPackageName);         // <-- change

            // optional debug
            if (UNetDriver* ND = World->GetNetDriver())
            {
                UE_LOG(LogNet, Log, TEXT("Listening at %s"), *ND->LowLevelGetNetworkNumber());
            }
        }
    }
}


FString UMenuWidget::ResolveLobbyMapPath() const
{
    if (!bShuttingDownForTravel && IsValid(MapPathBox))
    {
        const FString FromBox = MapPathBox->GetText().ToString().TrimStartAndEnd();
        if (!FromBox.IsEmpty())
        {
            return FromBox;
        }
    }
    // Hard fallback (prevents MAPNAME empty)
    return LobbyMapAssetPath.IsEmpty() ? TEXT("/Game/Maps/L_PreGame") : LobbyMapAssetPath;
}

FString UMenuWidget::ResolveJoinAddress() const
{
    if (EditText)
    {
        const FString FromBox = EditText->GetText().ToString().TrimStartAndEnd();
        if (!FromBox.IsEmpty())
        {
            return FromBox;
        }
    }
    return DefaultJoinAddress;
}

/* -------------------- Buttons -------------------- */

void UMenuWidget::OnHostClicked()
{
    if (bUseOnlineSessions) { CreateSessionOrFallback(); }
    else                    { OpenLobbyAsListen(); }
}

void UMenuWidget::OnJoinClicked()
{
    if (JoinButton) JoinButton->SetIsEnabled(false);
    if (EditText)   EditText->SetText(FText::FromString(TEXT("Searching Steam lobbies…")));

    ShowJoinDebugSnapshot(TEXT("OnJoinClicked(BeforeFind)"));
    FindSessions_Steam();
}

void UMenuWidget::HandleJoinTimeout()
{
    SetJoinFailText(FString::Printf(TEXT("Timed out after %.0fs"), JoinTimeoutSeconds));
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(JoinTimeoutHandle);
    }
}

void UMenuWidget::HandleTravelFailure(UWorld*, ETravelFailure::Type Type, const FString& ErrorString)
{
    SetJoinFailText(FriendlyTravelText(Type, ErrorString));
}

void UMenuWidget::HandleNetworkFailure(UWorld*, UNetDriver*, ENetworkFailure::Type Type, const FString& ErrorString)
{
    SetJoinFailText(FriendlyNetworkText(Type, ErrorString));
}

void UMenuWidget::SetJoinFailText(const FString& FriendlyMessage)
{
    if (EditText)
    {
        const FString Short = FriendlyMessage.Replace(TEXT("\n"), TEXT(" "));
        const FString TextToShow = FString::Printf(TEXT("Join failed: %s | Try: %s"), *Short, *LastJoinAttempt);
        EditText->SetText(FText::FromString(TextToShow));
        EditText->SetKeyboardFocus();
    }
    if (JoinButton) { JoinButton->SetIsEnabled(true); }

    // 🔒 Hold status refresh so the error is visible for longer
    if (UWorld* W = GetWorld())
    {
        bHoldStatusRefresh = true;
        W->GetTimerManager().ClearTimer(JoinFailHoldTimer);
        W->GetTimerManager().SetTimer(
            JoinFailHoldTimer,
            [this]()
            {
                bHoldStatusRefresh = false;   // ⏱️ release the hold after N seconds
            },
            JoinFailHoldSeconds,
            false
        );
    }
}

FString UMenuWidget::FriendlyTravelText(ETravelFailure::Type Type, const FString& Reason)
{
    switch (Type)
    {
        case ETravelFailure::NoLevel:               return TEXT("Invalid URL or map");
        case ETravelFailure::LoadMapFailure:        return TEXT("Server failed to load map");
        case ETravelFailure::InvalidURL:            return TEXT("Invalid address");
        case ETravelFailure::PackageVersion:        return TEXT("Version mismatch");
        case ETravelFailure::PendingNetGameCreateFailure: return TEXT("Server unreachable / timed out");
        case ETravelFailure::TravelFailure:         return TEXT("Travel failure");
        default:                                    return Reason.IsEmpty() ? TEXT("Travel error") : Reason;
    }
}

FString UMenuWidget::FriendlyNetworkText(ENetworkFailure::Type Type, const FString& Reason)
{
    switch (Type)
    {
        case ENetworkFailure::ConnectionLost:            return TEXT("Connection lost");
        case ENetworkFailure::ConnectionTimeout:         return TEXT("Connection timed out");
        case ENetworkFailure::FailureReceived:           return TEXT("Connection refused");
        case ENetworkFailure::PendingConnectionFailure:  return TEXT("Server unreachable");
        case ENetworkFailure::NetGuidMismatch:           return TEXT("Version/content mismatch");
        case ENetworkFailure::NetChecksumMismatch:       return TEXT("Checksum mismatch");
        case ENetworkFailure::NetDriverAlreadyExists:    return TEXT("Net driver already exists");
        case ENetworkFailure::OutdatedClient:            return TEXT("Client is outdated");
        case ENetworkFailure::OutdatedServer:            return TEXT("Server is outdated");
        default:                                         return Reason.IsEmpty() ? TEXT("Network error") : Reason;
    }
}

void UMenuWidget::OnQuitClicked()
{
    UKismetSystemLibrary::QuitGame(GetWorld(), GetOwningPlayer(), EQuitPreference::Quit, false);
}

/* -------------------- Host helpers -------------------- */

void UMenuWidget::OpenLobbyAsListen()
{
    // 🔒 stop periodic UI updates before world teardown
    bShuttingDownForTravel = true;
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(OssStatusRefreshHandle);
        W->GetTimerManager().ClearTimer(JoinFailHoldTimer);
    }
    if (GEngine)
    {
        GEngine->OnTravelFailure().RemoveAll(this);
        GEngine->OnNetworkFailure().RemoveAll(this);
    }

    if (HostLevelOverride)
    {
        OpenAsListen_ByLevel(HostLevelOverride);
        return;
    }
    if (!HostMapAsset.IsNull())
    {
        OpenAsListen_ByWorld(HostMapAsset);
        return;
    }

    const FString MapPath = ResolveLobbyMapPath();
    OpenAsListen_ByName(MapPath);
}

/* -------------------- Sessions -------------------- */

void UMenuWidget::CreateSessionOrFallback()
{
    IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
    if (!Subsystem) { OpenLobbyAsListen(); return; }

    SessionInterface = Subsystem->GetSessionInterface();
    if (!SessionInterface.IsValid()) { OpenLobbyAsListen(); return; }

    if (SessionInterface->GetNamedSession(SessionName))
    {
        UE_LOG(LogTemp, Log, TEXT("Session '%s' already exists; destroying before create..."), *SessionName.ToString());
        DestroySessionCompleteHandle =
            SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(
                FOnDestroySessionCompleteDelegate::CreateUObject(this, &UMenuWidget::HandleDestroyThenCreate));
        SessionInterface->DestroySession(SessionName);
        return;
    }

    // Figure out the actual map we will travel to
    const FString WillTravelTo = HostLevelOverride
        ? PackageNameFromLevel(HostLevelOverride).ToString()
        : (!HostMapAsset.IsNull()
            ? HostMapAsset.ToSoftObjectPath().GetLongPackageName()
            : ResolveLobbyMapPath());

    FOnlineSessionSettings Settings;
    Settings.bIsLANMatch                      = false;
    Settings.bShouldAdvertise                 = true;
    Settings.bUsesPresence                    = true;
    Settings.bUseLobbiesIfAvailable           = true;
    Settings.bAllowJoinInProgress             = bAllowJoinInProgress;
    Settings.NumPublicConnections             = FMath::Max(NumPublicConnections, 1);
    Settings.bAllowInvites                    = true;
    Settings.bAllowJoinViaPresence            = true;
    Settings.bAllowJoinViaPresenceFriendsOnly = false;
    
    Settings.BuildUniqueId = IntendedBuildId; // keep for parity, but not used for search

    // Advertise our identifiers in lobby metadata:
    Settings.Set(SEARCH_KEYWORDS, FString(kOurSearchTag), EOnlineDataAdvertisementType::ViaOnlineService);
    Settings.Set(SETTING_PRODUCT,  FString(kOurProduct),   EOnlineDataAdvertisementType::ViaOnlineService);
    Settings.Set(SETTING_BUILDID,  IntendedBuildId,        EOnlineDataAdvertisementType::ViaOnlineService);

    // Never empty now:
    const FString MapToAdvertise = WillTravelTo.IsEmpty() ? TEXT("/Game/Maps/L_PreGame") : WillTravelTo;
    Settings.Set(SETTING_MAPNAME, MapToAdvertise, EOnlineDataAdvertisementType::ViaOnlineService);

    TSharedPtr<const FUniqueNetId> UserId = GetLocalUserId(GetWorld());
    if (!UserId.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("CreateSession: no valid UniqueNetId (Steam not logged in?)."));
        OpenLobbyAsListen();
        return;
    }

    BindSessionDelegates();

    UE_LOG(LogTemp, Log, TEXT("OSS=%s, Using Lobbies=%d, Presence=%d, Pubs=%d"),
        Subsystem ? *Subsystem->GetSubsystemName().ToString() : TEXT("NULL"),
        (int32)Settings.bUseLobbiesIfAvailable, (int32)Settings.bUsesPresence, Settings.NumPublicConnections);
    UE_LOG(LogTemp, Log, TEXT("Advertising MAPNAME='%s'"), *MapToAdvertise);

    const bool bStartOk = SessionInterface->CreateSession(*UserId, SessionName, Settings);
    if (!bStartOk)
    {
        UE_LOG(LogTemp, Warning, TEXT("CreateSession() returned false immediately."));
        UnbindSessionDelegates();
        OpenLobbyAsListen();
    }
}

void UMenuWidget::HandleDestroyThenCreate(FName InSessionName, bool bWasSuccessful)
{
    if (SessionInterface.IsValid())
    {
        SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteHandle);
        DestroySessionCompleteHandle.Reset();
    }
    UE_LOG(LogTemp, Log, TEXT("DestroySession before create: %s"), bWasSuccessful ? TEXT("OK") : TEXT("FAIL (continuing)"));
    // Try again (will fall through to normal create path)
    CreateSessionOrFallback();
}

void UMenuWidget::BindSessionDelegates()
{
    if (SessionInterface.IsValid())
    {
        CreateSessionCompleteHandle =
            SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
                FOnCreateSessionCompleteDelegate::CreateUObject(this, &UMenuWidget::HandleCreateSessionComplete));
    }
}

void UMenuWidget::UnbindSessionDelegates()
{
    if (SessionInterface.IsValid() && CreateSessionCompleteHandle.IsValid())
    {
        SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle);
        CreateSessionCompleteHandle.Reset();
    }
}

void UMenuWidget::HandleCreateSessionComplete(FName InSessionName, bool bWasSuccessful)
{
    UnbindSessionDelegates();
    if (!bWasSuccessful)
    {
        OpenLobbyAsListen();
        return;
    }

    // Travel now. Do NOT schedule any timers back to this widget/world.
    OpenLobbyAsListen();

    // Start the session (no need to use a world timer; this is OSS-only)
    if (IOnlineSubsystem* OSS = IOnlineSubsystem::Get())
        if (auto SI = OSS->GetSessionInterface())
            SI->StartSession(NAME_GameSession);
}
/* -------------------- Find/Join (Steam) -------------------- */

void UMenuWidget::FindSessions_Steam()
{
    IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
    if (!Subsystem) { UE_LOG(LogTemp, Warning, TEXT("FindSessions: No OSS.")); AppendOssStatusLine(TEXT("ERR: No Online Subsystem.")); goto DoneFail; }

    // ✅ Make sure Steam login/identity is ready before searching
    {
        IOnlineIdentityPtr Id = Subsystem->GetIdentityInterface();
        if (!Id.IsValid() || !Id->GetUniquePlayerId(0).IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("FindSessions aborted: not logged into Steam yet."));
            AppendOssStatusLine(TEXT("ERR: Identity not ready (Steam login)."));
            if (EditText) EditText->SetText(FText::FromString(TEXT("Steam not ready yet. Try again in a moment…")));
            goto DoneFail;
        }
    }

    SessionInterface = Subsystem->GetSessionInterface();
    if (!SessionInterface.IsValid()) { UE_LOG(LogTemp, Warning, TEXT("FindSessions: No SessionInterface.")); AppendOssStatusLine(TEXT("ERR: No SessionInterface.")); goto DoneFail; }

    SessionSearch = MakeShared<FOnlineSessionSearch>();
    SessionSearch->bIsLanQuery      = false;
    SessionSearch->MaxSearchResults = 200;

    // Presence/lobbies
    SessionSearch->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);
    // Optional (Steam): ensure lobby search (some subsystems ignore, but harmless)
    SessionSearch->QuerySettings.Set(SEARCH_LOBBIES,  true, EOnlineComparisonOp::Equals);

    // Only our game’s lobbies:
    SessionSearch->QuerySettings.Set(SEARCH_KEYWORDS, FString(kOurSearchTag), EOnlineComparisonOp::Equals);
    SessionSearch->QuerySettings.Set(SETTING_PRODUCT, FString(kOurProduct),   EOnlineComparisonOp::Equals);
    SessionSearch->QuerySettings.Set(SETTING_BUILDID, IntendedBuildId,        EOnlineComparisonOp::Equals);
    

    // Only return sessions with at least 1 open slot
    SessionSearch->QuerySettings.Set(SEARCH_MINSLOTSAVAILABLE, 1, EOnlineComparisonOp::GreaterThanEquals);


    FindSessionsCompleteHandle =
        SessionInterface->AddOnFindSessionsCompleteDelegate_Handle(
            FOnFindSessionsCompleteDelegate::CreateUObject(this, &UMenuWidget::HandleFindSessionsComplete));

    if (!SessionInterface->FindSessions(/*LocalUserNum*/0, SessionSearch.ToSharedRef()))
    {
        SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
        UE_LOG(LogTemp, Warning, TEXT("FindSessions failed to start."));
        AppendOssStatusLine(TEXT("ERR: FindSessions() returned false immediately."));
        goto DoneFail;
    }
    return;

DoneFail:
    if (JoinButton) JoinButton->SetIsEnabled(true);
    ShowJoinDebugSnapshot(TEXT("FindSessions(FAILED)"));
    return;
}


TArray<FFoundSessionRow> UMenuWidget::GetLastSearchRows() const // ⭐ removed UMenuWidget::
{
    TArray<FFoundSessionRow> Out;
    for (int32 i = 0; i < LastSearchResults.Num(); ++i)
    {
        const auto& R = LastSearchResults[i];
        FFoundSessionRow Row;
        Row.ResultIndex = i;
        Row.OwnerName   = R.Session.OwningUserName;
        Row.MaxSlots    = R.Session.SessionSettings.NumPublicConnections;
        Row.OpenSlots   = R.Session.NumOpenPublicConnections;
        Row.PingMs      = R.PingInMs;

        FString Map;
        if (R.Session.SessionSettings.Get(SETTING_MAPNAME, Map))
        {
            Row.Map = Map;
        }
        Out.Add(Row);
    }
    return Out;
}

void UMenuWidget::JoinSessionByIndex(int32 ResultIndex)
{
    ShowJoinDebugSnapshot(TEXT("JoinSessionByIndex(Start)"), ResultIndex);

    if (!LastSearchResults.IsValidIndex(ResultIndex))
    {
        AppendOssStatusLine(TEXT("ERR: Invalid ResultIndex for LastSearchResults."));
        if (JoinButton) JoinButton->SetIsEnabled(true);
        return;
    }

    IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
    SessionInterface = Subsystem ? Subsystem->GetSessionInterface() : nullptr;
    if (!SessionInterface.IsValid())
    {
        AppendOssStatusLine(TEXT("ERR: No SessionInterface (OSS missing or not initialized)."));
        if (JoinButton) JoinButton->SetIsEnabled(true);
        return;
    }

    // If a session exists, destroy first, then join in the callback.
    if (SessionInterface->GetNamedSession(SessionName))
    {
        PendingJoinIndex = ResultIndex; // remember which one we wanted
        AppendOssStatusLine(TEXT("Info: Destroying existing session before joining..."));
        DestroySessionCompleteHandle =
            SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(
                FOnDestroySessionCompleteDelegate::CreateUObject(this, &UMenuWidget::HandleDestroySessionComplete));
        SessionInterface->DestroySession(SessionName);
        return; // ✅ don't call Join yet
    }

    // No existing session -> join now
    JoinSessionCompleteHandle =
        SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(
            FOnJoinSessionCompleteDelegate::CreateUObject(this, &UMenuWidget::HandleJoinSessionComplete));

    const int32 LocalUserNum = 0;
    if (!SessionInterface->JoinSession(LocalUserNum, SessionName, LastSearchResults[ResultIndex]))
    {
        SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
        UE_LOG(LogTemp, Warning, TEXT("JoinSession failed to start."));
        AppendOssStatusLine(TEXT("ERR: JoinSession() returned false immediately."));
        ShowJoinDebugSnapshot(TEXT("JoinSessionByIndex(JoinStartFailed)"), ResultIndex);
        if (JoinButton) JoinButton->SetIsEnabled(true);
    }
}


void UMenuWidget::HandleJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type Result)
{
    if (SessionInterface.IsValid())
    {
        SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
    }

    if (Result != EOnJoinSessionCompleteResult::Success)
    {
        const FString R = JoinResultToString(Result);
        AppendOssStatusLine(FString::Printf(TEXT("Join failed: %s"), *R));
        ShowJoinDebugSnapshot(TEXT("HandleJoinSessionComplete(Fail)"));
        SetJoinFailText(FString::Printf(TEXT("Join failed: %s"), *R));
        if (JoinButton) JoinButton->SetIsEnabled(true);
        return;
    }

    FString ConnectStr;

    if (!SessionInterface->GetResolvedConnectString(InSessionName, ConnectStr, NAME_GamePort))
    {
        // Fallback for subsystems that don’t need the port type
        if (!SessionInterface->GetResolvedConnectString(InSessionName, ConnectStr))
        {
            AppendOssStatusLine(TEXT("ERR: GetResolvedConnectString returned empty."));
            ShowJoinDebugSnapshot(TEXT("JoinResolvedConnectString(Empty)"));
            SetJoinFailText(TEXT("Could not resolve server address."));
            if (JoinButton) JoinButton->SetIsEnabled(true);
            return;
        }
    }

    if (APlayerController* PC = GetOwningPlayer())
    {
        UE_LOG(LogTemp, Log, TEXT("ClientTravel to %s"), *ConnectStr);
        AppendOssStatusLine(FString::Printf(TEXT("ClientTravel to %s"), *ConnectStr));
        PC->ClientTravel(ConnectStr, ETravelType::TRAVEL_Absolute);
    }
}


void UMenuWidget::HandleDestroySessionComplete(FName InSessionName, bool bWasSuccessful)
{
    if (SessionInterface.IsValid())
    {
        SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteHandle);
    }
    AppendOssStatusLine(FString::Printf(TEXT("DestroySession complete: %s"), bWasSuccessful ? TEXT("OK") : TEXT("FAIL")));

    // Proceed with the pending join (if any)
    if (PendingJoinIndex != INDEX_NONE && SessionInterface.IsValid() && LastSearchResults.IsValidIndex(PendingJoinIndex))
    {
        AppendOssStatusLine(FString::Printf(TEXT("Proceeding to Join pending index %d..."), PendingJoinIndex));

        JoinSessionCompleteHandle =
            SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(
                FOnJoinSessionCompleteDelegate::CreateUObject(this, &UMenuWidget::HandleJoinSessionComplete));

        const int32 LocalUserNum = 0;
        const bool bJoinStarted =
            SessionInterface->JoinSession(LocalUserNum, SessionName, LastSearchResults[PendingJoinIndex]);

        if (!bJoinStarted)
        {
            SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
            UE_LOG(LogTemp, Warning, TEXT("JoinSession failed to start after destroy."));
            AppendOssStatusLine(TEXT("ERR: JoinSession failed to start after destroy."));
            ShowJoinDebugSnapshot(TEXT("HandleDestroySessionComplete(JoinStartFailed)"), PendingJoinIndex);
            if (JoinButton) JoinButton->SetIsEnabled(true);
        }
    }
    PendingJoinIndex = INDEX_NONE; // reset
}


void UMenuWidget::HandleFindSessionsComplete(bool bWasSuccessful)
{
    if (SessionInterface.IsValid())
    {
        SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
    }

    LastSearchResults.Reset();

    if (bWasSuccessful && SessionSearch.IsValid())
    {
        LastSearchResults = SessionSearch->SearchResults;
        UE_LOG(LogTemp, Log, TEXT("FindSessions: %d results"), LastSearchResults.Num());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("FindSessions: failed or 0 results."));
    }

    // Log each result to log & status
    UE_LOG(LogTemp, Log, TEXT("FindSessions complete: %d results"), SessionSearch.IsValid() ? SessionSearch->SearchResults.Num() : 0);
    if (SessionSearch.IsValid())
    {
        for (int32 i=0;i<SessionSearch->SearchResults.Num();++i)
        {
            const auto& S = SessionSearch->SearchResults[i].Session;
            FString MapVal; S.SessionSettings.Get(SETTING_MAPNAME, MapVal);
            UE_LOG(LogTemp, Log, TEXT("  [%d] Owner=%s Open=%d/%d Presence=%d Map='%s' LobbyId=%s"),
                i, *S.OwningUserName, S.NumOpenPublicConnections,
                S.SessionSettings.NumPublicConnections,
                (int32)S.SessionSettings.bUsesPresence,
                *MapVal,
                *S.GetSessionIdStr());
        }
    }

    // Update UI snapshot
    ShowJoinDebugSnapshot(TEXT("FindSessionsComplete"));
    if (LastSearchResults.Num() == 0)
    {
        AppendOssStatusLine(TEXT("Info: FindSessions returned 0 results."));
    }

    const TArray<FFoundSessionRow> Rows = GetLastSearchRows();

    // Tell UMG about it
    OnSessionsUpdated.Broadcast(Rows);

    // Optional: auto-join first viable result (host not full)
    if (bAutoJoinFirstResult)
    {
        for (int32 i = 0; i < Rows.Num(); ++i)
        {
            const auto& R = Rows[i];
            if (R.OpenSlots > 0)
            {
                if (EditText) EditText->SetText(FText::FromString(FString::Printf(TEXT("Joining %s…"), *R.OwnerName)));
                JoinSessionByIndex(R.ResultIndex);
                return;
            }
        }

        if (EditText) EditText->SetText(FText::FromString(TEXT("No joinable sessions found.")));
        if (JoinButton) JoinButton->SetIsEnabled(true);
    }
    else
    {
        if (EditText) EditText->SetText(FText::FromString(FString::Printf(TEXT("Found %d sessions."), Rows.Num())));
        if (JoinButton) JoinButton->SetIsEnabled(true);
    }
}

