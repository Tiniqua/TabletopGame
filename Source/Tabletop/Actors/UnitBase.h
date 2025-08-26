#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Tabletop/ArmyData.h"
#include "UnitBase.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UStaticMesh;
class ATabletopPlayerState;
class UNiagaraSystem;

UCLASS()
class TABLETOP_API AUnitBase : public AActor
{
    GENERATED_BODY()
public:
    AUnitBase();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetModelWorldLocations(TArray<FVector>& Out) const;
    
    // ---------- Identity / ownership (replicated) ----------
    UPROPERTY(Replicated) FName UnitId = NAME_None;
    UPROPERTY(Replicated) EFaction Faction = EFaction::None;
    UPROPERTY(Replicated) APlayerState* OwningPS = nullptr;

    // Chosen weapon index within the row (server sets, clients read)
    UPROPERTY(Replicated) int32 WeaponIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Stats|Objective")
    int32 ObjectiveControlPerModel = 1;

    UFUNCTION(BlueprintPure, Category="Objective")
    int32 GetObjectiveControlAt(const class AObjectiveMarker* Marker) const;
    
    // ---------- Runtime state (replicated) ----------
    UPROPERTY(ReplicatedUsing=OnRep_Models) int32 ModelsCurrent = 0;
    UPROPERTY(Replicated)                 int32 ModelsMax     = 0;

    UPROPERTY(ReplicatedUsing=OnRep_Move) float MoveBudgetInches = 0.f;
    UPROPERTY(Replicated)                 float MoveMaxInches    = 0.f;

    UPROPERTY(Replicated) bool bHasShot         = false;

    // ---------- Replicated stat snapshot (so clients don’t need to DT lookup) ----------
    UPROPERTY(Replicated) int32 ToughnessRep = 0;
    UPROPERTY(Replicated) int32 WoundsRep    = 0;
    UPROPERTY(Replicated) int32 SaveRep      = 5;

    UPROPERTY(Replicated) int32 WeaponRangeInchesRep = 0;
    UPROPERTY(Replicated) int32 WeaponAttacksRep     = 0;
    UPROPERTY(Replicated) int32 WeaponDamageRep      = 0;
    UPROPERTY(Replicated) int32 WeaponSkillToHitRep  = 4;
    UPROPERTY(Replicated) int32 WeaponStrengthRep    = 4;
    UPROPERTY(Replicated) int32 WeaponAPRep          = 0;

    UPROPERTY(ReplicatedUsing=OnRep_Health) int32 WoundsPool = 0;

    int32 GetSave() const { return SaveRep; }

    void ApplyDamage_Server(int32 Damage);

    UFUNCTION() void OnRep_Health();
    
    // Init from spawn params (server only)
    void Server_InitFromRow(APlayerState* OwnerPS, const FUnitRow& Row, int32 InWeaponIndex);

    // Hooks for UI / visuals
    UFUNCTION() void OnSelected();
    UFUNCTION() void OnDeselected();
    UFUNCTION() void OnDamaged(int32 ModelsLost, int32 WoundsOverflow = 0);

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

    // --- VFX: simple "all models" volley ---
    UPROPERTY(EditDefaultsOnly, Category="VFX")
    class UNiagaraSystem* FX_Muzzle = nullptr;

    UPROPERTY(EditDefaultsOnly, Category="VFX")
    class UNiagaraSystem* FX_Impact = nullptr;

    // Optional sockets/offsets
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VFX")
    FName MuzzleSocketName = "Muzzle";

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VFX")
    FVector MuzzleOffsetLocal = FVector(30, 0, 60);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VFX")
    FName ImpactSocketName = "Impact";   // if your target mesh has one

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VFX")
    FVector ImpactOffsetLocal = FVector(0, 0, 40);

    // Fire muzzle on every attacker model and impact on every target model.
    // Keep it Unreliable (purely cosmetic).
    UFUNCTION(NetMulticast, Unreliable)
    void Multicast_PlayMuzzleAndImpactFX_AllModels(class AUnitBase* TargetUnit);

    // (helpers)
    UFUNCTION(BlueprintPure, Category="VFX")
    FTransform GetMuzzleTransform(int32 ModelIndex) const;

    UPROPERTY(EditDefaultsOnly, Category="Selection")
    TEnumAsByte<ECollisionChannel> SelectionTraceECC = ECC_GameTraceChannel2;

    // If your mesh assets often lack simple collision, this helps selection “just work”
    UPROPERTY(EditDefaultsOnly, Category="Selection")
    bool bUseComplexForSelection = true;
    
protected:
    virtual void BeginPlay() override;

    UFUNCTION() void OnRep_Models();
    UFUNCTION() void OnRep_Move();

    // Build N child meshes in a grid from ModelsCurrent
    void RebuildFormation();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

    // ---------- Components / visuals ----------
    UPROPERTY(VisibleAnywhere) USphereComponent* SelectCollision = nullptr;

    // Simple list of per-model components (client & server)
    UPROPERTY() TArray<UStaticMeshComponent*> ModelMeshes;

    // Mesh to use per model (assign in BP or defaults)
    UPROPERTY(EditDefaultsOnly, Category="Unit|Visual")
    UStaticMesh* ModelMesh = nullptr;

    // Grid settings
    UPROPERTY(EditDefaultsOnly, Category="Unit|Visual")
    int32 GridColumns = 3;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Formation", meta=(ClampMin="0.0"))
    float ExtraSpacingCmX = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Formation", meta=(ClampMin="0.0"))
    float ExtraSpacingCmY = 10.f;

    // Per-unit BP override
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Formation", meta=(ClampMin="0.05", ClampMax="10.0"))
    float ModelScale = 1.0f;

    UPROPERTY(EditDefaultsOnly, Category="Unit|Visual")
    float ModelSpacingCm = 30.f; // ~1.18 inches; tweak for your base sizes

private:
    void SetHighlighted(bool bOn);
};
