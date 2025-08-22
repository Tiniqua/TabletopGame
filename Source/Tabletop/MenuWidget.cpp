
#include "MenuWidget.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"

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
}

void UMenuWidget::OpenAsListen_ByLevel(ULevel* Level)
{
    if (!Level)
    {
        UE_LOG(LogTemp, Warning, TEXT("OpenAsListen_ByLevel: Level is null."));
        return;
    }

    const UWorld* OwningWorld = Level->GetTypedOuter<UWorld>();
    if (!OwningWorld)
    {
        UE_LOG(LogTemp, Warning, TEXT("OpenAsListen_ByLevel: No owning world."));
        return;
    }

    // If this is a sub-level, traveling to its owning persistent world is what you actually want.
    if (OwningWorld->PersistentLevel != Level)
    {
        UE_LOG(LogTemp, Warning, TEXT("OpenAsListen_ByLevel: Provided ULevel is a sub-level; using the persistent world instead."));
    }

    const FName PackageName = OwningWorld->GetOutermost()->GetFName();
    if (PackageName.IsNone())
    {
        UE_LOG(LogTemp, Warning, TEXT("OpenAsListen_ByLevel: Could not resolve package name."));
        return;
    }

    if (UWorld* World = GetWorld())
    {
        UGameplayStatics::OpenLevel(World, PackageName, /*bAbsolute*/ true, TEXT("listen"));
    }
}

void UMenuWidget::OpenAsListen_ByWorld(TSoftObjectPtr<UWorld> MapAsset)
{
    if (UWorld* World = GetWorld())
    {
        FName PackageName = NAME_None;

        if (!MapAsset.IsNull())
        {
            // Use GetLongPackageName() which returns a string like "/Game/Maps/Lobby"
            const FString PathString = MapAsset.ToSoftObjectPath().GetLongPackageName();
            PackageName = FName(*PathString);
        }

        if (!PackageName.IsNone())
        {
            UGameplayStatics::OpenLevel(World, PackageName, true, TEXT("listen"));
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
        const FName MapName = NormalizeLevelName(LevelNameOrPath);
        if (!MapName.IsNone())
        {
            UGameplayStatics::OpenLevel(World, MapName, true, TEXT("listen"));
        }
    }
}

FString UMenuWidget::ResolveLobbyMapPath() const
{
    if (MapPathBox)
    {
        const FString FromBox = MapPathBox->GetText().ToString().TrimStartAndEnd();
        if (!FromBox.IsEmpty())
        {
            return FromBox;
        }
    }
    return LobbyMapAssetPath;
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

/* -------------------- Button handlers -------------------- */

void UMenuWidget::OnHostClicked()
{
    if (bUseOnlineSessions)
    {
        CreateSessionOrFallback();
    }
    else
    {
        OpenLobbyAsListen();
    }
}

void UMenuWidget::OnJoinClicked()
{
    APlayerController* PC = GetOwningPlayer();
    if (!PC) { return; }

    const FString Address = ResolveJoinAddress();
    PC->ClientTravel(Address, TRAVEL_Absolute);
}

void UMenuWidget::OnQuitClicked()
{
    UKismetSystemLibrary::QuitGame(GetWorld(), GetOwningPlayer(), EQuitPreference::Quit, /*bIgnorePlatformRestrictions*/ false);
}

/* -------------------- Host helpers -------------------- */

void UMenuWidget::OpenLobbyAsListen()
{
    // Priority: 1) ULevel* override, 2) soft UWorld asset, 3) string path
    if (HostLevelOverride)
    {
        OpenAsListen_ByLevel(HostLevelOverride);
        return;
    }
    if (HostMapAsset.IsValid() || !HostMapAsset.IsNull())
    {
        OpenAsListen_ByWorld(HostMapAsset);
        return;
    }

    const FString MapPath = ResolveLobbyMapPath();
    OpenAsListen_ByName(MapPath);
}

/* -------------------- Sessions (optional) -------------------- */

void UMenuWidget::CreateSessionOrFallback()
{
    IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
    if (!Subsystem)
    {
        UE_LOG(LogTemp, Warning, TEXT("No OnlineSubsystem; falling back to direct listen host."));
        OpenLobbyAsListen();
        return;
    }

    SessionInterface = Subsystem->GetSessionInterface();
    if (!SessionInterface.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("No SessionInterface; falling back to direct listen host."));
        OpenLobbyAsListen();
        return;
    }

    // If a session already exists, destroy it first (simple approach for demos)
    if (SessionInterface->GetNamedSession(SessionName))
    {
        SessionInterface->DestroySession(SessionName);
    }

    FOnlineSessionSettings Settings;
    Settings.bIsLANMatch = bIsLANMatch;
    Settings.bShouldAdvertise = true;
    Settings.bAllowJoinInProgress = bAllowJoinInProgress;
    Settings.NumPublicConnections = FMath::Max(NumPublicConnections, 1);
    Settings.bUsesPresence = false;             // set true if using presence (Steam/EOS presence)
    Settings.bAllowInvites = false;
    Settings.bAllowJoinViaPresence = false;
    Settings.bAllowJoinViaPresenceFriendsOnly = false;

    BindSessionDelegates();

    const int32 LocalUserNum = 0;
    const bool bStarted = SessionInterface->CreateSession(LocalUserNum, SessionName, Settings);
    if (!bStarted)
    {
        UE_LOG(LogTemp, Warning, TEXT("CreateSession failed to start; falling back to direct listen host."));
        UnbindSessionDelegates();
        OpenLobbyAsListen();
    }
}

void UMenuWidget::BindSessionDelegates()
{
    if (SessionInterface.IsValid())
    {
        CreateSessionCompleteHandle =
            SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
                FOnCreateSessionCompleteDelegate::CreateUObject(
                    this, &UMenuWidget::HandleCreateSessionComplete));
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

    if (bWasSuccessful)
    {
        UE_LOG(LogTemp, Log, TEXT("CreateSession succeeded. Opening lobby as listen..."));
        OpenLobbyAsListen();
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("CreateSession failed; opening lobby as direct listen host."));
        OpenLobbyAsListen();
    }
}