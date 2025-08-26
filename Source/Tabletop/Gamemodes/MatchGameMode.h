
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "MatchGameMode.generated.h"

enum class ECoverType : uint8;
class AMatchPlayerController;
class AUnitBase;


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
	Shoot
};

USTRUCT()
struct FCombatPreview
{
	GENERATED_BODY()
	UPROPERTY() AUnitBase* Attacker = nullptr;
	UPROPERTY() AUnitBase* Target   = nullptr;
	UPROPERTY() ETurnPhase Phase    = ETurnPhase::Move;
};

// ignore bad dupe im lazy
USTRUCT()
struct FActionPreview
{
	GENERATED_BODY()

	UPROPERTY() class AUnitBase* Attacker = nullptr;
	UPROPERTY() class AUnitBase* Target   = nullptr;
	UPROPERTY() ETurnPhase Phase = ETurnPhase::Move;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeploymentChanged);

UCLASS()
class TABLETOP_API AMatchGameState : public AGameStateBase
{
	GENERATED_BODY()
public:

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_DrawShotDebug(const FVector& WorldLoc, const FString& Msg,
								 FColor Color = FColor::Yellow, float Duration = 4.f);

	UPROPERTY(ReplicatedUsing=OnRep_Match)
	FCombatPreview Preview;
	UFUNCTION() void OnRep_Match(){ OnDeploymentChanged.Broadcast(); }
	
	UPROPERTY(Replicated)
	bool bTeamsAndTurnsInitialized = false;

	UPROPERTY(ReplicatedUsing=OnRep_Preview)
	FActionPreview ActionPreview;

	UPROPERTY(Transient, BlueprintReadOnly)
	TArray<class AObjectiveMarker*> Objectives;

	UFUNCTION() void OnRep_Preview() { OnDeploymentChanged.Broadcast(); }

	UPROPERTY(ReplicatedUsing=OnRep_Match) uint8 CurrentRound = 1;
	UPROPERTY(ReplicatedUsing=OnRep_Match) uint8 MaxRounds = 5;
	UPROPERTY(ReplicatedUsing=OnRep_Match) uint8 TurnInRound = 0; // 0=first player's turn, 1=second
	UPROPERTY(ReplicatedUsing=OnRep_Match) ETurnPhase TurnPhase = ETurnPhase::Move;
	UPROPERTY(ReplicatedUsing=OnRep_Match) APlayerState* CurrentTurn = nullptr;

	UPROPERTY(ReplicatedUsing=OnRep_Match) int32 ScoreP1 = 0;
	UPROPERTY(ReplicatedUsing=OnRep_Match) int32 ScoreP2 = 0;
	
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
	virtual void BeginPlay() override;
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
	void ScoreObjectivesForRound();
	void NotifyUnitTransformChanged(AUnitBase* Changed);

	void FinalizePlayerJoin(APlayerController* PC);
	void TallyObjectives_EndOfRound();

	void ResolveMoveToBudget(const AUnitBase* U, const FVector& WantedDest, FVector& OutFinalDest, float& OutSpentTTIn,
	                         bool& bOutClamped) const;
	// Movement
	bool ValidateMove(AUnitBase* Unit, const FVector& Dest, float& OutDistInches) const;
	void Handle_MoveUnit(AMatchPlayerController* PC, AUnitBase* Unit, const FVector& Dest);

	// Targeting & shooting
	bool ValidateShoot(AUnitBase* Attacker, AUnitBase* Target) const;
	void Handle_SelectTarget(AMatchPlayerController* PC, AUnitBase* Attacker, AUnitBase* Target);
	void Handle_ConfirmShoot(AMatchPlayerController* PC, AUnitBase* Attacker, AUnitBase* Target);
	
	// Cancels any active GS->Preview if this PC owns the Attacker and it's their turn
	void Handle_CancelPreview(class AMatchPlayerController* PC, class AUnitBase* Attacker);
	
	// Per-turn reset (call when starting Move for CurrentTurn)
	void ResetTurnFor(APlayerState* PS);
	
	float CmPerTabletopInch() const { return 2.54f * TabletopToUnrealInchScale; }

	UPROPERTY(EditDefaultsOnly, Category="Cover")
	TEnumAsByte<ECollisionChannel> CoverTraceChannel = ECC_GameTraceChannel4;

	// hard cap to keep perf predictable when sampling per-model
	UPROPERTY(EditDefaultsOnly, Category="Cover")
	int32 MaxCoverSamplesPerUnit = 4;

	// NEW: proximity window for cover validity (tabletop inches)
	UPROPERTY(EditDefaultsOnly, Category="Cover")
	float CoverProximityInches = 6.f;

	// NEW: toggle draw-debug lines/text for cover traces
	UPROPERTY(EditDefaultsOnly, Category="Cover|Debug")
	bool bDebugCoverTraces = true;

	// Simple center-to-center line; returns first cover hit (if any)
	bool ComputeCoverBetween(const FVector& From, const FVector& To, ECoverType& OutType) const;

	// Full query with per-model fallback; returns modifiers
	bool QueryCover(class AUnitBase* Attacker, class AUnitBase* Target,
					int32& OutHitMod, int32& OutSaveMod, ECoverType& OutType) const;

	


protected:
	virtual void BeginPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;

private:
	UFUNCTION(BlueprintCallable, Category="Round")
	void ResetUnitRoundStateFor(class APlayerState* TurnOwner);
	
	class AMatchGameState* GS() const { return GetGameState<AMatchGameState>(); }

	APlayerState* OtherPlayer(APlayerState* PS) const;

	UPROPERTY(EditAnywhere, Category="Scale")
	float TabletopToUnrealInchScale = 20.f; // 1 tabletop inch == 20 UE inches (exact). Use 19.685 for 50 cm/in.

	// Helper (cm per tabletop inch)
	
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
