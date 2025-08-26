#include "UnitBase.h"

#include "EngineUtils.h"
#include "ObjectiveMarker.h"
#include "Net/UnrealNetwork.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Tabletop/PlayerStates/TabletopPlayerState.h"

AUnitBase::AUnitBase()
{
    bReplicates = true;
    SetReplicateMovement(true);

    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    SelectCollision = CreateDefaultSubobject<USphereComponent>(TEXT("SelectCollision"));
    SelectCollision->SetupAttachment(RootComponent);
    SelectCollision->InitSphereRadius(60.f);
    SelectCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    SelectCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
    // Match your PC trace channel choice (see controller code)
    SelectCollision->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
    // Optional: also block visibility if you select on visibility
    // SelectCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void AUnitBase::BeginPlay()
{
    Super::BeginPlay();

    // On clients, replicated ModelsCurrent may arrive later; OnRep_Models will build the formation.
    if (HasAuthority())
    {
        // Ensure a valid formation on server from the start (useful on listen host)
        RebuildFormation();
    }
}

/** Server-only init from DataTable row */
void AUnitBase::Server_InitFromRow(APlayerState* OwnerPS, const FUnitRow& Row, int32 InWeaponIndex)
{
    check(HasAuthority());

    OwningPS = OwnerPS;
    UnitId   = Row.UnitId;
    ObjectiveControlPerModel = Row.ObjectiveControlPerModel;


    if (const ATabletopPlayerState* TPS = Cast<ATabletopPlayerState>(OwnerPS))
    {
        Faction = TPS->SelectedFaction;
    }

    ModelsMax     = Row.Models;
    ModelsCurrent = ModelsMax;

    // Core stats snapshot
    ToughnessRep = Row.Toughness;
    WoundsRep    = Row.Wounds;
    SaveRep      = Row.Save;        // NEW

    // Wounds pool (models * wounds each)
    WoundsPool = ModelsMax * FMath::Max(1, WoundsRep); // NEW

    // Movement
    MoveMaxInches    = (float)Row.MoveInches;
    MoveBudgetInches = 0.f;

    // Weapon snapshot
    if (Row.Weapons.Num() > 0)
    {
        WeaponIndex = FMath::Clamp(InWeaponIndex, 0, Row.Weapons.Num() - 1);
        const FWeaponProfile& W = Row.Weapons[WeaponIndex];

        WeaponRangeInchesRep = W.RangeInches;
        WeaponAttacksRep     = W.Attacks;
        WeaponDamageRep      = W.Damage;

        WeaponSkillToHitRep  = W.SkillToHit;   // NEW
        WeaponStrengthRep    = W.Strength;     // NEW
        WeaponAPRep          = W.AP;           // NEW
    }
    else
    {
        WeaponIndex = 0;
        WeaponRangeInchesRep = 0;
        WeaponAttacksRep     = 0;
        WeaponDamageRep      = 0;

        WeaponSkillToHitRep  = 6;
        WeaponStrengthRep    = 3;
        WeaponAPRep          = 0;
    }

    RebuildFormation();
    ForceNetUpdate();
}

FTransform AUnitBase::GetMuzzleTransform(int32 ModelIndex) const
{
    if (!ModelMeshes.IsValidIndex(ModelIndex) || !ModelMeshes[ModelIndex])
        return GetActorTransform();

    UStaticMeshComponent* C = ModelMeshes[ModelIndex];

    if (C->DoesSocketExist(MuzzleSocketName))
    {
        return C->GetSocketTransform(MuzzleSocketName, ERelativeTransformSpace::RTS_World);
    }

    const FTransform WT = C->GetComponentTransform();
    const FVector   WLoc = WT.TransformPosition(MuzzleOffsetLocal);
    return FTransform((WT.GetRotation()).Rotator(), WLoc, WT.GetScale3D());
}

void AUnitBase::Multicast_PlayMuzzleAndImpactFX_AllModels_Implementation(AUnitBase* TargetUnit)
{
    if (!FX_Muzzle && !FX_Impact) return;         // nothing to do
    if (!IsValid(TargetUnit)) return;

    // ---------- MUZZLES (attacker side) ----------
    const FVector TargetCenter = TargetUnit->GetActorLocation();

    for (int32 i = 0; i < ModelMeshes.Num(); ++i)
    {
        UStaticMeshComponent* C = ModelMeshes[i];
        if (!IsValid(C)) continue;

        const FTransform MuzzXform = GetMuzzleTransform(i);
        const FVector MuzzLoc = MuzzXform.GetLocation();
        const FRotator AimRot = (TargetCenter - MuzzLoc).Rotation();

        if (FX_Muzzle)
        {
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                GetWorld(), FX_Muzzle, MuzzLoc, AimRot);
        }
    }

    // ---------- IMPACTS (target side) ----------
    // For each target model: pick an impact point (socket if present, else local offset),
    // and orient along the incoming direction from the attacker.
    const FVector AttackerCenter = GetActorLocation();

    for (int32 j = 0; j < TargetUnit->ModelMeshes.Num(); ++j)
    {
        UStaticMeshComponent* TC = TargetUnit->ModelMeshes[j];
        if (!IsValid(TC)) continue;

        FVector ImpactLoc;
        FRotator ImpactRot;

        if (TC->DoesSocketExist(TargetUnit->ImpactSocketName))
        {
            const FTransform Sock = TC->GetSocketTransform(TargetUnit->ImpactSocketName, ERelativeTransformSpace::RTS_World);
            ImpactLoc = Sock.GetLocation();
        }
        else
        {
            const FTransform WT = TC->GetComponentTransform();
            ImpactLoc = WT.TransformPosition(TargetUnit->ImpactOffsetLocal);
        }

        // Face roughly against the incoming shot direction
        const FVector InDir = (ImpactLoc - AttackerCenter).GetSafeNormal();
        ImpactRot = InDir.Rotation();

        if (FX_Impact)
        {
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                GetWorld(), FX_Impact, ImpactLoc, ImpactRot);
        }
    }
}


