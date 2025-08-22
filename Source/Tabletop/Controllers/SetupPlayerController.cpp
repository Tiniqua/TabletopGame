
#include "SetupPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Tabletop/SetupWidget.h"
#include "Tabletop/Gamemodes/SetupGamemode.h"

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

void ASetupPlayerController::Server_SetUnitCount_Implementation(FName UnitRow, int32 Count)
{
	if (ASetupGamemode* GM = GetWorld()->GetAuthGameMode<ASetupGamemode>())
	{
		GM->HandleSetUnitCount(this, Count < 0 ? UnitRow : UnitRow, FMath::Max(0, Count));
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
