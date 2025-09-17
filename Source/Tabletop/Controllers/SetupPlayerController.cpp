
#include "SetupPlayerController.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerState.h"
#include "Tabletop/SetupWidget.h"
#include "Tabletop/Gamemodes/SetupGamemode.h"
#include "Tabletop/PlayerStates/TabletopPlayerState.h"

void ASetupPlayerController::Server_SetReady_Implementation(bool bReady)
{
	if (ASetupGamemode* GM = GetWorld()->GetAuthGameMode<ASetupGamemode>())
	{
		GM->HandlePlayerReady(this, bReady);     // plain server method
	}
}

void ASetupPlayerController::Server_AdvanceFromLobby_Implementation()
{
	if (ASetupGamemode* GM = GetWorld()->GetAuthGameMode<ASetupGamemode>())
	{
		GM->TryAdvanceFromLobby();
	}
}

void ASetupPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		// 1) Pull platform/Steam nickname
		FString Nickname;

		if (IOnlineSubsystem* OSS = IOnlineSubsystem::Get())
		{
			IOnlineIdentityPtr Identity = OSS->GetIdentityInterface();
			if (Identity.IsValid())
			{
				// Local user 0 is fine for typical setups
				Nickname = Identity->GetPlayerNickname(0);
			}
		}

		if (Nickname.IsEmpty())
		{
			// sensible fallback
			Nickname = FString::Printf(TEXT("Player_%d"), GetLocalPlayer() ? GetLocalPlayer()->GetControllerId() : 0);
		}

		// 2) Tell the server to store it on our PlayerState
		Server_SetDisplayName(Nickname);
	}

	if (IsLocalController() && SetupWidgetClass)
	{
		SetupWidgetInstance = CreateWidget<USetupWidget>(this, SetupWidgetClass);
		if (SetupWidgetInstance)
		{
			SetupWidgetInstance->AddToViewport();

			FInputModeGameAndUI Mode;
			Mode.SetHideCursorDuringCapture(false);
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Mode.SetWidgetToFocus(SetupWidgetInstance->TakeWidget());
			SetShowMouseCursor(true);
			bShowMouseCursor = true;
			SetInputMode(Mode);
			
		}
	}

	if (IsLocalController())
	{
		Server_RequestLobbySync();
	}
}

void ASetupPlayerController::Server_RequestLobbySync_Implementation()
{
	if (ASetupGamemode* GM = GetWorld()->GetAuthGameMode<ASetupGamemode>())
	{
		GM->UpdateCachedPlayerNames();             // fills Player1Name/Player2Name
		if (ASetupGameState* S = GM->GetGameState<ASetupGameState>())
		{
			S->OnPlayerSlotsChanged.Broadcast();   // host-side UI
			S->ForceNetUpdate();                   // replicate to everyone
		}
	}
}

void ASetupPlayerController::Server_SetDisplayName_Implementation(const FString& InName)
{
	if (APlayerState* PS = PlayerState)
	{
		PS->SetPlayerName(InName);
		if (ATabletopPlayerState* TPS = Cast<ATabletopPlayerState>(PS))
		{
			TPS->DisplayName = InName;
		}
	}

	// Ask GM to recache & replicate the lobby slot strings
	if (ASetupGamemode* GM = GetWorld()->GetAuthGameMode<ASetupGamemode>())
	{
		GM->UpdateCachedPlayerNames();
	}
}

void ASetupPlayerController::Server_SnapshotSetupToPS_Implementation()
{
	if (ASetupGamemode* GM = GetWorld()->GetAuthGameMode<ASetupGamemode>())
	{
		// Let GM copy from GS to this PC’s PlayerState
		GM->SnapshotPlayerToPS(this);
	}
}

void ASetupPlayerController::Server_SelectFaction_Implementation(EFaction Faction)
{
	if (ASetupGamemode* GM = GetWorld()->GetAuthGameMode<ASetupGamemode>())
	{
		GM->HandleSelectFaction(this, Faction);
	}
}

void ASetupPlayerController::Server_AdvanceFromArmy_Implementation()
{
	if (ASetupGamemode* GM = GetWorld()->GetAuthGameMode<ASetupGamemode>())
	{
		GM->TryAdvanceFromArmySelection();
	}
}

void ASetupPlayerController::Server_SetUnitCount_Implementation(FName UnitId, int32 WeaponIndex, int32 NewCount)
{
	if (ASetupGamemode* GM = GetWorld()->GetAuthGameMode<ASetupGamemode>())
	{
		GM->HandleSetUnitCount(this, UnitId, WeaponIndex, FMath::Clamp(NewCount,0,99));
	}
}

void ASetupPlayerController::Server_AdvanceFromUnits_Implementation()
{
	if (ASetupGamemode* GM = GetWorld()->GetAuthGameMode<ASetupGamemode>())
	{
		GM->TryAdvanceFromUnitSelection();
	}
}

void ASetupPlayerController::Server_SelectMapRow_Implementation(FName MapRow)
{
	if (ASetupGamemode* GM = GetWorld()->GetAuthGameMode<ASetupGamemode>())
	{
		GM->HandleSelectMap(this, MapRow);
	}
}

void ASetupPlayerController::Server_AdvanceFromMap_Implementation()
{
	if (ASetupGamemode* GM = GetWorld()->GetAuthGameMode<ASetupGamemode>())
	{
		GM->TryAdvanceFromMapSelection();
	}
}
