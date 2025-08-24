#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Tabletop/ArmyData.h"
#include "UnitBase.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UStaticMesh;
class ATabletopPlayerState;

UCLASS()
class TABLETOP_API AUnitBase : public AActor
{
    GENERATED_BODY()
public:
    AUnitBase();

    // ---------- Identity / ownership (replicated) ----------
    UPROPERTY(Replicated) FName UnitId = NAME_None;
    UPROPERTY(Replicated) EFaction Faction = EFaction::None;
    UPROPERTY(Replicated) APlayerState* OwningPS = nullptr;

    // Chosen weapon index within the row (server sets, clients read)
    UPROPERTY(Replicated) int32 WeaponIndex = 0;

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
    int32 GridColumns = 5;

    UPROPERTY(EditDefaultsOnly, Category="Unit|Visual")
    float ModelSpacingCm = 30.f; // ~1.18 inches; tweak for your base sizes

private:
    void SetHighlighted(bool bOn);
};
