
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

	UFUNCTION(Server, Reliable)
	void Server_SetDisplayName(const FString& InName);

	virtual void BeginPlayingState() override;

	UFUNCTION(Server, Reliable)
	void Server_ReportClientWorldReady(FName WorldPackageName);

	UPROPERTY()
	FName LastReportedMap;
	
	virtual void BeginPlay() override;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI")
	TSubclassOf<UUserWidget> SetupWidgetClass;

	UPROPERTY() TWeakObjectPtr<USetupWidget> SetupUI;

	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_SnapshotSetupToPS();
	
	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_SelectFaction(EFaction Faction);

	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_AdvanceFromArmy();

	UFUNCTION(Server, Reliable)
	void Server_SetUnitCount(FName UnitId, int32 WeaponIndex, int32 NewCount);

	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_AdvanceFromUnits();

	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_SelectMapRow(FName MapRow);

	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_AdvanceFromMap();

	UFUNCTION(Server, Reliable)
	void Server_RequestLobbySync();
	
private:
	UPROPERTY()
	USetupWidget* SetupWidgetInstance;
	
};
