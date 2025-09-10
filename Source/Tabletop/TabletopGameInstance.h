
#pragma once

#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "CoreMinimal.h"
#include "OnlineSessionSettings.h"
#include "Engine/GameInstance.h"
#include "TabletopGameInstance.generated.h"

UCLASS()
class TABLETOP_API UTabletopGameInstance : public UGameInstance
{
	GENERATED_BODY()

	FDelegateHandle InviteAcceptedHandle;
	FDelegateHandle InviteReceivedHandle;
	FDelegateHandle JoinCompleteHandle;                          // NEW
	TSharedPtr<IOnlineSession, ESPMode::ThreadSafe> SessionInterface;

	virtual void Init() override;

	// Handlers
	void OnInviteReceived(const FUniqueNetId& /*UserId*/, const FUniqueNetId& /*FromId*/,
						  const FString& /*AppId*/, const FOnlineSessionSearchResult& /*InviteResult*/);

	void OnInviteAccepted(const bool bWasSuccessful, const int32 /*ControllerId*/,
						  TSharedPtr<const FUniqueNetId> /*UserId*/,
						  const FOnlineSessionSearchResult& InviteResult);
	
	virtual void Shutdown() override;
	
	void HandleDestroyThenJoinFromInvite(FName InSessionName, bool bWasSuccessful);

	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result); // NEW


	void HandleJoinCompleteGI(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	
	void OnJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

	FOnlineSessionSearchResult PendingInviteResult;
	FDelegateHandle DestroyForInviteHandle;
};
