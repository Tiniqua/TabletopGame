
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Tabletop/UnitActionResourceComponent.h"
#include "Tabletop/Actors/CoverVolume.h"
#include "Tabletop/Actors/UnitAction.h"

#include "MatchGameMode.generated.h"


class ATabletopPlayerState;
class ANetDebugTextActor;

class AMatchPlayerController;
class AUnitBase;

USTRUCT(BlueprintType)
struct FSurvivorEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FText UnitName;
	UPROPERTY(BlueprintReadOnly) int32 ModelsCurrent = 0;
	UPROPERTY(BlueprintReadOnly) int32 ModelsMax     = 0;
	UPROPERTY(BlueprintReadOnly) int32 TeamNum       = 0;
};

USTRUCT(BlueprintType)
struct FMatchSummary
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 ScoreP1 = 0;
	UPROPERTY(BlueprintReadOnly) int32 ScoreP2 = 0;
	UPROPERTY(BlueprintReadOnly) TArray<FSurvivorEntry> Survivors;
	UPROPERTY(BlueprintReadOnly) int32 RoundsPlayed = 0;
};

enum class EFaction : uint8;
struct FUnitCount;
UENUM(BlueprintType)
enum class EMatchPhase : uint8
{
	Deployment,
	Battle,
	EndGame
};

USTRUCT(BlueprintType)
struct FUnitSummary
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName UnitId = NAME_None;
	UPROPERTY(BlueprintReadOnly) FText DisplayName;
	UPROPERTY(BlueprintReadOnly) int32 ModelsAlive = 0;
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

	UPROPERTY() int8       HitMod   = 0;
	UPROPERTY() int8       SaveMod  = 0;
	UPROPERTY() ECoverType Cover    = ECoverType::None;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeploymentChanged);

UCLASS()
class TABLETOP_API AMatchGameState : public AGameStateBase
{
	GENERATED_BODY()
	
public:

	UPROPERTY(ReplicatedUsing=OnRep_SelectionVis)
	AUnitBase* SelectedUnitGlobal = nullptr;

	UPROPERTY(ReplicatedUsing=OnRep_SelectionVis)
	AUnitBase* TargetUnitGlobal   = nullptr;

	UFUNCTION()
	void OnRep_SelectionVis();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ApplySelectionVis(class AUnitBase* NewSel, class AUnitBase* NewTgt);
	
	UPROPERTY(Transient)
	AUnitBase* LastSelApplied = nullptr;
	UPROPERTY(Transient)
	AUnitBase* LastTgtApplied = nullptr;

	// Server-side setters (call these from GM/PC)
	void SetGlobalSelected(AUnitBase* NewSel);
	void SetGlobalTarget(AUnitBase* NewTarget);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_DrawShotDebug(const FVector& WorldLoc, const FString& Msg,
								 FColor Color = FColor::Yellow, float Duration = 4.f);

	UPROPERTY(ReplicatedUsing=OnRep_FinalSummary, BlueprintReadOnly, Category="Summary")
	FMatchSummary FinalSummary;

	UFUNCTION()
	void OnRep_FinalSummary() { OnDeploymentChanged.Broadcast(); }

	UPROPERTY(ReplicatedUsing=OnRep_Summary)
	bool bShowSummary = false;

	UFUNCTION() void OnRep_Summary() { OnDeploymentChanged.Broadcast(); }
	
	UFUNCTION(BlueprintPure, Category="Summary")
	const FMatchSummary& GetFinalSummary() const { return FinalSummary; }
	
	void SetFinalSummary(const FMatchSummary& In) { FinalSummary = In; OnRep_FinalSummary(); ForceNetUpdate(); }

	UPROPERTY(ReplicatedUsing=OnRep_Match)
	FCombatPreview Preview;
	UFUNCTION() void OnRep_Match(){ OnDeploymentChanged.Broadcast(); }
	
	UFUNCTION(BlueprintPure, Category="Teams")
	ATabletopPlayerState* GetPSForTeam(int32 TeamNum) const;
	
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
	UPROPERTY(ReplicatedUsing=OnRep_Players)
	ATabletopPlayerState* P1 = nullptr;
    UPROPERTY(ReplicatedUsing=OnRep_Players)
	ATabletopPlayerState* P2 = nullptr;

