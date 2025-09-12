
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Tabletop/ArmyData.h"
#include "SetupGamemode.generated.h"

UENUM(BlueprintType)
enum class ESetupPhase : uint8
{
	Lobby,          // waiting for 2 players
	ArmySelection,  // pick faction
	UnitSelection,  // pick units to reach points cap
	MapSelection,   // host selects map
	ReadyToStart
};

USTRUCT(BlueprintType)
struct FUnitCount
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName UnitId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Count = 0;
};



static FString BuildListenURL(const TSoftObjectPtr<UWorld>& Map)
{
	const FString Pkg = Map.ToSoftObjectPath().GetLongPackageName(); // "/Game/Maps/YourMap"
	return Pkg.IsEmpty() ? FString() : (Pkg + TEXT("?listen"));
}


DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerSlotsChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerReadyUp);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnArmySelectionChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerReadyUpChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRosterChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMapSelectionChanged);

UCLASS()
class TABLETOP_API ASetupGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	UPROPERTY(ReplicatedUsing=OnRep_PlayerSlots, BlueprintReadOnly)
	FString Player1Name;

	UPROPERTY(ReplicatedUsing=OnRep_PlayerSlots, BlueprintReadOnly)
	FString Player2Name;
	
	UPROPERTY(ReplicatedUsing=OnRep_Phase, BlueprintReadOnly)
	ESetupPhase Phase = ESetupPhase::Lobby;

	UPROPERTY(BlueprintAssignable, Category="Events")
	FOnPlayerSlotsChanged OnPlayerSlotsChanged;

	UPROPERTY(BlueprintAssignable)
	FOnPlayerSlotsChanged OnPhaseChanged;

	UPROPERTY(BlueprintAssignable)
	FOnPlayerReadyUp OnPlayerReadyUp;
	
	UPROPERTY(Replicated, BlueprintReadOnly, ReplicatedUsing=OnRep_PlayerSlots)
	APlayerState* Player1 = nullptr; // Host
	UPROPERTY(Replicated, BlueprintReadOnly, ReplicatedUsing=OnRep_PlayerSlots )
	APlayerState* Player2 = nullptr; // Joiner

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Map")
	UDataTable* MapsTable = nullptr;

	// RowName selected by host
	UPROPERTY(ReplicatedUsing=OnRep_SelectedMap, BlueprintReadOnly, Category="Map")
	FName SelectedMapRow = NAME_None;

	

	UPROPERTY(BlueprintAssignable)
	FOnMapSelectionChanged OnMapSelectionChanged;

	// Army selections per seat
	UPROPERTY(Replicated, ReplicatedUsing=OnRep_ArmySelection, BlueprintReadOnly)
	EFaction P1Faction = EFaction::None;

	UPROPERTY(Replicated, ReplicatedUsing=OnRep_ArmySelection, BlueprintReadOnly)
	EFaction P2Faction = EFaction::None;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Army")
	UDataTable* FactionsTable = nullptr;

	UPROPERTY(BlueprintAssignable)
	FOnArmySelectionChanged OnArmySelectionChanged;
	
	UPROPERTY(Replicated, ReplicatedUsing=OnRep_ReadyUp, BlueprintReadOnly)
	bool bP1Ready = false;
	UPROPERTY(Replicated,ReplicatedUsing=OnRep_ReadyUp, BlueprintReadOnly)
	bool bP2Ready = false;

	UPROPERTY(ReplicatedUsing=OnRep_Rosters, BlueprintReadOnly)
	TArray<FUnitCount> P1Roster;

	UPROPERTY(ReplicatedUsing=OnRep_Rosters, BlueprintReadOnly)
	TArray<FUnitCount> P2Roster;

	UPROPERTY(ReplicatedUsing=OnRep_Rosters, BlueprintReadOnly)
	int32 P1Points = 0;
	UPROPERTY(ReplicatedUsing=OnRep_Rosters, BlueprintReadOnly)
	int32 P2Points = 0;

	UPROPERTY(BlueprintAssignable)
	FOnRosterChanged OnRosterChanged;
	
	UPROPERTY(Replicated, BlueprintReadOnly)
	FName SelectedMap; // Level name/soft ref
	
	UFUNCTION()
	void OnRep_Phase();

	UFUNCTION()
	void OnRep_ArmySelection();
	
	UFUNCTION()
	void OnRep_PlayerSlots();

	UFUNCTION()
	void OnRep_PlayerNames();

	UFUNCTION()
	void OnRep_Rosters();

	UFUNCTION()
	void OnRep_SelectedMap();

	UFUNCTION()
	void OnRep_ReadyUp();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;


};

UCLASS()
class TABLETOP_API ASetupGamemode : public AGameModeBase
{
	GENERATED_BODY()
public:
	void UpdateCachedPlayerNames();
	ASetupGamemode();
	
	virtual void BeginPlay() override;

	UFUNCTION(Exec, BlueprintCallable)
	void Server_SelectMap(const FName MapRowOrLevel);

	// Call when everything is ready to start the match
	UFUNCTION(BlueprintCallable) void StartMatchTravel();
	
	UFUNCTION(BlueprintCallable)
	bool AllPlayersReady();
	UFUNCTION(BlueprintCallable)
	void HandlePlayerReady(APlayerController* PC, bool bReady);
	UFUNCTION(BlueprintCallable)
	void TryAdvanceFromLobby();

	UFUNCTION(BlueprintCallable)
	void HandleSetUnitCount(APlayerController* PC, FName UnitRow, int32 Count);

	UFUNCTION(BlueprintCallable)
	void TryAdvanceFromUnitSelection();

	UFUNCTION(BlueprintCallable)
	void SnapshotPlayerToPS(APlayerController* PC);
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Army")
	UDataTable* FactionsTable = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Map")
	UDataTable* MapsTable = nullptr;

	UFUNCTION(BlueprintCallable)
	void HandleSelectFaction(APlayerController* PC, EFaction Faction);
	
	UFUNCTION(BlueprintCallable)
	void TryAdvanceFromArmySelection();

	UFUNCTION(BlueprintCallable)
	void HandleSelectMap(APlayerController* PC, FName MapRow);

	// Advance MapSelection -> Start game (ServerTravel)
	UFUNCTION(BlueprintCallable)
	void TryAdvanceFromMapSelection();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Lobby")
	int32 MaxPlayerNameChars = 12; // tweak in defaults

	static FString CleanAndClampName(const FString& In, int32 MaxChars);

protected:
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

private:
	ASetupGameState* GS() const { return GetGameState<ASetupGameState>(); }

	UDataTable* GetUnitsTableForFaction(EFaction Faction) const;
	int32 ComputeRosterPoints(UDataTable* UnitsTable, const TMap<FName,int32>& Roster) const;
};
