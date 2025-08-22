
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MenuPlayerController.generated.h"

UCLASS()
class TABLETOP_API AMenuPlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	virtual void BeginPlay() override;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI")
	TSubclassOf<UUserWidget> MenuWidgetClass;

private:
	UPROPERTY()
	UUserWidget* MenuWidgetInstance;
};
