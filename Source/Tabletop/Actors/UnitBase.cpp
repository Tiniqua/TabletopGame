#include "UnitBase.h"
#include "Net/UnrealNetwork.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
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

void AUnitBase::GetModelWorldLocations(TArray<FVector>& Out) const
{
    Out.Reset();
    for (UStaticMeshComponent* C : ModelMeshes)
        if (IsValid(C)) Out.Add(C->GetComponentLocation());

    if (Out.Num() == 0) Out.Add(GetActorLocation());
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

    // Remove extra meshes
    for (int32 i = ModelMeshes.Num() - 1; i >= Needed; --i)
    {
        if (UStaticMeshComponent* C = ModelMeshes[i])
        {
            C->DestroyComponent();
        }
        ModelMeshes.RemoveAt(i);
    }

    // Add missing meshes
    while (ModelMeshes.Num() < Needed)
    {
        const FName CompName = *FString::Printf(TEXT("Model_%02d"), ModelMeshes.Num());
        UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, CompName);
        C->SetupAttachment(RootComponent);
        C->RegisterComponent();
        C->SetCanEverAffectNavigation(false);
        C->SetMobility(EComponentMobility::Movable);

        if (ModelMesh)
        {
            C->SetStaticMesh(ModelMesh);
        }

        // Make selection outline respect the unit highlight toggle
        C->SetRenderCustomDepth(false);

        ModelMeshes.Add(C);
    }

    // Position meshes in a simple grid (row-major)
    if (GridColumns <= 0) GridColumns = 5;
    const float Spacing = ModelSpacingCm;

    for (int32 i = 0; i < ModelMeshes.Num(); ++i)
    {
        const int32 Row = i / GridColumns;
        const int32 Col = i % GridColumns;

        // Center the formation around actor origin
        const int32 CountInLastRow = (i == ModelMeshes.Num() - 1) ? ((ModelMeshes.Num() - 1) % GridColumns) + 1 : GridColumns;
        const float TotalCols = (float)GridColumns;

        // Horizontal offset (centered)
        const float X = (Col - (TotalCols - 1) * 0.5f) * Spacing;
        const float Y = (-Row) * Spacing; // move rows “back” (negative Y) to stack

        if (UStaticMeshComponent* C = ModelMeshes[i])
        {
            C->SetRelativeLocation(FVector(X, Y, 0.f));
            C->SetRelativeRotation(FRotator::ZeroRotator);
        }
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

    DOREPLIFETIME(AUnitBase, WeaponRangeInchesRep);
    DOREPLIFETIME(AUnitBase, WeaponAttacksRep);
    DOREPLIFETIME(AUnitBase, WeaponDamageRep);

    DOREPLIFETIME(AUnitBase, SaveRep);
    DOREPLIFETIME(AUnitBase, WeaponSkillToHitRep);
    DOREPLIFETIME(AUnitBase, WeaponStrengthRep);
    DOREPLIFETIME(AUnitBase, WeaponAPRep);
    DOREPLIFETIME(AUnitBase, WoundsPool);
}
