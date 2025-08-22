
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "MatchGameMode.generated.h"

enum class EFaction : uint8;
struct FUnitCount;
UENUM(BlueprintType)
enum class EMatchPhase : uint8
{
	Deployment,
	Battle,
	EndGame
};

UENUM(BlueprintType)
enum class ETurnPhase : uint8
{
	Move,
	Shoot,
	Charge,
	Fight
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeploymentChanged);

UCLASS()
class TABLETOP_API AMatchGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	
	UPROPERTY(Replicated)
	bool bTeamsAndTurnsInitialized = false;

	UPROPERTY(ReplicatedUsing=OnRep_Match) uint8 CurrentRound = 1;
	UPROPERTY(ReplicatedUsing=OnRep_Match) uint8 MaxRounds = 5;
	UPROPERTY(ReplicatedUsing=OnRep_Match) uint8 TurnInRound = 0; // 0=first player's turn, 1=second
	UPROPERTY(ReplicatedUsing=OnRep_Match) ETurnPhase TurnPhase = ETurnPhase::Move;
	UPROPERTY(ReplicatedUsing=OnRep_Match) APlayerState* CurrentTurn = nullptr;

	UPROPERTY(ReplicatedUsing=OnRep_Match) int32 ScoreP1 = 0;
	UPROPERTY(ReplicatedUsing=OnRep_Match) int32 ScoreP2 = 0;

	UFUNCTION() void OnRep_Match() { OnDeploymentChanged.Broadcast(); }
	
	UPROPERTY(ReplicatedUsing=OnRep_Deployment) EMatchPhase Phase = EMatchPhase::Deployment;

	// Who can deploy right now (alternates)
	UPROPERTY(ReplicatedUsing=OnRep_Deployment) APlayerState* CurrentDeployer = nullptr;

	// Remaining counts to place (copied from each PlayerState.Roster at start)
	UPROPERTY(ReplicatedUsing=OnRep_Deployment) TArray<FUnitCount> P1Remaining;
	UPROPERTY(ReplicatedUsing=OnRep_Deployment) TArray<FUnitCount> P2Remaining;

	// True once both remaining arrays are empty
	UPROPERTY(ReplicatedUsing=OnRep_Deployment) bool bDeploymentComplete = false;

	// For quick name/faction display
	UPROPERTY(ReplicatedUsing=OnRep_Players) APlayerState* P1 = nullptr;
    UPROPERTY(ReplicatedUsing=OnRep_Players) APlayerState* P2 = nullptr;

	UPROPERTY(BlueprintAssignable) FOnDeploymentChanged OnDeploymentChanged;

	UFUNCTION() void OnRep_Deployment() { OnDeploymentChanged.Broadcast(); }
	UFUNCTION() void OnRep_Players()    { OnDeploymentChanged.Broadcast(); }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
};

UCLASS()
class TABLETOP_API AMatchGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AMatchGameMode();
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;

	// Needed to look up unit display/icon (optional for spawning)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	UDataTable* FactionsTable = nullptr;

	// Server RPC endpoints (called by PC server functions)
	void HandleRequestDeploy(class APlayerController* PC, FName UnitId, const FTransform& Where);
	void HandleStartBattle(class APlayerController* PC);
	void HandleEndPhase(class APlayerController* PC);

	void FinalizePlayerJoin(APlayerController* PC);



protected:
	virtual void BeginPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;

private:
	class AMatchGameState* GS() const { return GetGameState<AMatchGameState>(); }

	APlayerState* OtherPlayer(APlayerState* PS) const;
	

	static int32 FindIdx(TArray<FUnitCount>& Arr, FName Unit);
	bool CanDeployAt(APlayerController* PC, const FVector& WorldLocation) const;
	void Server_RequestDeploy_Implementation(APlayerController* PC, TSubclassOf<AActor> UnitClass,
	                                         const FTransform& DesiredTransform);
	void CopyRostersFromPlayerStates();
	void RollFirstDeployer();
	bool AnyRemainingFor(APlayerState* PS) const;
	bool DecrementOne(APlayerState* PS, FName UnitId);
	void AdvanceTurnAfterSuccessfulDeploy();
	void FinishDeployment();
	UDataTable* UnitsForFaction(EFaction Faction) const;
	TSubclassOf<AActor> UnitClassFor(APlayerState* ForPS, FName UnitId) const;
};
