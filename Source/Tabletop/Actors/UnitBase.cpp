#pragma optimize("",off)

#include "UnitBase.h"

#include "EngineUtils.h"
#include "ObjectiveMarker.h"
#include "Net/UnrealNetwork.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "UnitAbility.h"
#include "UnitAction.h"
#include "Kismet/GameplayStatics.h"
#include "Tabletop/UnitActionResourceComponent.h"
#include "Tabletop/WeaponKeywordHelpers.h"
#include "Tabletop/Controllers/MatchPlayerController.h"
#include "Tabletop/Gamemodes/MatchGameMode.h"
#include "Tabletop/PlayerStates/TabletopPlayerState.h"


AUnitBase::AUnitBase()
{
    bReplicates = true;
    AActor::SetReplicateMovement(true);

    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    SelectCollision = CreateDefaultSubobject<USphereComponent>(TEXT("SelectCollision"));
    SelectCollision->SetupAttachment(RootComponent);
    SelectCollision->InitSphereRadius(60.f);
    SelectCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    SelectCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
    SelectCollision->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);

    ActionPoints = CreateDefaultSubobject<UUnitActionResourceComponent>(TEXT("ActionPoints"));
    ActionPoints->SetIsReplicated(true);

    RangeDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("RangeDecal"));
    RangeDecal->SetupAttachment(RootComponent);
    RangeDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f)); // project down onto ground
    RangeDecal->DecalSize = FVector(100.f, 100.f, RangeDecalThickness); // X/Y as radius cm (we’ll overwrite)
    RangeDecal->SetVisibility(false);
    RangeDecal->SetHiddenInGame(true);
    RangeDecal->SetFadeScreenSize(0.f); // don’t auto-fade
    //RangeDecal->rece = false; // this component is a decal, not a receiver

    // Optional invisible probe if you want proximity queries later
    RangeProbe = CreateDefaultSubobject<USphereComponent>(TEXT("RangeProbe"));
    RangeProbe->SetupAttachment(RootComponent);
    RangeProbe->InitSphereRadius(100.f);
    RangeProbe->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RangeProbe->SetVisibility(false);
    RangeProbe->SetHiddenInGame(true);
}

void AUnitBase::BeginPlay()
{
    Super::BeginPlay();
    EnsureRuntimeBuilt();
    EnsureRangeDecal();

    if (HasAuthority())
    {
        RebuildFormation();
    }
}

void AUnitBase::EnsureRangeDecal()
{
    if (!RangeDecal) return;

    if (RangeDecalMaterial && !RangeMID)
    {
        RangeMID = UMaterialInstanceDynamic::Create(RangeDecalMaterial, this);
        RangeDecal->SetDecalMaterial(RangeMID);
        // If your decal material exposes params:
        if (RangeMID && RangeMID->IsValidLowLevel())
        {
            RangeMID->SetScalarParameterValue(TEXT("RingSoftness"), RingSoftness);
            // Color comes per-context; we set it when showing
        }
    }
}

float AUnitBase::GetCmPerTTInch_Safe() const
{
    float cp = 0.f;

    if (const AMatchGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AMatchGameMode>() : nullptr)
    {
        cp = GM->CmPerTabletopInch();
    }

    // If GM not set or returned 0/invalid, try GS
    if (cp <= KINDA_SMALL_NUMBER)
    {
        if (const AMatchGameState* GS = GetWorld() ? GetWorld()->GetGameState<AMatchGameState>() : nullptr)
        {
            cp = GS->CmPerTTInchRep;
        }
    }

    // Final safety net
    if (cp <= KINDA_SMALL_NUMBER)
    {
        cp = 50.8f; // 1 tabletop inch in cm (your original default)
    }

    return cp;
}

