
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineSessionInterface.h"

#include "MenuWidget.generated.h"

class UButton;
class UEditableTextBox;

#ifndef SETTING_MAPNAME
static const FName SETTING_MAPNAME(TEXT("MAPNAME"));
#endif

#ifndef SEARCH_KEYWORDS
static const FName SEARCH_KEYWORDS(TEXT("SEARCH_KEYWORDS"));
#endif

#ifndef SETTING_PRODUCT
static const FName SETTING_PRODUCT(TEXT("PRODUCT"));  // custom metadata key
#endif

static const TCHAR* const kOurSearchTag = TEXT("Tabletop_Dev");


static const FName SETTING_BUILDID(TEXT("BUILDID"));
static const TCHAR* const kOurProduct  = TEXT("Tabletop");  

USTRUCT(BlueprintType)
struct FFoundSessionRow
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString OwnerName;
    UPROPERTY(BlueprintReadOnly) int32   OpenSlots = 0;
    UPROPERTY(BlueprintReadOnly) int32   MaxSlots  = 0;
    UPROPERTY(BlueprintReadOnly) int32   PingMs    = 0;
    UPROPERTY(BlueprintReadOnly) FString Map;
    UPROPERTY(BlueprintReadOnly) int32   ResultIndex = -1;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSessionsUpdated, const TArray<FFoundSessionRow>&, Rows);


UCLASS()
class TABLETOP_API UMenuWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    // --- Find/Join state ---
    TSharedPtr<class FOnlineSessionSearch> SessionSearch;
    TArray<FOnlineSessionSearchResult>     LastSearchResults;

    FDelegateHandle FindSessionsCompleteHandle;
    FDelegateHandle JoinSessionCompleteHandle;
    FDelegateHandle DestroySessionCompleteHandle;
    
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking")
    FString LobbyMapAssetPath = TEXT("/Game/Maps/L_PreGame");
    
    UPROPERTY(BlueprintAssignable, Category="Networking|Sessions")
    FOnSessionsUpdated OnSessionsUpdated;
    
    // Optional: auto-join the first result (nice for quick test)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Networking|Sessions")
    bool bAutoJoinFirstResult = false;

    UFUNCTION(BlueprintCallable, Category="Networking|Sessions")
    void FindSessions_Steam();

    UFUNCTION(BlueprintCallable, Category="Networking|Sessions")
    TArray<FFoundSessionRow> GetLastSearchRows() const;

    UFUNCTION(BlueprintCallable, Category="Networking|Sessions")
    void JoinSessionByIndex(int32 ResultIndex);

    // Internal handlers
    void HandleFindSessionsComplete(bool bWasSuccessful);
    void HandleJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type Result);
    void HandleDestroySessionComplete(FName InSessionName, bool bWasSuccessful);

    // --- Hosting helpers ---
    UFUNCTION(BlueprintCallable, Category="Networking")
    void OpenAsListen_ByName(const FString& LevelNameOrPath);

    UFUNCTION(BlueprintCallable, Category="Networking")
    void OpenAsListen_ByWorld(TSoftObjectPtr<UWorld> MapAsset);

    UFUNCTION(BlueprintCallable, Category="Networking")
    void OpenAsListen_ByLevel(ULevel* Level);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking")
    FString DefaultJoinAddress = TEXT("127.0.0.1:7777");

    // 🔎 Debug: shows current OSS/Steam/session state
    UPROPERTY(meta=(BindWidgetOptional))
    UEditableTextBox* OssStatusBox = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking|Debug", meta=(ClampMin="0.0"))
    float JoinFailHoldSeconds = 4.0f;

    FTimerHandle JoinFailHoldTimer;
    bool bShuttingDownForTravel = false;
    bool bHoldStatusRefresh = false;
    
    int32 PendingJoinIndex = INDEX_NONE;

    FString JoinResultToString(EOnJoinSessionCompleteResult::Type Result) const;
    FString SummarizeSearchResult(const FOnlineSessionSearchResult& R) const;
    void    SetOssStatus(const FString& Text);
    void    AppendOssStatusLine(const FString& Line);
    void    ShowJoinDebugSnapshot(const TCHAR* Context, int32 DesiredIndex = INDEX_NONE);
    

    // Optional: fixed BuildId you intend to use in dev (shown in status)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking|Debug")
    int32 IntendedBuildId = 1337;

    void UpdateOssStatusSummary();

    void HandleJoinTimeout();
    FTimerHandle JoinTimeoutHandle;

    UPROPERTY(EditAnywhere, Category="Networking")
    float JoinTimeoutSeconds = 30.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking")
    bool bUseOnlineSessions = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking")
    int32 NumPublicConnections = 2;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking")
    bool bIsLANMatch = false; // Steam = false

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking")
    bool bAllowJoinInProgress = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking", meta=(AllowedClasses="/Script/Engine.World"))
    TSoftObjectPtr<UWorld> HostMapAsset;
    
    // Optional: track the last normalized map name
    UPROPERTY() FName CurrentMapName;

    FTimerHandle OssStatusRefreshHandle;

    UPROPERTY(BlueprintReadWrite, Category="Networking")
    ULevel* HostLevelOverride = nullptr;

    UFUNCTION(BlueprintCallable, Category="Networking")
    void SetHostLevel(ULevel* InLevel) { HostLevelOverride = InLevel; }

    void HandleTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& ErrorString);
    void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type Type, const FString& ErrorString);

    UFUNCTION() void SetJoinFailText(const FString& FriendlyMessage);
    UFUNCTION() static FString FriendlyTravelText(ETravelFailure::Type Type, const FString& Reason);
    UFUNCTION() static FString FriendlyNetworkText(ENetworkFailure::Type Type, const FString& Reason);

    FString LastJoinAttempt;
    static constexpr int32 kPort = 7777;

protected:
    UPROPERTY(meta=(BindWidget)) UButton* HostButton = nullptr;
    UPROPERTY(meta=(BindWidget)) UButton* JoinButton = nullptr;
    UPROPERTY(meta=(BindWidget)) UButton* QuitButton = nullptr;

    UPROPERTY(BlueprintReadWrite, meta=(BindWidgetOptional))
    UEditableTextBox* EditText = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UEditableTextBox* MapPathBox = nullptr;

private:
    static FName NormalizeLevelName(const FString& LevelNameOrPath);
    static FName PackageNameFromLevel(const ULevel* Level);

    UFUNCTION() void OnHostClicked();
    UFUNCTION() void OnJoinClicked();
    UFUNCTION() void OnQuitClicked();

    FString ResolveLobbyMapPath() const;
    FString ResolveJoinAddress() const;

    void CreateSessionOrFallback();
    void HandleDestroyThenCreate(FName InSessionName, bool bWasSuccessful);
    void OpenLobbyAsListen();

    void BindSessionDelegates();
    void UnbindSessionDelegates();
    void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);

    TSharedPtr<class IOnlineSession, ESPMode::ThreadSafe> SessionInterface;
    FDelegateHandle CreateSessionCompleteHandle;

    FName SessionName = NAME_GameSession;
};