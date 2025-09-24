#pragma once

#include "CoreMinimal.h"
#include "UnitAction.h"
#include "GameFramework/Actor.h"
#include "Tabletop/ArmyData.h"
#include "Tabletop/CombatEffects.h"   // FRollModifiers / ECombatEvent
#include "Components/DecalComponent.h"   // ADD
#include "UnitBase.generated.h"

class UUnitAction;
struct FUnitModifier;                 // from UnitModifiers.h
class USphereComponent;
class UStaticMeshComponent;
class UStaticMesh;
class ATabletopPlayerState;
class UNiagaraSystem;

UENUM(BlueprintType)
enum class ERangeVizMode : uint8 { None, Move, Shoot }; // ADD

UENUM(BlueprintType)
enum class EUnitHighlight : uint8
{
    None      UMETA(DisplayName="None"),
    Friendly  UMETA(DisplayName="Friendly"),
    Enemy     UMETA(DisplayName="Enemy"),
    PotentialEnemy   UMETA(DisplayName="Potential"),
    PotentialAlly UMETA(DisplayName="Potential Ally")
};

USTRUCT()
struct FImpactSite
{
    GENERATED_BODY()
    FVector  Loc = FVector::ZeroVector;
    FRotator Rot = FRotator::ZeroRotator;
};

USTRUCT()
struct FActionUsageEntry
{
    GENERATED_BODY()

    UPROPERTY() FName  ActionId = NAME_None;
    UPROPERTY() int16  PerPhase = 0;
    UPROPERTY() int16  PerTurn  = 0;
    UPROPERTY() int16  PerMatch = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMoveChanged);

UCLASS()
class TABLETOP_API AUnitBase : public AActor
{
    GENERATED_BODY()
public:
    AUnitBase();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetModelWorldLocations(TArray<FVector>& Out) const;
    
    UPROPERTY(VisibleAnywhere, Category="RangeViz")
    UDecalComponent* RangeDecal = nullptr;

    // Optional: keep a sphere for later overlap/prox logic (hidden)
    UPROPERTY(VisibleAnywhere, Category="RangeViz")
    USphereComponent* RangeProbe = nullptr;

    UPROPERTY(EditDefaultsOnly, Category="RangeViz|Material")
    UMaterialInterface* RangeDecalMaterial = nullptr;

    UFUNCTION()
    void OnRep_OwningPS();

    UPROPERTY()
    UMaterialInstanceDynamic* RangeMID = nullptr;

    UPROPERTY(EditDefaultsOnly, Category="RangeViz|Material", meta=(ClampMin="1.0"))
    float RangeDecalThickness = 120.f; // Decal Z extent (height of the frustum)

    UPROPERTY(EditDefaultsOnly, Category="RangeViz|Material", meta=(ClampMin="0.0"))
    float RingSoftness = 30.f; // If your material supports it

    UPROPERTY(EditDefaultsOnly, Category="RangeViz|Material")
    FLinearColor FriendlyColor = FLinearColor(0.10f, 0.65f, 1.0f, 0.65f);

    UPROPERTY(EditDefaultsOnly, Category="RangeViz|Material")
    FLinearColor EnemyColor = FLinearColor(1.0f, 0.20f, 0.20f, 0.65f);

    // Show/hide/update
    UFUNCTION(BlueprintCallable, Category="RangeViz")
    void UpdateRangePreview(bool bAsTargetContext);

    UFUNCTION(BlueprintCallable, Category="RangeViz")
    void HideRangePreview();

    // ----- Overwatch viz -----
    UPROPERTY(VisibleAnywhere, Category="OverwatchViz")
    UDecalComponent* OverwatchDecal = nullptr;

    UPROPERTY(EditDefaultsOnly, Category="OverwatchViz|Material")
    UMaterialInterface* OverwatchDecalMaterial = nullptr;

    UPROPERTY()
    UMaterialInstanceDynamic* OverwatchMID = nullptr;

    // Slightly different z-thickness/height to avoid z-fight with RangeDecal
    UPROPERTY(EditDefaultsOnly, Category="OverwatchViz|Material", meta=(ClampMin="1.0"))
    float OverwatchDecalThickness = 120.f;

    UPROPERTY(EditDefaultsOnly, Category="OverwatchViz|Material")
    FLinearColor OverwatchColor = FLinearColor(1.0f, 0.65f, 0.10f, 0.55f); // amber-ish

    // 0 = use current weapon range; otherwise override in inches
    UPROPERTY(EditDefaultsOnly, Category="OverwatchViz")
    int32 OverwatchRangeOverrideInches = 0;

    // Should enemies see the ring? Usually no—owner/team only.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_OWVisToEnemies, Category="Overwatch")
    bool bOverwatchVisibleToEnemies = true;