void AUnitBase::SetRangeVisible(float RadiusCm, const FLinearColor& Color, ERangeVizMode Mode)
{
    EnsureRangeDecal();
    if (!RangeDecal) return;

    if (RadiusCm <= KINDA_SMALL_NUMBER)
    {
        HideRangePreview();
        return;
    }

    // Size decal: X/Y extents, Z thickness (for your rotated -90° setup)
    RangeDecal->DecalSize = FVector(RangeDecalThickness, RadiusCm, RadiusCm);
    RangeDecal->SetRelativeLocation(FVector(0.f, 0.f, 5.f));

    if (RangeMID)
    {
        RangeMID->SetVectorParameterValue(TEXT("TintColor"), Color);
        RangeMID->SetScalarParameterValue(TEXT("RadiusCm"), RadiusCm);
    }

    if (RangeProbe) RangeProbe->SetSphereRadius(RadiusCm);

    RangeDecal->SetVisibility(true);
    RangeDecal->SetHiddenInGame(false);

    // Decal components sometimes need this to re-evaluate bounds/size
    RangeDecal->MarkRenderStateDirty();         // <- force the component to refresh
    // Optionally: RangeDecal->RecreateRenderState_Concurrent(); (heavier)

    CurrentRangeMode = Mode;
}

void AUnitBase::HideRangePreview()
{
    if (RangeDecal)
    {
        RangeDecal->SetVisibility(false);
        RangeDecal->SetHiddenInGame(true);
    }
    CurrentRangeMode = ERangeVizMode::None;
}

void AUnitBase::UpdateRangePreview(bool bAsTargetContext)
{
    const AMatchGameState* S = GetWorld() ? GetWorld()->GetGameState<AMatchGameState>() : nullptr;
    if (!S) { HideRangePreview(); return; }

    const float cmPer = GetCmPerTTInch_Safe();

    float radiusCm = 0.f;
    ERangeVizMode mode = ERangeVizMode::None;

    if (S->Phase == EMatchPhase::Battle)
    {
        if (S->TurnPhase == ETurnPhase::Move)
        {
            radiusCm = FMath::Max(0.f, MoveBudgetInches) * cmPer;
            mode = ERangeVizMode::Move;
        }
        else if (S->TurnPhase == ETurnPhase::Shoot)
        {
            radiusCm = FMath::Max(0, WeaponRangeInchesRep) * cmPer;
            mode = ERangeVizMode::Shoot;
        }
    }

    const bool bEnemy = bAsTargetContext; // treat target ring as “enemy” color
    const FLinearColor col = bEnemy ? EnemyColor : FriendlyColor;

    if (radiusCm > 0.f) SetRangeVisible(radiusCm, col, mode);
    else HideRangePreview();
}

void AUnitBase::RefreshRangeIfActive()
{
    if (CurrentRangeMode == ERangeVizMode::None) return;

    const AMatchGameState* S = GetWorld() ? GetWorld()->GetGameState<AMatchGameState>() : nullptr;
    if (!S) return;

    bool bAsTarget = (S->TargetUnitGlobal == this);
    bool bAsSel    = (S->SelectedUnitGlobal == this);

    // ALSO consider the local player's selection (use your actual controller class)
    if (!bAsSel)
    {
        if (const AMatchPlayerController* PC = GetWorld()->GetFirstPlayerController<AMatchPlayerController>())
        {
            if (PC->SelectedUnit == this)
            {
                bAsSel = true;
            }
        }
    }

    if (!bAsTarget && !bAsSel)
    {
        HideRangePreview();
        return;
    }

    UpdateRangePreview(/*bAsTargetContext*/ bAsTarget);
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
    bOverwatchArmed = false;

    InvulnerableSaveRep = FMath::Clamp(Row.InvulnSave, 2, 7);
    FeelNoPainRep       = FMath::Clamp(Row.FeelNoPain, 2, 7);

    WoundsPool = ModelsMax * FMath::Max(1, WoundsRep);

    MoveMaxInches    = static_cast<float>(Row.MoveInches);
    MoveBudgetInches = 0.f;

    FX_Muzzle       = Row.FX_Muzzle;
    FX_Impact       = Row.FX_Impact;
    Snd_Muzzle      = Row.Snd_Muzzle;
    Snd_Impact      = Row.Snd_Impact;
    Snd_Selected    = Row.Snd_Selected;
    Snd_UnderFire   = Row.Snd_UnderFire;
    SndAttenuation  = Row.SndAttenuation;
    SndConcurrency  = Row.SndConcurrency;
    ImpactDelaySeconds = Row.ImpactDelaySeconds;

    if (Row.Weapons.Num() > 0)
    {
        WeaponIndex   = FMath::Clamp(InWeaponIndex, 0, Row.Weapons.Num() - 1);
        CurrentWeapon = Row.Weapons[WeaponIndex];        // 🔹 single source of truth
        for (TSubclassOf<UUnitAbility> AC : CurrentWeapon.AbilityClasses)
        {
            if (!*AC) continue;
            if (UUnitAbility* Ab = NewObject<UUnitAbility>(this, AC))
            {
                RuntimeAbilities.Add(Ab);
                Ab->Setup(this);
                if (Ab->GrantsAction)
                {
                    if (UUnitAction* GA = NewObject<UUnitAction>(this, Ab->GrantsAction))
                    {
                        GA->Setup(this);
                        RuntimeActions.Add(GA);
                    }
                }
            }
        }
    }
    else
    {
        WeaponIndex   = 0;
        CurrentWeapon = FWeaponProfile{};                // defaults (Range=24 etc. if you set)
    }

    AbilityClassesRep = Row.AbilityClasses;
    RebuildRuntimeActions();
    
    // Snapshots for UI/fast access stay in sync with CurrentWeapon
    SyncWeaponSnapshotsFromCurrent();

    bMovedThisTurn    = false;
    bAdvancedThisTurn = false;

    RebuildFormation();
    ForceNetUpdate();
}

