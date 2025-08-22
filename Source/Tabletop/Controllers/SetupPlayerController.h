
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Tabletop/ArmyData.h"
#include "SetupPlayerController.generated.h"

class USetupWidget;
UCLASS()
class TABLETOP_API ASetupPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_SetReady(bool bReady);

	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_AdvanceFromLobby();
	
	virtual void BeginPlay() override;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI")
	TSubclassOf<UUserWidget> SetupWidgetClass;

	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_SnapshotSetupToPS();
	
	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_SelectFaction(EFaction Faction);

	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_AdvanceFromArmy();

	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_SetUnitCount(FName UnitRow, int32 Count);

	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_AdvanceFromUnits();

	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_SelectMapRow(FName MapRow);

	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_AdvanceFromMap();
	
private:
	UPROPERTY()
	USetupWidget* SetupWidgetInstance;
	
};