	UPROPERTY(BlueprintAssignable) FOnDeploymentChanged OnDeploymentChanged;

	UFUNCTION() void OnRep_Deployment() { OnDeploymentChanged.Broadcast(); }
	UFUNCTION() void OnRep_Players()    { OnDeploymentChanged.Broadcast(); }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NetDebug")
	bool bEnableNetDebugDraw = true;

	UPROPERTY(EditAnywhere, Category="NetDebug")
	TSubclassOf<ANetDebugTextActor> DebugTextActorClass;

	UPROPERTY(EditAnywhere, Category="NetDebug", meta=(ClampMin="4.0"))
	float DebugTextWorldSize = 28.f;

	// 2D on-screen message (like GEngine->AddOnScreenDebugMessage)
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_ScreenMsg(const FString& Text, FColor Color = FColor::Yellow, float Time = 3.f, int32 Key = -1);

	// World text at a 3D location
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_DrawWorldText(const FVector& WorldLoc, const FString& Text, FColor Color = FColor::Black, float Time = 3.f, float FontScale = 1.f);

	// Lines & spheres (works in packaged builds)
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_DrawLine(const FVector& Start, const FVector& End, FColor Color, float Time = 5.f, float Thickness = 2.f);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_DrawSphere(const FVector& Center, float Radius, int32 Segments, FColor Color, float Time = 5.f, float Thickness = 2.f);

};

UCLASS()
class TABLETOP_API AMatchGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	int32 ApplyFeelNoPain(int32 IncomingDamage, int32 FnpTN);
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
	void Handle_AdvanceUnit(AMatchPlayerController* PC, AUnitBase* Unit);

	UFUNCTION()
	void ApplyDelayedDamageAndReport(AUnitBase* Attacker, AUnitBase* Target, int32 TotalDamage, FVector DebugMid, FString DebugMsg);

	void BuildMatchSummaryAndReveal();
	
	void FinalizePlayerJoin(APlayerController* PC);
	void TallyObjectives_EndOfRound();
	void ResetAPForTurnOwner(UWorld* W, APlayerState* TurnOwner, EActionPoolScope Scope);

	void ResolveMoveToBudget(const AUnitBase* U, const FVector& WantedDest, FVector& OutFinalDest, float& OutSpentTTIn,
	                         bool& bOutClamped) const;
	// Movement
	bool ValidateMove(AUnitBase* Unit, const FVector& Dest, float& OutDistInches) const;
	void Handle_MoveUnit(AMatchPlayerController* PC, AUnitBase* Unit, const FVector& Dest);

	// Targeting & shooting
	bool ValidateShoot(AUnitBase* Attacker, AUnitBase* Target) const;
	void Handle_SelectTarget(AMatchPlayerController* PC, AUnitBase* Attacker, AUnitBase* Target);
	void Handle_ConfirmShoot(AMatchPlayerController* PC, AUnitBase* Attacker, AUnitBase* Target);
	int32 CountVisibleTargetModels(const AUnitBase* Attacker, const AUnitBase* Target) const;
	
	void Handle_ExecuteAction(class AMatchPlayerController* PC, class AUnitBase* Unit, FName ActionId, const FActionRuntimeArgs& Args);

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
	void ResetUnitRoundStateFor(APlayerState* TurnOwner);
	
	AMatchGameState* GS() const { return GetGameState<AMatchGameState>(); }

	APlayerState* OtherPlayer(APlayerState* PS) const;

	UPROPERTY(EditAnywhere, Category="Scale")
	float TabletopToUnrealInchScale = 20.f; // 1 tabletop inch == 20 UE inches (exact). Use 19.685 for 50 cm/in.
	
	static int32 FindIdx(TArray<FUnitCount>& Arr, FName Unit);
	bool CanDeployAt(APlayerController* PC, const FVector& WorldLocation) const;
	void CopyRostersFromPlayerStates();
	bool AnyRemainingFor(APlayerState* PS) const;
	bool DecrementOne(APlayerState* PS, FName UnitId);
	void FinishDeployment();
	UDataTable* UnitsForFaction(EFaction Faction) const;
	TSubclassOf<AActor> UnitClassFor(APlayerState* ForPS, FName UnitId) const;
};