void AUnitBase::OnRep_AbilityClasses()
{
    EnsureRuntimeBuilt();
}

void AUnitBase::OnRep_ActionUsage()
{
    ActionUsageRuntime.Empty(ActionUsageRep.Num());
    for (const FActionUsageEntry& E : ActionUsageRep)
        ActionUsageRuntime.Add(E.ActionId, E);

    if (UWorld* W = GetWorld())
        if (AMatchGameState* S = W->GetGameState<AMatchGameState>())
            S->OnDeploymentChanged.Broadcast();

    EnsureRuntimeBuilt();
}

bool AUnitBase::CanUseActionNow(const FActionDescriptor& D) const
{
    if (const FActionUsageEntry* E = ActionUsageRuntime.Find(D.ActionId))
    {
        if (D.UsesPerPhase > 0 && E->PerPhase >= D.UsesPerPhase) return false;
        if (D.UsesPerTurn  > 0 && E->PerTurn  >= D.UsesPerTurn)  return false;
        if (D.UsesPerMatch > 0 && E->PerMatch >= D.UsesPerMatch) return false;
    }
    return true;
}

FActionUsageEntry* AUnitBase::FindOrAddUsage(AUnitBase* U, const FName Id)
{
    // runtime cache
    FActionUsageEntry* InMem = U->ActionUsageRuntime.Find(Id);
    if (!InMem)
    {
        FActionUsageEntry NewE; NewE.ActionId = Id;
        U->ActionUsageRuntime.Add(Id, NewE);
        InMem = U->ActionUsageRuntime.Find(Id);
    }

    // replicated array mirror
    FActionUsageEntry* Rep = nullptr;
    for (FActionUsageEntry& E : U->ActionUsageRep)
        if (E.ActionId == Id) { Rep = &E; break; }
    if (!Rep)
    {
        U->ActionUsageRep.Add({Id, 0, 0, 0});
        Rep = &U->ActionUsageRep.Last();
    }
    // Keep addresses separate; return the runtime one for convenience
    return InMem;
}

void AUnitBase::ResetUsageForPhase()
{
    if (!HasAuthority()) return;
    for (auto& KV : ActionUsageRuntime)   KV.Value.PerPhase = 0;
    for (auto& E  : ActionUsageRep)       E.PerPhase = 0;
    ForceNetUpdate();
}

void AUnitBase::ResetUsageForTurn()
{
    if (!HasAuthority()) return;
    for (auto& KV : ActionUsageRuntime)   KV.Value.PerTurn = 0;
    for (auto& E  : ActionUsageRep)       E.PerTurn = 0;
    ForceNetUpdate();
}