    // Make the armed flag drive the ring via OnRep
    UPROPERTY(ReplicatedUsing=OnRep_OverwatchArmed, BlueprintReadOnly)
    bool bOverwatchArmed = false;

    UFUNCTION()
    void OnRep_OverwatchArmed();

    UFUNCTION()
    void OnRep_OWVisToEnemies();

    // Helpers
    void EnsureOverwatchDecal();
    void UpdateOverwatchIndicatorLocal(bool bForceHide = false);
    float GetOverwatchRangeCm() const;

    // Server helper to toggle state
    UFUNCTION(BlueprintCallable, Category="Overwatch")
    void SetOverwatchArmed(bool bArmed);

    // ---------- Abilities ----------

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Action")
    class UUnitActionResourceComponent* ActionPoints = nullptr;
    
    UPROPERTY(Instanced, VisibleAnywhere, BlueprintReadOnly, Category="Action")
    TArray<UUnitAction*> RuntimeActions;

    UPROPERTY(BlueprintAssignable, Category="Events")
    FOnMoveChanged OnMoveChanged;

    UFUNCTION(BlueprintCallable, Category="Movement")
    void NotifyMoveChanged();
    
    UPROPERTY(Instanced, VisibleAnywhere, BlueprintReadOnly, Category="Ability")
    TArray< UUnitAbility*> RuntimeAbilities;

    UPROPERTY(ReplicatedUsing=OnRep_AbilityClasses)
    TArray<TSubclassOf<UUnitAbility>> AbilityClassesRep;

    UPROPERTY(ReplicatedUsing=OnRep_ActionUsage)
    TArray<FActionUsageEntry> ActionUsageRep;

    UPROPERTY()
    TMap<FName, FActionUsageEntry> ActionUsageRuntime;

    UFUNCTION()
    void OnRep_ActionUsage();
    bool CanUseActionNow(const FActionDescriptor& D) const;

    UPROPERTY(Replicated)
    int32 NextPhaseAPDebt = 0;

    UFUNCTION()
    void OnRep_AbilityClasses();

    FActionUsageEntry* FindOrAddUsage(AUnitBase* U, FName Id);
    void ResetUsageForPhase();
    void ResetUsageForTurn();
    void BumpUsage(const FActionDescriptor& D);
    // Helper you can call from both server + clients
    void EnsureRuntimeBuilt();

    // helpers
    UFUNCTION(BlueprintCallable, Category="Action")
    const TArray<UUnitAction*>& GetActions();
    UFUNCTION(BlueprintCallable, Category="Ability")
    const TArray<UUnitAbility*>& GetAbilities();

    // ---------- Identity / ownership (replicated) ----------
    UPROPERTY(Replicated) FName UnitId = NAME_None;
    UPROPERTY(Replicated) EFaction Faction = EFaction::None;
    UPROPERTY(ReplicatedUsing=OnRep_OwningPS)
    APlayerState* OwningPS = nullptr;
    
    // Chosen weapon index within the row (server sets, clients read)
    UPROPERTY(Replicated) int32 WeaponIndex = 0;

    // Active combat mods (REPLICATED so clients can preview/visualize)
    UPROPERTY(Replicated, BlueprintReadOnly)
    TArray<FUnitModifier> ActiveCombatMods;

    // Track move/advance this turn (REPLICATED for preview & keyword logic)
    UPROPERTY(Replicated, BlueprintReadOnly) bool bMovedThisTurn = false;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bAdvancedThisTurn = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Stats|Objective")
    int32 ObjectiveControlPerModel = 1;

    UFUNCTION(BlueprintPure, Category="Objective")
    int32 GetObjectiveControlAt(const class AObjectiveMarker* Marker) const;
    
    // ---------- Runtime state (replicated) ----------
    UPROPERTY(ReplicatedUsing=OnRep_Models) int32 ModelsCurrent = 0;
    UPROPERTY(Replicated)                 int32 ModelsMax     = 0;
    UPROPERTY(Replicated)                 FText UnitName;
    
    UPROPERTY(ReplicatedUsing=OnRep_CurrentWeapon)
    FWeaponProfile CurrentWeapon;

    // Accessor used by gameplay (server authoritative)
    FORCEINLINE const FWeaponProfile& GetActiveWeaponProfile() const { return CurrentWeapon; }

    
    UPROPERTY(ReplicatedUsing=OnRep_Move) float MoveBudgetInches = 0.f;
    UPROPERTY(ReplicatedUsing=OnRep_Move) float MoveMaxInches    = 0.f;

    UPROPERTY(Replicated) bool bHasShot         = false;
    UPROPERTY(Replicated) AActor* CurrentTarget = nullptr;

    // ---------- Replicated stat snapshot ----------
    UPROPERTY(Replicated) int32 ToughnessRep = 0;
    UPROPERTY(Replicated) int32 WoundsRep    = 0;
    UPROPERTY(Replicated) int32 SaveRep      = 5;

