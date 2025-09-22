
#include "SetupPlayerController.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "GameFramework/PlayerState.h"
#include "Tabletop/SetupWidget.h"
#include "Tabletop/Gamemodes/MatchGameMode.h"
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

void ASetupPlayerController::BeginPlayingState()
{
	Super::BeginPlayingState();

	// Only the client should report readiness to the server
	if (!HasAuthority() && IsLocalController())
	{
		const FName MapName = FName(*GetWorld()->GetMapName()); // PIE-safe is fine for identity here
		if (MapName != LastReportedMap)
		{
			LastReportedMap = MapName;
			Server_ReportClientWorldReady(MapName);
		}
	}
}

void ASetupPlayerController::Server_ReportClientWorldReady_Implementation(FName WorldPackageName)
{
	if (AMatchGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AMatchGameMode>() : nullptr)
	{
		GM->OnClientReportedLoaded(this);
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
		// Prevent duplicates
		TArray<UUserWidget*> Existing;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Existing, USetupWidget::StaticClass(), /*TopLevelOnly*/ true);
		for (auto* W : Existing) if (W) W->RemoveFromParent();

		USetupWidget* W = CreateWidget<USetupWidget>(this, SetupWidgetClass);
		if (W)
		{
			W->AddToViewport(10);
			bShowMouseCursor = true;
			FInputModeGameAndUI Mode;
			Mode.SetWidgetToFocus(W->TakeWidget());
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
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
	if (ASetupGameState* S = GetWorld()->GetGameState<ASetupGameState>())
	{
		const bool bP1 = (PlayerState == S->Player1);
		UE_LOG(LogTemp, Warning, TEXT("[RPC] Server_SetUnitCount from %s  Unit=%s WIdx=%d New=%d  Seat=%s"),
			*GetNameSafe(this), *UnitId.ToString(), WeaponIndex, NewCount, bP1?TEXT("P1"):TEXT("P2"));
	}
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
