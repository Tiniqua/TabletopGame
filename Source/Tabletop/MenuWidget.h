
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MenuWidget.generated.h"

class UButton;
class UEditableTextBox;

UCLASS()
class TABLETOP_API UMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    /** Open the given map as a listen server using a level name or full asset path. */
    UFUNCTION(BlueprintCallable, Category="Networking")
    void OpenAsListen_ByName(const FString& LevelNameOrPath);

    /** Open the given map as a listen server using a soft reference to a UWorld map asset. */
    UFUNCTION(BlueprintCallable, Category="Networking")
    void OpenAsListen_ByWorld(TSoftObjectPtr<UWorld> MapAsset);

    /** Open using a ULevel pointer (we resolve its package name). Only valid for a persistent level. */
    UFUNCTION(BlueprintCallable, Category="Networking")
    void OpenAsListen_ByLevel(ULevel* Level);

    /** Full asset path of your lobby/map (packaged-friendly). e.g. /Game/Maps/Lobby */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking")
    FString LobbyMapAssetPath = TEXT("/Game/Maps/Lobby");

    /** Default IP:Port if there is no IP text box present or it is empty */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking")
    FString DefaultJoinAddress = TEXT("127.0.0.1:7777");

    void HandleJoinTimeout();
    FTimerHandle JoinTimeoutHandle;

    UPROPERTY(EditAnywhere, Category="Networking")
    float JoinTimeoutSeconds = 10.f;

    /** If true, try CreateSession (LAN by default) then open map as listen on success.
        If false, host just directly opens the map with ?listen. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking")
    bool bUseOnlineSessions = true;

    /** Session settings (used only when bUseOnlineSessions is true) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking")
    int32 NumPublicConnections = 2;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking")
    bool bIsLANMatch = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking")
    bool bAllowJoinInProgress = true;

    // --- New: preferred packaged-safe map asset (optional fallback) ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Networking", meta=(AllowedClasses="World"))
    TSoftObjectPtr<UWorld> HostMapAsset;

    // --- New: runtime ULevel* override (what you asked for) ---
    // Not useful as a default in asset editor; set it at runtime via SetHostLevel()
    UPROPERTY(BlueprintReadWrite, Category="Networking")
    ULevel* HostLevelOverride = nullptr;

    UFUNCTION(BlueprintCallable, Category="Networking")
    void SetHostLevel(ULevel* InLevel) { HostLevelOverride = InLevel; }
    
    void HandleTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& ErrorString);
    
    void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type Type, const FString& ErrorString);

    UFUNCTION()
    void SetJoinFailText(const FString& FriendlyMessage);
    UFUNCTION()
    static FString FriendlyTravelText(ETravelFailure::Type Type, const FString& Reason);
    UFUNCTION()
    static FString FriendlyNetworkText(ENetworkFailure::Type Type, const FString& Reason);

    // Remember what user tried so we can suggest it back
    FString LastJoinAttempt;

    static constexpr int32 kPort = 7777;

protected:
    /** Bound buttons (must be named exactly in the BP: HostButton, JoinButton, QuitButton) */
    UPROPERTY(meta=(BindWidget))
    UButton* HostButton = nullptr;

    UPROPERTY(meta=(BindWidget))
    UButton* JoinButton = nullptr;

    UPROPERTY(meta=(BindWidget))
    UButton* QuitButton = nullptr;

    /** Optional text boxes (if you have them in your BP). Safe to omit in the BP. */
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

    /* ---- Session helpers (only used if bUseOnlineSessions) ---- */
    void CreateSessionOrFallback();
    void OpenLobbyAsListen();

    void BindSessionDelegates();
    void UnbindSessionDelegates();

    void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);

    /** Online Subsystem session interface */
    TSharedPtr<class IOnlineSession, ESPMode::ThreadSafe> SessionInterface;
    FDelegateHandle CreateSessionCompleteHandle;

    FName SessionName = NAME_GameSession;
};
