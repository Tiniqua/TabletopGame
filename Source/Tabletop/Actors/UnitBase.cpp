#include "UnitBase.h"

#include "EngineUtils.h"
#include "ObjectiveMarker.h"
#include "Net/UnrealNetwork.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Kismet/GameplayStatics.h"
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
    SelectCollision->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
}

void AUnitBase::BeginPlay()
{
    Super::BeginPlay();

    if (HasAuthority())
    {
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

    UnitName = Row.DisplayName;
    ModelsMax     = Row.Models;
    ModelsCurrent = ModelsMax;

    ToughnessRep = Row.Toughness;
    WoundsRep    = Row.Wounds;
    SaveRep      = Row.Save;

    WoundsPool = ModelsMax * FMath::Max(1, WoundsRep);

    MoveMaxInches    = static_cast<float>(Row.MoveInches);
    MoveBudgetInches = 0.f;

    if (Row.Weapons.Num() > 0)
    {
        WeaponIndex   = FMath::Clamp(InWeaponIndex, 0, Row.Weapons.Num() - 1);
        CurrentWeapon = Row.Weapons[WeaponIndex];        // 🔹 single source of truth
    }
    else
    {
        WeaponIndex   = 0;
        CurrentWeapon = FWeaponProfile{};                // defaults (Range=24 etc. if you set)
    }

    // Snapshots for UI/fast access stay in sync with CurrentWeapon
    SyncWeaponSnapshotsFromCurrent();

    bMovedThisTurn    = false;
    bAdvancedThisTurn = false;

    RebuildFormation();
    ForceNetUpdate();
}

void AUnitBase::SyncWeaponSnapshotsFromCurrent()
{
    // Mirror a few frequently-read ints (optional, but you already have UIs reading these)
    WeaponRangeInchesRep = CurrentWeapon.RangeInches;
    WeaponAttacksRep     = CurrentWeapon.Attacks;
    WeaponDamageRep      = CurrentWeapon.Damage;
    WeaponSkillToHitRep  = CurrentWeapon.SkillToHit;
    WeaponStrengthRep    = CurrentWeapon.Strength;
    WeaponAPRep          = CurrentWeapon.AP;
}

void AUnitBase::OnRep_CurrentWeapon()
{
    // Keep cached ints consistent on clients
    SyncWeaponSnapshotsFromCurrent();
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

    FRotator OutRot = WT.Rotator();
    OutRot.Yaw += FacingYawOffsetDeg;

    return FTransform(OutRot, WLoc, WT.GetScale3D());
}

void AUnitBase::Multicast_PlayMuzzleAndImpactFX_AllModels_Implementation(AUnitBase* TargetUnit, float DelaySeconds)
{
    if (!IsValid(TargetUnit)) return;

    const FVector TargetCenter = TargetUnit->GetActorLocation();
    for (int32 i = 0; i < ModelMeshes.Num(); ++i)
    {
        UStaticMeshComponent* C = ModelMeshes[i];
        if (!IsValid(C)) continue;

        const FTransform Muzz = GetMuzzleTransform(i);
        const FVector MuzzLoc = Muzz.GetLocation();
        const FRotator AimRot = (TargetCenter - MuzzLoc).Rotation();

        if (FX_Muzzle) UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), FX_Muzzle, MuzzLoc, AimRot);
        if (Snd_Muzzle) UGameplayStatics::PlaySoundAtLocation(this, Snd_Muzzle, MuzzLoc, 1.f, 1.f, 0.f, SndAttenuation, SndConcurrency);
    }

    FTimerDelegate Del;
    Del.BindUFunction(this, FName("PlayImpactFXAndSounds_Delayed"), TargetUnit);

    GetWorld()->GetTimerManager().ClearTimer(ImpactFXTimerHandle);
    GetWorld()->GetTimerManager().SetTimer(ImpactFXTimerHandle, Del, FMath::Max(0.f, DelaySeconds), false);
}

