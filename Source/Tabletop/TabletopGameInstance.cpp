
#include "TabletopGameInstance.h"

#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineIdentityInterface.h"

static bool IsSteamIdentityReady()
{
	if (IOnlineSubsystem* OSS = IOnlineSubsystem::Get())
		if (IOnlineIdentityPtr Id = OSS->GetIdentityInterface())
			return Id->GetUniquePlayerId(0).IsValid();
	return false;
}

void UTabletopGameInstance::Init()
{
	Super::Init();

	if (IOnlineSubsystem* OSS = IOnlineSubsystem::Get())
	{
		UE_LOG(LogTemp, Log, TEXT("Active OSS: %s"), *OSS->GetSubsystemName().ToString());

		SessionInterface = OSS->GetSessionInterface();
		if (SessionInterface.IsValid())
		{
			InviteAcceptedHandle =
				SessionInterface->AddOnSessionUserInviteAcceptedDelegate_Handle(
					FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &UTabletopGameInstance::OnInviteAccepted));

			InviteReceivedHandle =
				SessionInterface->AddOnSessionInviteReceivedDelegate_Handle(
					FOnSessionInviteReceivedDelegate::CreateUObject(this, &UTabletopGameInstance::OnInviteReceived));

			// 🔑 Listen for join completion here (used when joining via invite)
			JoinCompleteHandle =
				SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(
					FOnJoinSessionCompleteDelegate::CreateUObject(this, &UTabletopGameInstance::OnJoinComplete));
		}
	}
}

void UTabletopGameInstance::OnInviteReceived(const FUniqueNetId&, const FUniqueNetId&, const FString&,
	const FOnlineSessionSearchResult&)
{
}

void UTabletopGameInstance::OnInviteAccepted(
	const bool bWasSuccessful, const int32 /*ControllerId*/,
	TSharedPtr<const FUniqueNetId> /*UserId*/,
	const FOnlineSessionSearchResult& InviteResult)
{
	if (!bWasSuccessful || !SessionInterface.IsValid()) return;

	// Destroy our existing session first, then join in the callback.
	if (SessionInterface->GetNamedSession(NAME_GameSession))
	{
		PendingInviteResult = InviteResult;
		DestroyForInviteHandle =
			SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(
				FOnDestroySessionCompleteDelegate::CreateUObject(
					this, &UTabletopGameInstance::HandleDestroyThenJoinFromInvite));
		SessionInterface->DestroySession(NAME_GameSession);
		return;
	}

	// >>> 5.5 fixup like AdvancedSessions
	FOnlineSessionSearchResult Fixed = InviteResult;
	if (!Fixed.Session.SessionSettings.bIsDedicated) {
		Fixed.Session.SessionSettings.bUsesPresence = true;
		Fixed.Session.SessionSettings.bUseLobbiesIfAvailable = true;
	}

	// Ensure we’re listening for completion
	SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
	JoinCompleteHandle =
		SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(
			FOnJoinSessionCompleteDelegate::CreateUObject(
				this, &UTabletopGameInstance::OnJoinSessionComplete));

	SessionInterface->JoinSession(/*LocalUserNum*/0, NAME_GameSession, Fixed);
}

void UTabletopGameInstance::Shutdown()
{
	if (SessionInterface.IsValid())
	{
		if (InviteAcceptedHandle.IsValid())
			SessionInterface->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteAcceptedHandle);
		if (InviteReceivedHandle.IsValid())
			SessionInterface->ClearOnSessionInviteReceivedDelegate_Handle(InviteReceivedHandle);
		if (JoinCompleteHandle.IsValid())
			SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
		if (DestroyForInviteHandle.IsValid())
			SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroyForInviteHandle);
	}
	Super::Shutdown();
}

void UTabletopGameInstance::HandleDestroyThenJoinFromInvite(FName InSessionName, bool bWasSuccessful)
{
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroyForInviteHandle);
	}

	if (!SessionInterface.IsValid() || !PendingInviteResult.IsValid()) return;

	const int32 LocalUserNum = 0;
	const bool bStarted = SessionInterface->JoinSession(LocalUserNum, NAME_GameSession, PendingInviteResult);
	UE_LOG(LogTemp, Log, TEXT("JoinSession (after destroy) started: %s"), bStarted ? TEXT("Yes") : TEXT("No"));

	PendingInviteResult = FOnlineSessionSearchResult(); // clear
}

void UTabletopGameInstance::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	UE_LOG(LogTemp, Log, TEXT("OnJoinSessionComplete: %s result=%d"), *SessionName.ToString(), (int32)Result);

	if (!SessionInterface.IsValid())
		return;

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("Join failed (%d) after invite"), (int32)Result);
		return;
	}

	FString ConnectStr;
	if (!SessionInterface->GetResolvedConnectString(SessionName, ConnectStr, NAME_GamePort) &&
		!SessionInterface->GetResolvedConnectString(SessionName, ConnectStr))
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not resolve connect string after join."));
		return;
	}

	if (UGameInstance::GetFirstLocalPlayerController(GetWorld()))
	{
		APlayerController* PC = UGameInstance::GetFirstLocalPlayerController(GetWorld());
		UE_LOG(LogTemp, Log, TEXT("ClientTravel to %s"), *ConnectStr);
		PC->ClientTravel(ConnectStr, ETravelType::TRAVEL_Absolute);
	}
}

void UTabletopGameInstance::HandleJoinCompleteGI(FName InSessionName, EOnJoinSessionCompleteResult::Type Result)
{
	if (!SessionInterface.IsValid())
		return;

	UE_LOG(LogTemp, Log, TEXT("GI Join complete: %d"), (int32)Result);
	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		// Log more detail if possible
		return;
	}

	FString ConnectStr;
	if (!SessionInterface->GetResolvedConnectString(InSessionName, ConnectStr, NAME_GamePort))
		if (!SessionInterface->GetResolvedConnectString(InSessionName, ConnectStr))
		{
			UE_LOG(LogTemp, Warning, TEXT("GI could not resolve connect string."));
			return;
		}

	if (APlayerController* PC = GetFirstLocalPlayerController())
	{
		UE_LOG(LogTemp, Log, TEXT("GI ClientTravel -> %s"), *ConnectStr);
		PC->ClientTravel(ConnectStr, ETravelType::TRAVEL_Absolute);
	}
}

void UTabletopGameInstance::OnJoinComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type Result)
{
	if (!SessionInterface.IsValid())
		return;

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("Invite join failed: %d"), (int32)Result);
		return;
	}

	FString Connect;
	if (!SessionInterface->GetResolvedConnectString(InSessionName, Connect, NAME_GamePort))
	{
		SessionInterface->GetResolvedConnectString(InSessionName, Connect); // fallback
	}

	if (Connect.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Invite join: empty connect string"));
		return;
	}

	if (UGameViewportClient* Viewport = GetGameViewportClient())
	{
		if (APlayerController* PC = Viewport->GetWorld()->GetFirstPlayerController())
		{
			UE_LOG(LogTemp, Log, TEXT("Invite join: ClientTravel to %s"), *Connect);
			PC->ClientTravel(Connect, TRAVEL_Absolute);
		}
	}
}