    UPROPERTY(Replicated) int32 WeaponRangeInchesRep = 0;
    UPROPERTY(Replicated) int32 WeaponAttacksRep     = 0;
    UPROPERTY(Replicated) int32 WeaponDamageRep      = 0;
    UPROPERTY(Replicated) int32 WeaponSkillToHitRep  = 4;
    UPROPERTY(Replicated) int32 WeaponStrengthRep    = 4;
    UPROPERTY(Replicated) int32 WeaponAPRep          = 0;
    
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Stats|Defense")
    int32 InvulnerableSaveRep = 7;

    UPROPERTY(Replicated, BlueprintReadOnly, Category="Stats|Defense")
    int32 FeelNoPainRep = 7;
    
    UFUNCTION(BlueprintPure) int32 GetInvuln() const { return InvulnerableSaveRep; }
    UFUNCTION(BlueprintPure) int32 GetFeelNoPain() const { return FeelNoPainRep; }

    UPROPERTY(ReplicatedUsing=OnRep_Health) int32 WoundsPool = 0;

    int32 GetSave() const { return SaveRep; }

    void ApplyDamage_Server(int32 Damage);
    void ApplyMortalDamage_Server(int32 Damage);

    UFUNCTION()
    void OnRep_CurrentWeapon();

    // Small helper so both Server_InitFromRow and OnRep_CurrentWeapon can call it
    void SyncWeaponSnapshotsFromCurrent();

    // Mods API (no bespoke debuff funcs)
    void AddUnitModifier(const FUnitModifier& Mod);
    FRollModifiers CollectStageMods(ECombatEvent Stage, bool bAsAttacker, const class AUnitBase* Opponent) const;
    void ConsumeForStage(ECombatEvent Stage, bool bAsAttacker);
    void OnTurnAdvanced();
    void OnRoundAdvanced();

    UFUNCTION() void OnRep_Health();
    
    // Init from spawn params (server only)
    void Server_InitFromRow(APlayerState* OwnerPS, const FUnitRow& Row, int32 InWeaponIndex);

    // Hooks for UI / visuals
    UFUNCTION() void OnSelected();
    UFUNCTION() void OnDeselected();

    UFUNCTION(BlueprintCallable, Server, Reliable)
    void ApplyHealing_Server(int32 Wounds);

    UFUNCTION(NetMulticast, Reliable)
    void Multicast_OnDamaged(int32 ModelsLost, int32 WoundsOverflow = 0);

    // Utilities
    float GetWeaponRange()  const { return (float)WeaponRangeInchesRep; }
    int32 GetAttacks()      const { return WeaponAttacksRep; }
    int32 GetDamage()       const { return WeaponDamageRep; }
    int32 GetToughness()    const { return ToughnessRep; }
    int32 GetWounds()       const { return WoundsRep; }

    UFUNCTION(BlueprintCallable, Category="Facing")
    void FaceNearestEnemyInstant();

    UFUNCTION(BlueprintPure, Category="Teams")
    bool IsEnemy(const AUnitBase* Other) const;
    
    UFUNCTION(BlueprintPure, Category="Facing")
    AActor* FindNearestEnemyUnit(float MaxSearchDistCm = 100000.f) const;
    bool FaceActorInstant(AActor* Target, float YawSnapDeg = 1.0f);

    UPROPERTY(EditAnywhere, Category="Facing", meta=(ClampMin="-180.0", ClampMax="180.0"))
    float FacingYawOffsetDeg = -90.f;

    UFUNCTION(BlueprintPure)
    int32 GetWoundsPerModel() const { return FMath::Max(1, WoundsRep); }
    
    UPROPERTY(EditAnywhere, Category="Formation", meta=(ClampMin="0.0"))
    float ModelPaddingCm = 2.0f;

    UPROPERTY(EditAnywhere, Category="Formation", meta=(ClampMin="-180.0", ClampMax="180.0"))
    float ModelYawVisualOffsetDeg = 0.0f;

    UPROPERTY(EditDefaultsOnly, Category="Unit|Visual")
    float ModelSpacingApartCm = 80.f; // ~1.18 inches; tweak for your base sizes

    // ===== Visual-only facing (no actor rotation) =====

    UFUNCTION(BlueprintCallable, Category="Facing")
    void VisualFaceYaw(float WorldYaw);

    UPROPERTY(ReplicatedUsing=OnRep_VFXAudio) UNiagaraSystem* FX_Muzzle = nullptr;
    UPROPERTY(ReplicatedUsing=OnRep_VFXAudio) UNiagaraSystem* FX_Impact = nullptr;

