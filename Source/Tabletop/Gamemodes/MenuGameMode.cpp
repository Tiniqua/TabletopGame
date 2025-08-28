
#include "MenuGameMode.h"

#include "GameFramework/SpectatorPawn.h"
#include "Tabletop/Controllers/MenuPlayerController.h"

AMenuGameMode::AMenuGameMode()
{
	bUseSeamlessTravel = true;
	PlayerControllerClass = AMenuPlayerController::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();
	bStartPlayersAsSpectators = true;
}
