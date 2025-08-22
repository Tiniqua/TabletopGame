
#include "MenuPlayerController.h"

#include "Blueprint/UserWidget.h"

void AMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController() && MenuWidgetClass)
	{
		MenuWidgetInstance = CreateWidget<UUserWidget>(this, MenuWidgetClass);
		if (MenuWidgetInstance)
		{
			MenuWidgetInstance->AddToViewport();

			FInputModeGameAndUI Mode;
			Mode.SetHideCursorDuringCapture(false);
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Mode.SetWidgetToFocus(MenuWidgetInstance->TakeWidget());
			SetShowMouseCursor(true);
			bShowMouseCursor = true;
			SetInputMode(Mode);
		}
	}
}