int32 AUnitBase::FindBestShooterModelIndex(const FVector& TargetWorld) const
{
    int32 BestIdx = INDEX_NONE;
    float BestD2  = TNumericLimits<float>::Max();

    for (int32 i = 0; i < ModelMeshes.Num(); ++i)
    {
        UStaticMeshComponent* C = ModelMeshes[i];
        if (!IsValid(C)) continue;

        const FVector MuzzleLoc = GetMuzzleTransform(i).GetLocation();
        const float D2 = FVector::DistSquaredXY(MuzzleLoc, TargetWorld);
        if (D2 < BestD2)
        {
            BestD2  = D2;
            BestIdx = i;
        }
    }
    return (BestIdx == INDEX_NONE) ? 0 : BestIdx;
}

void AUnitBase::PlayImpactFXAndSounds_Delayed(AUnitBase* TargetUnit)
{
    if (!IsValid(TargetUnit)) return;

    const FVector AttackerCenter = GetActorLocation();

    for (int32 j = 0; j < TargetUnit->ModelMeshes.Num(); ++j)
    {
        UStaticMeshComponent* TC = TargetUnit->ModelMeshes[j];
        if (!IsValid(TC)) continue;

        FVector ImpactLoc;
        if (TC->DoesSocketExist(TargetUnit->ImpactSocketName))
        {
            ImpactLoc = TC->GetSocketTransform(TargetUnit->ImpactSocketName, ERelativeTransformSpace::RTS_World).GetLocation();
        }
        else
        {
            ImpactLoc = TC->GetComponentTransform().TransformPosition(TargetUnit->ImpactOffsetLocal);
        }

        const FVector InDir = (ImpactLoc - AttackerCenter).GetSafeNormal();
        FRotator ImpactRot = InDir.Rotation();
        ImpactRot.Yaw -= FacingYawOffsetDeg; 

        if (FX_Impact)
        {
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), FX_Impact, ImpactLoc, ImpactRot);
        }
        if (Snd_Impact)
        {
            UGameplayStatics::PlaySoundAtLocation(this, Snd_Impact, ImpactLoc, 1.f, 1.f, 0.f, SndAttenuation, SndConcurrency);
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

    TArray<FVector> ModelLocs;
    GetModelWorldLocations(ModelLocs);
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
        Destroy();
        return;
    }

    ForceNetUpdate();
}

void AUnitBase::ApplyMortalDamage_Server(int32 Damage)
{
    // For now treat as normal unsavable damage; can extend if you track separate pools.
    ApplyDamage_Server(Damage);
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
    const int32 PerModel = FMath::Max(1, WoundsRep);
    int32 NewModels = (WoundsPool + PerModel - 1) / PerModel;
    NewModels = FMath::Clamp(NewModels, 0, ModelsMax);

    if (NewModels != ModelsCurrent)
    {
        ModelsCurrent = NewModels;
    }
    RebuildFormation();
}

void AUnitBase::AddUnitModifier(const FUnitModifier& Mod)
{
    ActiveCombatMods.Add(Mod);
}

static bool MatchesRole(const FUnitModifier& M, bool bAsAttacker)
{
    if (M.Targeting == EModifierTarget::OwnerAlways)        return true;
    if (M.Targeting == EModifierTarget::OwnerWhenAttacking) return bAsAttacker;
    if (M.Targeting == EModifierTarget::OwnerWhenDefending) return !bAsAttacker;
    return false;
}

FRollModifiers AUnitBase::CollectStageMods(ECombatEvent Stage, bool bAsAttacker, const AUnitBase* /*Opp*/) const
{
    FRollModifiers Out;
    for (const FUnitModifier& M : ActiveCombatMods)
    {
        if (M.AppliesAt != Stage) continue;
        if (!MatchesRole(M, bAsAttacker)) continue;

        Out.Accumulate(M.Mods);
    }
    return Out;
}