    UPROPERTY(ReplicatedUsing=OnRep_VFXAudio) USoundBase*    Snd_Muzzle = nullptr;
    UPROPERTY(ReplicatedUsing=OnRep_VFXAudio) USoundBase*    Snd_Impact = nullptr;
    UPROPERTY(ReplicatedUsing=OnRep_VFXAudio) USoundBase*    Snd_Selected = nullptr;
    UPROPERTY(ReplicatedUsing=OnRep_VFXAudio) USoundBase*    Snd_UnderFire = nullptr;

    UPROPERTY(ReplicatedUsing=OnRep_VFXAudio) USoundAttenuation* SndAttenuation = nullptr;
    UPROPERTY(ReplicatedUsing=OnRep_VFXAudio) USoundConcurrency* SndConcurrency  = nullptr;

    UPROPERTY(ReplicatedUsing=OnRep_VFXAudio)
    float ImpactDelaySeconds = 1.0f;

    UFUNCTION()
    void OnRep_VFXAudio();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VFX")
    FName MuzzleSocketName = "Muzzle";

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VFX")
    FVector MuzzleOffsetLocal = FVector(30,0,60);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VFX")
    FName ImpactSocketName = "Impact";

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VFX")
    FVector ImpactOffsetLocal = FVector(0,0,40);

    FTimerHandle ImpactFXTimerHandle;

    UFUNCTION(NetMulticast, Unreliable)
    void Multicast_PlayMuzzleAndImpactFX_AllModels_WithSites(const FVector& TargetCenter, const TArray<FImpactSite>& Sites, float DelaySeconds);
    
    UFUNCTION()
    void PlayImpactFXAndSounds_Delayed(AUnitBase* TargetUnit);

    UFUNCTION(BlueprintPure, Category="VFX")
    FTransform GetMuzzleTransform(int32 ModelIndex) const;

    UFUNCTION()
    void ApplyAPPhaseStart(ETurnPhase Phase);

    UPROPERTY(EditDefaultsOnly, Category="Selection")
    TEnumAsByte<ECollisionChannel> SelectionTraceECC = ECC_GameTraceChannel2;

    UPROPERTY(EditDefaultsOnly, Category="Selection")
    bool bUseComplexForSelection = true;

    UPROPERTY() TArray<UStaticMeshComponent*> ModelMeshes;

    UFUNCTION(BlueprintPure, Category="VFX")
    int32 FindBestShooterModelIndex(const FVector& TargetWorld) const;
    
    // Local-only visual toggle (safe on clients and listen server)
    UFUNCTION(BlueprintCallable, Category="Selection|Outline")
    void SetHighlightLocal(EUnitHighlight Mode);


    UFUNCTION()
    void ApplyFormationOffsetsLocal(const TArray<FVector>& OffsetsLocal);
    
    void RebuildFormation();
    UFUNCTION()
    void OnRep_Move();

    void RefreshRangeIfActive();
    
protected:
    virtual void BeginPlay() override;

    UFUNCTION()
    void OnRep_Models();
    
    void EnsureRangeDecal();
    void SetRangeVisible(float RadiusCm, const FLinearColor& Color, ERangeVizMode Mode);
    
    float GetCmPerTTInch_Safe() const;

    ERangeVizMode CurrentRangeMode = ERangeVizMode::None;

    
    void RebuildRuntimeActions();
    void RebuildRuntimeAbilitiesFromSources();

    

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

    UPROPERTY(VisibleAnywhere) USphereComponent* SelectCollision = nullptr;

    UPROPERTY(ReplicatedUsing=OnRep_ModelVisual, EditDefaultsOnly, Category="Unit|Visual")
    UStaticMesh* ModelMesh = nullptr;
    
    UFUNCTION()
    void OnRep_ModelVisual();

    UPROPERTY(EditDefaultsOnly, Category="Unit|Visual")
    int32 GridColumns = 3;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Formation", meta=(ClampMin="0.0"))
    float ExtraSpacingCmX = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Formation", meta=(ClampMin="0.0"))
    float ExtraSpacingCmY = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Formation", meta=(ClampMin="0.05", ClampMax="10.0"))
    float ModelScale = 1.0f;
    
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Selection|Outline")
    UMaterialInterface* OutlineFriendlyMaterial = nullptr;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Selection|Outline")
    UMaterialInterface* OutlineEnemyMaterial = nullptr;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Selection|Outline")
    UMaterialInterface* OutlinePotentialEnemyMaterial = nullptr;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Selection|Outline")
    UMaterialInterface* OutlinePotentialAllyMaterial = nullptr;
    
    

private:
    UPROPERTY()
    TArray<UMaterialInstanceDynamic*> HighlightMIDs;
    
    void ApplyOutlineToAllModels(UMaterialInterface* Mat);

    EUnitHighlight CurrentHighlight = EUnitHighlight::None;
};
