
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Tabletop/AbiltyEventSubsystem.h"
#include "Tabletop/ArmyData.h"
#include "Tabletop/Actors/CoverVolume.h"
#include "Tabletop/Actors/UnitAction.h"

#include "MatchGameMode.generated.h"

struct FRosterEntry;
class ATabletopPlayerState;
class ANetDebugTextActor;

class AMatchPlayerController;
class AUnitBase;

USTRUCT()
struct FCoverPairKey
{
	GENERATED_BODY()
	UPROPERTY() TWeakObjectPtr<const AUnitBase> A;
	UPROPERTY() TWeakObjectPtr<const AUnitBase> T;
	bool operator==(const FCoverPairKey& O) const { return A==O.A && T==O.T; }
};


USTRUCT()
struct FCoverRowAssignment
{
	GENERATED_BODY()

	UPROPERTY() TWeakObjectPtr<ACoverVolume> Volume;

	// Keep RowName if useful for debug, but we won't *need* it on clients anymore
	UPROPERTY() FName RowName;

	UPROPERTY() uint8 bPreferLow : 1;

	// Fully resolved on the server, replicated to clients:
	UPROPERTY() UStaticMesh* HighMesh = nullptr;
	UPROPERTY() UStaticMesh* LowMesh  = nullptr;
	UPROPERTY() UStaticMesh* NoneMesh = nullptr;

	UPROPERTY() float StartPct     = 1.f;
	UPROPERTY() float ThresholdPct = 0.5f;
};