void AUnitBase::ConsumeForStage(ECombatEvent Stage, bool bAsAttacker)
{
    for (int32 i = ActiveCombatMods.Num()-1; i >= 0; --i)
    {
        FUnitModifier& M = ActiveCombatMods[i];
        if (M.AppliesAt != Stage || !MatchesRole(M, bAsAttacker)) continue;

        if (M.Expiry == EModifierExpiry::NextNOwnerShots && bAsAttacker)
        {
            if (M.UsesRemaining > 0 && --M.UsesRemaining == 0)
                ActiveCombatMods.RemoveAtSwap(i);
        }
        else if (M.Expiry == EModifierExpiry::Uses)
        {
            if (M.UsesRemaining > 0 && --M.UsesRemaining == 0)
                ActiveCombatMods.RemoveAtSwap(i);
        }
    }
}

void AUnitBase::OnTurnAdvanced()
{
    for (int32 i = ActiveCombatMods.Num()-1; i >= 0; --i)
    {
        FUnitModifier& M = ActiveCombatMods[i];
        if (M.Expiry == EModifierExpiry::UntilEndOfTurn && --M.TurnsRemaining <= 0)
            ActiveCombatMods.RemoveAtSwap(i);
    }
}

void AUnitBase::OnRoundAdvanced()
{
    for (int32 i = ActiveCombatMods.Num()-1; i >= 0; --i)
    {
        FUnitModifier& M = ActiveCombatMods[i];
        if (M.Expiry == EModifierExpiry::UntilEndOfRound && --M.TurnsRemaining <= 0)
            ActiveCombatMods.RemoveAtSwap(i);
    }
}
void AUnitBase::ApplyOutlineToAllModels(UMaterialInterface* Mat)
{
    for (UStaticMeshComponent* C : ModelMeshes)
    {
        if (!IsValid(C)) continue;
        C->SetOverlayMaterial(Mat);
    }
}

void AUnitBase::SetHighlightLocal(EUnitHighlight Mode)
{
    if (CurrentHighlight == Mode) return;
    CurrentHighlight = Mode;

    switch (Mode)
    {
    case EUnitHighlight::Friendly:
        ApplyOutlineToAllModels(OutlineFriendlyMaterial);
        break;
    case EUnitHighlight::Enemy:
        ApplyOutlineToAllModels(OutlineEnemyMaterial);
        break;
    default:
        ApplyOutlineToAllModels(nullptr);
        break;
    }
}

void AUnitBase::OnSelected()
{
    SetHighlightLocal(EUnitHighlight::Friendly);
}

void AUnitBase::OnDeselected()
{
    SetHighlightLocal(EUnitHighlight::None);
}


void AUnitBase::OnRep_Models()
{
    RebuildFormation();
}

void AUnitBase::OnRep_Move()
{
    // Hook for UI if needed
}

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

        C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        C->SetGenerateOverlapEvents(false);
        C->SetCollisionResponseToAllChannels(ECR_Ignore);
        C->SetCollisionResponseToChannel(SelectionTraceECC, ECR_Block);
        C->SetCollisionResponseToChannel(ECC_GameTraceChannel5 /*LOS*/, ECR_Ignore);

        ModelMeshes.Add(C);
    }

    if (ModelMeshes.Num() == 0) return;

    const int32 N = FMath::Max(1, ModelsCurrent);

    // Use explicit per-unit spacing with optional extra spacing
    float d = FMath::Max(5.f, ModelSpacingApartCm) + ExtraSpacingCmX; // uniform hex spacing

    // Slight tighten for small squads (helps 5-mans feel closer)
    if (N <= 5)      d *= 0.82f;
    else if (N <=10) d *= 0.90f;

    auto AxialToXY = [d](int q, int r) -> FVector2D
    {
        const float x = d * (q + r * 0.5f);
        const float y = d * (FMath::Sqrt(3.f) * 0.5f) * r;
        return FVector2D(x, y);
    };

    struct FCube { int x, y, z; };
    auto Dir = [](int i)->FCube {
        static const FCube dirs[6] = {
            { 1,-1, 0},{ 1, 0,-1},{ 0, 1,-1},
            {-1, 1, 0},{-1, 0, 1},{ 0,-1, 1}
        };
        return dirs[i % 6];
    };
    auto Add = [](FCube a, FCube b){ return FCube{a.x+b.x, a.y+b.y, a.z+b.z}; };

    TArray<FVector2D> points; points.Reserve(Needed);
    points.Add(FVector2D::ZeroVector);

    for (int ring = 1; points.Num() < Needed; ++ring)
    {
        FCube cur{ring, -ring, 0};
        for (int side = 0; side < 6 && points.Num() < Needed; ++side)
        {
            const FCube step = Dir(side);
            for (int stepIdx = 0; stepIdx < ring && points.Num() < Needed; ++stepIdx)
            {
                points.Add(AxialToXY(cur.x, cur.z));
                cur = Add(cur, step);
            }
        }
    }

    FVector2D centroid(0,0);
    for (const auto& p : points) centroid += p;
    centroid /= float(points.Num());

    for (int32 i = 0; i < ModelMeshes.Num(); ++i)
    {
        if (UStaticMeshComponent* C = ModelMeshes[i])
        {
            const FVector2D p = points[i] - centroid;
            C->SetRelativeLocation(FVector(p.X, p.Y, 0.f));
            C->SetRelativeRotation(FRotator(0.f, ModelYawVisualOffsetDeg, 0.f));
            C->SetRelativeScale3D(FVector(ModelScale));
        }
    }
}