void AUnitBase::BumpUsage(const FActionDescriptor& D)
{
    if (!HasAuthority()) return;
    // runtime + rep array stay in sync
    FActionUsageEntry* Runtime = FindOrAddUsage(this, D.ActionId);
    Runtime->PerPhase++; Runtime->PerTurn++; Runtime->PerMatch++;

    // mirror into the replicated entry
    for (FActionUsageEntry& E : ActionUsageRep)
        if (E.ActionId == D.ActionId) { E = *Runtime; break; }

    ForceNetUpdate();
}

void AUnitBase::EnsureRuntimeBuilt()
{
    // Actions: if empty, add the three base ones
    if (RuntimeActions.Num() == 0)
    {
        RuntimeActions.Add(NewObject<UAction_Move>(this));
        RuntimeActions.Add(NewObject<UAction_Advance>(this));
        RuntimeActions.Add(NewObject<UAction_Shoot>(this));
        for (UUnitAction* A : RuntimeActions) if (A) A->Setup(this);
    }

    // Abilities: (re)create from replicated classes
    // (If you later add per-match upgrades etc., add guards as needed)
    if (RuntimeAbilities.Num() == 0 && AbilityClassesRep.Num() > 0)
    {
        for (TSubclassOf<UUnitAbility> AC : AbilityClassesRep)
        {
            if (!*AC) continue;
            if (UUnitAbility* Ab = NewObject<UUnitAbility>(this, AC))
            {
                RuntimeAbilities.Add(Ab);
                Ab->Setup(this);

                // auto-grant an action if the ability exposes one
                if (Ab->GetClass()->FindPropertyByName(TEXT("GrantsAction")))
                {
                    // Access the property safely
                    if (UUnitAction* GA = Ab->GrantsAction ? NewObject<UUnitAction>(this, Ab->GrantsAction) : nullptr)
                    {
                        GA->Setup(this);
                        RuntimeActions.Add(GA);
                    }
                }
            }
        }
    }
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

void AUnitBase::ApplyAPPhaseStart(ETurnPhase Phase)
{
    if (!HasAuthority()) return;

    UUnitActionResourceComponent* AP = FindComponentByClass<UUnitActionResourceComponent>();
    if (!AP) return;

    // Start from defaults every phase
    const int32 BaseMax = AP->DefaultMaxAP;
    int32 NewMax = BaseMax;

    // 1) Consume pending debt from previous phase
    const int32 Debt = FMath::Max(0, NextPhaseAPDebt);
    NewMax = FMath::Max(0, NewMax - Debt);
    NextPhaseAPDebt = 0; // consumed now

    // 2) Assault: +1 AP in Shooting phase (new rule, no Advance requirement)
    if (Phase == ETurnPhase::Shoot &&
        UWeaponKeywordHelpers::HasKeyword(GetActiveWeaponProfile(), EWeaponKeyword::Assault))
    {
        ++NewMax;
    }

    // 3) Objective control: +1 AP if this unit is inside an objective it controls,
    //    BUT NOT on Round 1 Move (deployment advantage guard).
    bool bGiveObjectiveAP = true;
    if (const AMatchGameState* GS = GetWorld() ? GetWorld()->GetGameState<AMatchGameState>() : nullptr)
    {
        if (GS->CurrentRound <= 1 && Phase == ETurnPhase::Move)
        {
            bGiveObjectiveAP = false; // no objective AP on the very first movement phase
        }
    }

    if (bGiveObjectiveAP)
    {
        // Iterate objectives and check control + presence
        for (TActorIterator<AObjectiveMarker> It(GetWorld()); It; ++It)
        {
            const AObjectiveMarker* Obj = *It;
            if (!Obj) continue;

            if (Obj->GetControllingPlayerState() == OwningPS && GetObjectiveControlAt(Obj) > 0)
            {
                ++NewMax; // only +1 even if standing in multiple; break after first
                break;
            }
        }
    }

    // Apply and replicate
    AP->MaxAP     = FMath::Max(0, NewMax);
    AP->CurrentAP = AP->MaxAP;

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

    // Keep the mesh’s world rotation; no FacingYawOffsetDeg here
    return FTransform(WT.GetRotation(), WLoc, WT.GetScale3D());
}

void AUnitBase::Multicast_PlayMuzzleAndImpactFX_AllModels_WithSites_Implementation(const FVector& TargetCenter, const TArray<FImpactSite>& Sites, float DelaySeconds)
{
    // ---- MUZZLE (aim from each muzzle to the provided target center) ----
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

    // ---- IMPACT (use only cached sites; target actor can be gone) ----
    TArray<FImpactSite> SitesCopy = Sites; // capture by value for lambda safety

    FTimerDelegate Del;
    Del.BindLambda([this, SitesCopy]()
    {
        UWorld* W = GetWorld();
        if (!W) return;

        if (SitesCopy.Num() == 0)
        {
            // Fallback: single center impact at attacker forward a bit (or skip)
            const FVector FallbackLoc = GetActorLocation() + GetActorForwardVector()*100.f + FVector(0,0,50.f);
            if (FX_Impact) UNiagaraFunctionLibrary::SpawnSystemAtLocation(W, FX_Impact, FallbackLoc, GetActorRotation());
            if (Snd_Impact) UGameplayStatics::PlaySoundAtLocation(this, Snd_Impact, FallbackLoc, 1.f, 1.f, 0.f, SndAttenuation, SndConcurrency);
            return;
        }

        for (const FImpactSite& S : SitesCopy)
        {
            if (FX_Impact) UNiagaraFunctionLibrary::SpawnSystemAtLocation(W, FX_Impact, S.Loc, S.Rot);
            if (Snd_Impact) UGameplayStatics::PlaySoundAtLocation(this, Snd_Impact, S.Loc, 1.f, 1.f, 0.f, SndAttenuation, SndConcurrency);
        }
    });

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

    for (int32 j = 0; j < TargetUnit->ModelMeshes.Num(); ++j)
    {
        UStaticMeshComponent* TC = TargetUnit->ModelMeshes[j];
        if (!IsValid(TC)) continue;

        // Compute the exact impact location per target model
        FVector ImpactLoc;
        if (TC->DoesSocketExist(TargetUnit->ImpactSocketName))
        {
            ImpactLoc = TC->GetSocketTransform(TargetUnit->ImpactSocketName, ERelativeTransformSpace::RTS_World).GetLocation();
        }
        else
        {
            ImpactLoc = TC->GetComponentTransform().TransformPosition(TargetUnit->ImpactOffsetLocal);
        }

        // Find the shooter muzzle closest to THIS impact point
        const int32 BestShooterIdx = FindBestShooterModelIndex(ImpactLoc);
        const FVector BestMuzzleLoc = GetMuzzleTransform(BestShooterIdx).GetLocation();

        // Incoming direction = (target -> shooter)
        const FRotator IncomingRot = (BestMuzzleLoc - ImpactLoc).Rotation();

        if (FX_Impact)
        {
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), FX_Impact, ImpactLoc, IncomingRot);
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

const TArray<UUnitAction*>& AUnitBase::GetActions()
{
    if (RuntimeActions.Num() == 0)
    {
        EnsureRuntimeBuilt();   // builds Move/Advance/Shoot and any granted by abilities
    }
    
    return RuntimeActions;
}

const TArray<UUnitAbility*>& AUnitBase::GetAbilities()
{
    return RuntimeAbilities;
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

    if (Snd_UnderFire)
    {
        UGameplayStatics::PlaySoundAtLocation(this, Snd_UnderFire, GetActorLocation(),
            1.f, 1.f, 0.f, SndAttenuation, SndConcurrency);
    }
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
    case EUnitHighlight::Friendly:       ApplyOutlineToAllModels(OutlineFriendlyMaterial);        break;
    case EUnitHighlight::Enemy:          ApplyOutlineToAllModels(OutlineEnemyMaterial);           break;
    case EUnitHighlight::PotentialEnemy: ApplyOutlineToAllModels(OutlinePotentialEnemyMaterial);  break;
    case EUnitHighlight::PotentialAlly: ApplyOutlineToAllModels(OutlinePotentialAllyMaterial); break;
    default:                              ApplyOutlineToAllModels(nullptr);                       break;
    }
}

void AUnitBase::OnSelected()
{
    SetHighlightLocal(EUnitHighlight::Friendly);

    UpdateRangePreview(/*bAsTargetContext=*/false);

    if (Snd_Selected)
    {
        UGameplayStatics::PlaySoundAtLocation(this, Snd_Selected, GetActorLocation(),
            1.f, 1.f, 0.f, SndAttenuation, SndConcurrency);
    }
}

void AUnitBase::OnDeselected()
{
    SetHighlightLocal(EUnitHighlight::None);
    HideRangePreview();
}


void AUnitBase::OnRep_Models()
{
    RebuildFormation();
    EnsureRuntimeBuilt();
}

void AUnitBase::OnRep_Move()
{
    OnMoveChanged.Broadcast();
    EnsureRuntimeBuilt();
    RefreshRangeIfActive();
}

void AUnitBase::OnRep_CurrentWeapon()
{
    SyncWeaponSnapshotsFromCurrent();
    RebuildRuntimeActions();
    RefreshRangeIfActive();
}

void AUnitBase::NotifyMoveChanged()
{
    OnMoveChanged.Broadcast();
    if (HasAuthority())
    {
        ForceNetUpdate(); // push to clients a bit sooner
    }
}

void AUnitBase::ApplyFormationOffsetsLocal(const TArray<FVector>& OffsetsLocal)
{
    // Ensure the right number of components exist locally on each client
    RebuildFormation();

    const int32 N = FMath::Min(ModelMeshes.Num(), OffsetsLocal.Num());
    for (int32 i = 0; i < N; ++i)
    {
        if (UStaticMeshComponent* C = ModelMeshes[i])
        {
            const FVector P = OffsetsLocal[i];
            C->SetRelativeLocation(FVector(P.X, P.Y, 0.f));
        }
    }
}

void AUnitBase::RebuildRuntimeAbilitiesFromSources()
{
    RuntimeAbilities.Empty();

    auto AddAbilityList = [&](const TArray<TSubclassOf<UUnitAbility>>& Classes)
    {
        for (TSubclassOf<UUnitAbility> AC : Classes)
        {
            if (!*AC) continue;
            if (UUnitAbility* Ab = NewObject<UUnitAbility>(this, AC))
            {
                RuntimeAbilities.Add(Ab);
                Ab->Setup(this);
            }
        }
    };

    // Row-level abilities (replicated via AbilityClassesRep already)
    AddAbilityList(AbilityClassesRep);

    // Weapon-level abilities
    AddAbilityList(CurrentWeapon.AbilityClasses);
}

void AUnitBase::RebuildRuntimeActions()
{
    RuntimeActions.Empty();

    // Base kit
    RuntimeActions.Add(NewObject<UAction_Move>(this));
    RuntimeActions.Add(NewObject<UAction_Advance>(this));
    RuntimeActions.Add(NewObject<UAction_Shoot>(this));
    //RuntimeActions.Add(NewObject<UAction_Overwatch>(this)); // if you keep it global

    // (Re)build abilities first
    RebuildRuntimeAbilitiesFromSources();

    // Abilities may grant actions
    for (UUnitAbility* Ab : RuntimeAbilities)
    {
        if (!Ab) continue;
        if (Ab->GetClass()->FindPropertyByName(TEXT("GrantsAction")))
        {
            if (UUnitAction* GA = Ab->GrantsAction ? NewObject<UUnitAction>(this, Ab->GrantsAction) : nullptr)
            {
                GA->Setup(this);
                RuntimeActions.Add(GA);
            }
        }
    }

    // Final setup pass (safe if called twice)
    for (UUnitAction* A : RuntimeActions) if (A) A->Setup(this);
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
    CurrentTarget = Target;
    ForceNetUpdate();
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
    DOREPLIFETIME(AUnitBase, CurrentTarget);

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
    DOREPLIFETIME(AUnitBase, InvulnerableSaveRep);
    DOREPLIFETIME(AUnitBase, FeelNoPainRep);

    DOREPLIFETIME(AUnitBase, AbilityClassesRep);
    DOREPLIFETIME(AUnitBase, NextPhaseAPDebt);
    DOREPLIFETIME(AUnitBase, bOverwatchArmed);
}