USTRUCT(BlueprintType)
struct FCoverPresetRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Which faction this preset is for */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EFaction Faction = EFaction::None;

	/** High / Low / None meshes used by ACoverVolume */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	UStaticMesh* HighCoverMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	UStaticMesh* LowCoverMesh  = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	UStaticMesh* NoCoverMesh   = nullptr;

	/**
	 * Health fraction threshold where High → Low (0..1).
	 * Example: 0.65 means at 65% health and above it's High, below it's Low.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.0", ClampMax="1.0"))
	float HighToLowPct = 0.65f;

	/**
	 * Starting health fraction (0..1). Usually 1.0, but can theme maps to start damaged.
	 * NOTE: If a volume has bPreferLowCover=true, the code will override this to just
	 * below HighToLowPct so it starts in Low without removing the High mesh.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.0", ClampMax="1.0"))
	float StartHealthPct = 1.0f;
};

FORCEINLINE uint32 GetTypeHash(const FCoverPairKey& K)
{
	return ::PointerHash(K.A.Get()) ^ (3u * ::PointerHash(K.T.Get()));
}

USTRUCT()
struct FCoverPairCache
{
	GENERATED_BODY()
	UPROPERTY() float LastFraction = 0.f;
	UPROPERTY() ECoverType LastType = ECoverType::None;
};

struct FShotResolveResult
{
	int32 FinalDamage = 0;
	int32 Hits = 0;
	int32 Attacks = 0;
	int32 Wounds = 0;
	int32 SavesMade = 0;
	bool  bIgnoredCover = false;
	ECoverType Cover = ECoverType::None;
};

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

	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category="Cover")
	UDataTable* CoverPresetsTable = nullptr;

	UPROPERTY(ReplicatedUsing=OnRep_CoverAssignments)
	TArray<FCoverRowAssignment> CoverAssignments;

	UFUNCTION()
	void OnRep_CoverAssignments();

	void ApplyCoverAssignment(const FCoverRowAssignment& A);

	UFUNCTION()
	void OnRep_SelectionVis();

	UFUNCTION()
	void OnLevelAdded(ULevel* Level, UWorld* World);

	void ReapplyAllCoverAssignments();
	
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Scale")
	float CmPerTTInchRep = 50.8f; // uses 20*2.54 from -- float CmPerTabletopInch() const { return 2.54f * TabletopToUnrealInchScale; }

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
	
	UFUNCTION() void OnRep_Match();
	
	UFUNCTION(BlueprintPure, Category="Teams")
	ATabletopPlayerState* GetPSForTeam(int32 TeamNum) const;
	
	UPROPERTY(Replicated)
	bool bTeamsAndTurnsInitialized = false;

	// Replace your existing UPROPERTY lines for these two with:
	UPROPERTY(ReplicatedUsing=OnRep_Preview)
	FCombatPreview Preview;

	UPROPERTY(ReplicatedUsing=OnRep_ActionPreview)
	FActionPreview ActionPreview;

	// Add these declarations somewhere in the public/protected section:
	UFUNCTION() void OnRep_Preview();
	UFUNCTION() void OnRep_ActionPreview();

	UPROPERTY(Transient, BlueprintReadOnly)
	TArray<class AObjectiveMarker*> Objectives;

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
	UPROPERTY(ReplicatedUsing=OnRep_Deployment) TArray<FRosterEntry> P1Remaining;
	UPROPERTY(ReplicatedUsing=OnRep_Deployment) TArray<FRosterEntry> P2Remaining;

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

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_SetPotentialTargets(const TArray<AUnitBase*>& NewPotentials);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_SetPotentialAllies(const TArray<AUnitBase*>& NewPotentials);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_ClearPotentialTargets();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ApplyCoverPreset(ACoverVolume* Volume,
								UStaticMesh* HighMesh,
								UStaticMesh* LowMesh,
								UStaticMesh* NoneMesh,
								float StartHealthPct,
								float ThresholdPct);

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AUnitBase>> LastPotentialApplied;
	
	const TArray<TWeakObjectPtr<AUnitBase>>& GetLastPotentialTargets() const { return LastPotentialApplied; }

};

UCLASS()
class TABLETOP_API AMatchGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	UAbilityEventSubsystem* AbilityBus(UWorld* W);
	int32 ApplyFeelNoPain(int32 IncomingDamage, int32 FnpTN);
	AMatchGameMode();
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover")
	UDataTable* CoverPresetsTable = nullptr;

	UFUNCTION() void RebroadcastCoverPresetsReliable();
	UFUNCTION() void OnClientReportedLoaded(class APlayerController* PC);
	virtual void PostSeamlessTravel() override;

	void Emit(ECombatEvent E, AUnitBase* Src=nullptr, AUnitBase* Tgt=nullptr,
		  const FVector& Pos=FVector::ZeroVector, float Radius=0.f,
		  const FGameplayTagContainer* Tags=nullptr);

	UPROPERTY() TSet<TWeakObjectPtr<APlayerController>> ClientsLoaded;

	UPROPERTY(EditDefaultsOnly, Category="Cover|Damage")
	float FriendlyCoverIgnoreProximityInches = 6.f;

	UPROPERTY(EditAnywhere, Category="Cover")
	float TargetCoverUseProximityInches = 6.0f;

	UFUNCTION(exec)
	void CoverPreset_Dump();
	
	UPROPERTY(Transient)
	TMap<FCoverPairKey, FCoverPairCache> CoverMemory;

	UPROPERTY(EditDefaultsOnly, Category="Cover|Query")
	bool bExhaustiveCoverCross = true;  // false = nearest-N (fast), true = all attacker×target pairs

	UPROPERTY(EditDefaultsOnly, Category="Cover|Damage")
	bool bDistributeCoverDamageAcrossHits = false;

	UFUNCTION(BlueprintCallable, Category="Formation")
	TArray<FVector> ComputeCohesiveCoveredFormation(
		const FVector& UnitCenter, const FVector& ThreatDir,
		int32 ModelCount, float BaseRadiusCm) const;
	

	bool QueryCoverWithActor(AUnitBase* Attacker, AUnitBase* Target,
						 int32& OutHitMod, int32& OutSaveMod,
						 ECoverType& OutType, ACoverVolume*& OutPrimaryCover,
						 TMap<ACoverVolume*, int32>* OutCoverHits /*=nullptr*/) const;

	// keep the old signature for existing callers
	bool QueryCover(class AUnitBase* Attacker, class AUnitBase* Target,
					int32& OutHitMod, int32& OutSaveMod, ECoverType& OutType) const;

	UFUNCTION()
	void ApplyDelayedCoverDamage(ACoverVolume* Cover, float Damage, FVector DebugLoc, FString DebugMsg);

	// Needed to look up unit display/icon (optional for spawning)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	UDataTable* FactionsTable = nullptr;

	FText BuildRosterDisplayLabel(class APlayerState* ForPS, const FRosterEntry& E) const;

	// Fill ServerDisplayLabel for every row in an array
	void  FillServerLabelsFor(class APlayerState* ForPS, TArray<FRosterEntry>& Arr) const;

	FShotResolveResult ResolveRangedAttack_Internal(AUnitBase* Attacker, AUnitBase* Target, const TCHAR* DebugPrefix);
	
	// Server RPC endpoints (called by PC server functions)
	void HandleRequestDeploy(APlayerController* PC, FName UnitId, const FTransform& Where, int32 WeaponIndex);
	void HandleStartBattle(class APlayerController* PC);
	void HandleEndPhase(class APlayerController* PC);
	void ScoreObjectivesForRound();
	void NotifyUnitTransformChanged(AUnitBase* Changed);
	void Handle_AdvanceUnit(AMatchPlayerController* PC, AUnitBase* Unit);
	void Handle_OverwatchShot(AUnitBase* Attacker, AUnitBase* Target);
	
	UFUNCTION()
	void ApplyDelayedDamageAndReport(AUnitBase* Attacker, AUnitBase* Target, int32 TotalDamage, FVector DebugMid, FString DebugMsg);

	void BuildMatchSummaryAndReveal();
	
	void FinalizePlayerJoin(APlayerController* PC);
	void TallyObjectives_EndOfRound();

	void BroadcastPotentialTargets(AUnitBase* Attacker);

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

	UPROPERTY(EditDefaultsOnly, Category="Cover|Query")
	float CoverModelCoverageThreshold = 0.5f; // >=50% models need cover to claim cover

	UPROPERTY(EditDefaultsOnly, Category="Cover|Query")
	float CoverHysteresis = 0.15f; // need to drop below (Threshold - Hysteresis) to lose cover

	UPROPERTY(EditDefaultsOnly, Category="Cover|Query")
	int32 RaysPerTargetModel = 2;   // how many attacker points to sample per target model (nearest muzzles)
	
	UPROPERTY(EditDefaultsOnly, Category="Cover")
	TEnumAsByte<ECollisionChannel> CoverTraceChannel = ECC_GameTraceChannel4;

	// hard cap to keep perf predictable when sampling per-model
	UPROPERTY(EditDefaultsOnly, Category="Cover")
	int32 MaxCoverSamplesPerUnit = 10;

	// NEW: proximity window for cover validity (tabletop inches)
	UPROPERTY(EditDefaultsOnly, Category="Cover")
	float CoverProximityInches = 8.f;

	// NEW: toggle draw-debug lines/text for cover traces
	UPROPERTY(EditDefaultsOnly, Category="Cover|Debug")
	bool bDebugCoverTraces = true;

	UDataTable* UnitsForFaction(EFaction Faction) const;

	UFUNCTION(BlueprintCallable, Category="Cover")
	void ApplyCoverPresetsFromTableOnce();

	bool bCoverPresetsApplied = false;
	
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
	
	bool CanDeployAt(APlayerController* PC, const FVector& WorldLocation) const;
	void CopyRostersFromPlayerStates();
	bool AnyRemainingFor(APlayerState* PS) const;
	bool DecrementOne(APlayerState* PS, FName UnitId, int32 WeaponIndex);
	void FinishDeployment();
	
	TSubclassOf<AActor> UnitClassFor(APlayerState* ForPS, FName UnitId) const;
};