void AUnitBase::GetModelWorldLocations(TArray<FVector>& Out) const
{
    Out.Reset();
    for (UStaticMeshComponent* C : ModelMeshes)
        if (IsValid(C)) Out.Add(C->GetComponentLocation());

    if (Out.Num() == 0) Out.Add(GetActorLocation());
}

int32 AUnitBase::GetObjectiveControlAt(const AObjectiveMarker* Marker) const
{
    if (!Marker || ObjectiveControlPerModel <= 0) return 0;

    // You already have something like this; use your version.
    TArray<FVector> ModelLocs;
    GetModelWorldLocations(ModelLocs);              // <- your existing helper
    if (ModelLocs.Num() == 0) return 0;

    int32 ModelsIn = 0;
    for (const FVector& L : ModelLocs)
    {
        if (Marker->IsInside(L)) ++ModelsIn;
    }
    return ModelsIn * ObjectiveControlPerModel;
}


void AUnitBase::ApplyDamage_Server(int32 Damage)
{
    if (!HasAuthority() || Damage <= 0) return;

    WoundsPool = FMath::Max(0, WoundsPool - Damage);

    // Ceil division to compute how many models still stand
    const int32 PerModel = FMath::Max(1, WoundsRep);
    int32 NewModels = (WoundsPool + PerModel - 1) / PerModel;
    NewModels = FMath::Clamp(NewModels, 0, ModelsMax);

    if (NewModels != ModelsCurrent)
    {
        ModelsCurrent = NewModels;
        RebuildFormation();
    }

    if (WoundsPool <= 0)
    {
        Destroy(); // replicated destroy
        return;
    }

    ForceNetUpdate();
}

void AUnitBase::OnDamaged(int32 ModelsLost, int32 /*WoundsOverflow*/)
{
    if (!HasAuthority()) return;

    ModelsCurrent = FMath::Clamp(ModelsCurrent - FMath::Max(0, ModelsLost), 0, ModelsMax);
    
    RebuildFormation();
    ForceNetUpdate();
}

void AUnitBase::OnRep_Health()
{
    // Recompute ModelsCurrent from WoundsPool on clients and rebuild
    const int32 PerModel = FMath::Max(1, WoundsRep);
    int32 NewModels = (WoundsPool + PerModel - 1) / PerModel;
    NewModels = FMath::Clamp(NewModels, 0, ModelsMax);

    if (NewModels != ModelsCurrent)
    {
        ModelsCurrent = NewModels;
    }
    RebuildFormation();
}

/** Simple highlight toggle (uses CustomDepth on all model meshes) */
void AUnitBase::SetHighlighted(bool bOn)
{
    for (UStaticMeshComponent* C : ModelMeshes)
    {
        if (C)
        {
            C->SetRenderCustomDepth(bOn);
        }
    }
    if (SelectCollision)
    {
        SelectCollision->SetRenderCustomDepth(bOn);
    }
}

void AUnitBase::OnSelected()
{
    SetHighlighted(true);
}

void AUnitBase::OnDeselected()
{
    SetHighlighted(false);
}



/** RepNotify: models changed → rebuild local formation */
void AUnitBase::OnRep_Models()
{
    RebuildFormation();
}

/** RepNotify: movement budget changed → (hook for VFX/UI if desired) */
void AUnitBase::OnRep_Move()
{
    // Intentionally empty for now; your UI can listen via controller/GS and read MoveBudgetInches.
}