void AUnitBase::VisualFaceYaw(float WorldYaw)
{
    const float LocalYaw = WorldYaw - GetActorRotation().Yaw;

    for (UStaticMeshComponent* C : ModelMeshes)
    {
        if (!C) continue;
        C->SetRelativeRotation(FRotator(0.f, LocalYaw + ModelYawVisualOffsetDeg, 0.f));
    }
}

void AUnitBase::VisualFaceActor(AActor* Target)
{
    if (!Target) return;
    FVector Dir = Target->GetActorLocation() - GetActorLocation();
    Dir.Z = 0.f;
    if (Dir.IsNearlyZero()) return;

    const float Yaw = Dir.Rotation().Yaw;
    VisualFaceYaw(Yaw);
}

AActor* AUnitBase::FindNearestEnemyUnit(float MaxSearchDistCm) const
{
    AUnitBase* Best = nullptr;
    float BestSq = MaxSearchDistCm * MaxSearchDistCm;
    const FVector MyLoc = GetActorLocation();

    for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
    {
        AUnitBase* U = *It;
        if (!U || U == this || !U->IsActorInitialized()) continue;
        if (!IsEnemy(U)) continue;

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

    SetActorRotation(FRotator(0.f, Desired.Yaw + FacingYawOffsetDeg, 0.f));
    return true;
}

void AUnitBase::FaceNearestEnemyInstant()
{
    if (AActor* Enemy = FindNearestEnemyUnit())
    {
        FaceActorInstant(Enemy);
    }
}

bool AUnitBase::IsEnemy(const AUnitBase* Other) const
{
    if (!Other || Other == this) return false;

    APlayerState* MyPS    = OwningPS;
    APlayerState* TheirPS = Other->OwningPS;

    if (MyPS && TheirPS)
    {
        const ATabletopPlayerState* M = Cast<ATabletopPlayerState>(MyPS);
        const ATabletopPlayerState* T = Cast<ATabletopPlayerState>(TheirPS);
        if (M && T && M->TeamNum > 0 && T->TeamNum > 0)
        {
            return M->TeamNum != T->TeamNum;
        }
        return MyPS != TheirPS;
    }
    return false;
}

void AUnitBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AUnitBase, UnitId);
    DOREPLIFETIME(AUnitBase, UnitName);
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

    DOREPLIFETIME(AUnitBase, ActiveCombatMods);
    DOREPLIFETIME(AUnitBase, bMovedThisTurn);
    DOREPLIFETIME(AUnitBase, bAdvancedThisTurn);

    DOREPLIFETIME(AUnitBase, CurrentWeapon);

}