/** Ensure there are exactly ModelsCurrent mesh components in a grid. */
void AUnitBase::RebuildFormation()
{
    const int32 Needed = FMath::Max(0, ModelsCurrent);

    // Remove extras
    for (int32 i = ModelMeshes.Num() - 1; i >= Needed; --i)
    {
        if (UStaticMeshComponent* C = ModelMeshes[i]) { C->DestroyComponent(); }
        ModelMeshes.RemoveAt(i);
    }

    // Add missing
    while (ModelMeshes.Num() < Needed)
    {
        const FName CompName = *FString::Printf(TEXT("Model_%02d"), ModelMeshes.Num());
        UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, CompName);
        C->SetupAttachment(RootComponent);
        C->RegisterComponent();
        C->SetCanEverAffectNavigation(false);
        C->SetMobility(EComponentMobility::Movable);
        if (ModelMesh) C->SetStaticMesh(ModelMesh);
        C->SetRenderCustomDepth(false);
        C->SetRelativeScale3D(FVector(ModelScale));
        ModelMeshes.Add(C);
    }

    if (ModelMeshes.Num() == 0) return;

    // Derive footprint from mesh bounds (fallback if no mesh)
    FVector Ext = FVector(50,50,50);
    if (UStaticMesh* SM = ModelMeshes[0]->GetStaticMesh())
        Ext = SM->GetBounds().BoxExtent;

    const float CellX = (Ext.X * 2.f * ModelScale) + ExtraSpacingCmX;
    const float CellY = (Ext.Y * 2.f * ModelScale) + ExtraSpacingCmY;

    if (GridColumns <= 0) GridColumns = 5;
    const int32 Cols = FMath::Max(1, GridColumns);
    const int32 Rows = FMath::CeilToInt(float(ModelMeshes.Num()) / float(Cols));

    // Center the whole rectangle, not per-row
    const float OriginX = -0.5f * (Cols - 1) * CellX;
    const float OriginY = -0.5f * (Rows - 1) * CellY;

    for (int32 i = 0; i < ModelMeshes.Num(); ++i)
    {
        const int32 Row = i / Cols;
        const int32 Col = i % Cols;
        const float X = OriginX + Col * CellX;
        const float Y = OriginY - Row * CellY; // rows “back”
        if (UStaticMeshComponent* C = ModelMeshes[i])
        {
            C->SetRelativeLocation(FVector(X, Y, 0.f));
            C->SetRelativeRotation(FRotator::ZeroRotator);
            C->SetRelativeScale3D(FVector(ModelScale));
        }
    }
}

AActor* AUnitBase::FindNearestEnemyUnit(float MaxSearchDistCm) const
{
    AUnitBase* Best = nullptr;
    float BestSq = MaxSearchDistCm * MaxSearchDistCm;
    const FVector MyLoc = GetActorLocation();

    for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
    {
        AUnitBase* U = *It;
        if (U == this || !U->IsActorInitialized()) continue;
        if (U->OwningPS == OwningPS) continue; // same side
        const float D2 = FVector::DistSquared(MyLoc, U->GetActorLocation());
        if (D2 < BestSq) { BestSq = D2; Best = U; }
    }
    return Best;
}

bool AUnitBase::FaceActorInstant(AActor* Target, float YawSnapDeg)
{
    if (!Target) return false;
    FVector Dir = Target->GetActorLocation() - GetActorLocation();
    Dir.Z = 0.f;
    if (Dir.IsNearlyZero()) return false;

    const FRotator Desired = Dir.Rotation();
    const float DeltaYaw = FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, Desired.Yaw));
    if (DeltaYaw < YawSnapDeg) return false;

    SetActorRotation(FRotator(0.f, Desired.Yaw, 0.f)); // relies on SetReplicateMovement(true)
    return true;
}

void AUnitBase::FaceNearestEnemyInstant()
{
    if (AActor* Enemy = FindNearestEnemyUnit())
    {
        FaceActorInstant(Enemy);
    }
}

/** Replication setup */
void AUnitBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AUnitBase, UnitId);
    DOREPLIFETIME(AUnitBase, Faction);
    DOREPLIFETIME(AUnitBase, OwningPS);
    DOREPLIFETIME(AUnitBase, WeaponIndex);

    DOREPLIFETIME(AUnitBase, ModelsCurrent);
    DOREPLIFETIME(AUnitBase, ModelsMax);

    DOREPLIFETIME(AUnitBase, MoveBudgetInches);
    DOREPLIFETIME(AUnitBase, MoveMaxInches);

    DOREPLIFETIME(AUnitBase, bHasShot);
    DOREPLIFETIME(AUnitBase, ToughnessRep);
    DOREPLIFETIME(AUnitBase, WoundsRep);
    DOREPLIFETIME(AUnitBase, ObjectiveControlPerModel);


    DOREPLIFETIME(AUnitBase, WeaponRangeInchesRep);
    DOREPLIFETIME(AUnitBase, WeaponAttacksRep);
    DOREPLIFETIME(AUnitBase, WeaponDamageRep);

    DOREPLIFETIME(AUnitBase, SaveRep);
    DOREPLIFETIME(AUnitBase, WeaponSkillToHitRep);
    DOREPLIFETIME(AUnitBase, WeaponStrengthRep);
    DOREPLIFETIME(AUnitBase, WeaponAPRep);
    DOREPLIFETIME(AUnitBase, WoundsPool);
}
