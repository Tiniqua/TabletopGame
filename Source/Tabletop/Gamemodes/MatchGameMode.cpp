
#include "MatchGameMode.h"

#include "EngineUtils.h"
#include "SetupGamemode.h"
#include "Components/LineBatchComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Tabletop/Actors/CoverVolume.h"
#include "Tabletop/Actors/DeploymentZone.h"
#include "Tabletop/Actors/NetDebugTextActor.h"
#include "Tabletop/Actors/ObjectiveMarker.h"
#include "Tabletop/Actors/UnitBase.h"
#include "Tabletop/Characters/TabletopCharacter.h"
#include "Tabletop/Controllers/MatchPlayerController.h"
#include "Tabletop/PlayerStates/TabletopPlayerState.h"

namespace
{
    FORCEINLINE float CmToInches(float Cm) { return Cm / 2.54f; }
    FORCEINLINE float DistInches(const FVector& A, const FVector& B) { return FVector::Dist(A, B) / 2.54f; }

    // When moving into melee, keep a little gap to avoid direct overlap. Tweak to your base size.
    constexpr float BaseToBaseGapCm = 50.f; // ~19.7 inches if using big base; adjust as needed
}

namespace
{
    FORCEINLINE int32 D6() { return FMath::RandRange(1, 6); }

    // 40k to-wound threshold from S vs T
    FORCEINLINE int32 ToWoundTarget(int32 S, int32 T)
    {
        if (S >= 2*T)     return 2;
        if (S >  T)       return 3;
        if (S == T)       return 4;
        if (2*S <= T)     return 6;
        /* S < T */       return 5;
    }

    // 40k: worsen the save by AP (AP 0→no change, AP 2 → +2 to needed roll)
    // clamp to [2..7]; 7 means "no save" (auto-fail)
    FORCEINLINE int32 ModifiedSaveNeed(int32 BaseSave, int32 AP)
    {
        int32 Need = BaseSave + FMath::Max(0, AP);
        Need = FMath::Clamp(Need, 2, 7);
        return Need;
    }
}

static APlayerState* ResolveControllerPS(const AMatchGameState* S, const APlayerController* PC)
{
    if (!S || !PC) return nullptr;

    APlayerState* PS = PC->PlayerState;
    if (!PS) return nullptr;

    // 1) Direct pointer match (fast path, preferred)
    if (PS == S->P1) return S->P1;
    if (PS == S->P2) return S->P2;

    // 2) Fallback: TeamNum mapping (helps after seamless travel / swaps)
    const ATabletopPlayerState* TPS  = Cast<ATabletopPlayerState>(PS);
    const ATabletopPlayerState* TP1  = S->P1 ? Cast<ATabletopPlayerState>(S->P1) : nullptr;
    const ATabletopPlayerState* TP2  = S->P2 ? Cast<ATabletopPlayerState>(S->P2) : nullptr;

    if (TPS && TP1 && TPS->TeamNum > 0 && TPS->TeamNum == TP1->TeamNum) return S->P1;
    if (TPS && TP2 && TPS->TeamNum > 0 && TPS->TeamNum == TP2->TeamNum) return S->P2;

    // 3) Couldn’t resolve — returns nullptr so callers can early‑out safely.
    return nullptr;
}

void AMatchGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AMatchGameState, Phase);
    DOREPLIFETIME(AMatchGameState, CurrentDeployer);
    DOREPLIFETIME(AMatchGameState, P1Remaining);
    DOREPLIFETIME(AMatchGameState, P2Remaining);
    DOREPLIFETIME(AMatchGameState, bDeploymentComplete);
    DOREPLIFETIME(AMatchGameState, P1);
    DOREPLIFETIME(AMatchGameState, P2);
    DOREPLIFETIME(AMatchGameState, bTeamsAndTurnsInitialized);
    DOREPLIFETIME(AMatchGameState, CurrentRound);
    DOREPLIFETIME(AMatchGameState, MaxRounds);
    DOREPLIFETIME(AMatchGameState, TurnInRound);
    DOREPLIFETIME(AMatchGameState, TurnPhase);
    DOREPLIFETIME(AMatchGameState, CurrentTurn);
    DOREPLIFETIME(AMatchGameState, ScoreP1);
    DOREPLIFETIME(AMatchGameState, ScoreP2);
    DOREPLIFETIME(AMatchGameState, Preview);
    DOREPLIFETIME(AMatchGameState, ActionPreview);
    
}

void AMatchGameState::Multicast_ScreenMsg_Implementation(const FString& Text, FColor Color, float Time, int32 Key)
{
    if (!bEnableNetDebugDraw) return;
    if (GEngine) GEngine->AddOnScreenDebugMessage(Key, Time, Color, Text);
}

void AMatchGameState::Multicast_DrawWorldText_Implementation(const FVector& WorldLoc, const FString& Text, FColor Color, float Time, float /*FontScale*/)
{
    if (!bEnableNetDebugDraw) return;
    UWorld* W = GetWorld(); if (!W) return;

    //const TSubclassOf<ANetDebugTextActor> Cls = DebugTextActorClass ? DebugTextActorClass : ANetDebugTextActor::StaticClass();
    FTransform T(FRotator::ZeroRotator, WorldLoc);
    TSubclassOf<ANetDebugTextActor> Cls = DebugTextActorClass;
    if (!Cls)
    {
        Cls = TSubclassOf<ANetDebugTextActor>(ANetDebugTextActor::StaticClass());
    }

    // usage:
    if (ANetDebugTextActor* A = W->SpawnActorDeferred<ANetDebugTextActor>(Cls, T))
    {
        A->Init(Text, Color, DebugTextWorldSize, Time);
        UGameplayStatics::FinishSpawningActor(A, T);
    }
}

static void DrawLineViaBatcher(UWorld* World, const FVector& A, const FVector& B, const FLinearColor& Color, float Thickness, float Time)
{
    if (!World) return;
    if (ULineBatchComponent* LB = World->PersistentLineBatcher ? World->PersistentLineBatcher : World->LineBatcher)
    {
        LB->DrawLine(A, B, Color.ToFColor(true), SDPG_World, Thickness, Time);
        LB->MarkRenderStateDirty();
        return;
    }
    // Fallback (may be compiled out in Shipping)
    DrawDebugLine(World, A, B, Color.ToFColor(true), false, Time, 0, Thickness);
}

void AMatchGameState::Multicast_DrawLine_Implementation(const FVector& Start, const FVector& End, FColor Color, float Time, float Thickness)
{
    if (!bEnableNetDebugDraw) return;
    DrawLineViaBatcher(GetWorld(), Start, End, Color, Thickness, Time);
}

void AMatchGameState::Multicast_DrawSphere_Implementation(const FVector& Center, float Radius, int32 Segments, FColor Color, float Time, float Thickness)
{
    if (!bEnableNetDebugDraw) return;
    UWorld* World = GetWorld();
    if (!World) return;

    // Use 3 great-circle rings via the batcher (Shipping-safe), or fallback to DrawDebugSphere.
    const int32 N = FMath::Max(8, Segments);
    auto Ring = [&](const FVector& X, const FVector& Y)
    {
        FVector Prev = Center + X * Radius;
        for (int32 i=1;i<=N;++i)
        {
            const float T = (float)i / (float)N * 2.f * PI;
            const FVector P = Center + (X*FMath::Cos(T) + Y*FMath::Sin(T)) * Radius;
            DrawLineViaBatcher(World, Prev, P, Color, Thickness, Time);
            Prev = P;
        }
    };
    Ring(FVector::RightVector, FVector::ForwardVector); // XY
    Ring(FVector::RightVector, FVector::UpVector);      // XZ
    Ring(FVector::UpVector,    FVector::ForwardVector); // YZ
}

void AMatchGameState::BeginPlay()
{
    Super::BeginPlay();
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AObjectiveMarker::StaticClass(), Found);
    Objectives.Reserve(Found.Num());
    for (AActor* A : Found)
        if (auto* M = Cast<AObjectiveMarker>(A)) Objectives.Add(M);
}

void AMatchGameState::Multicast_DrawShotDebug_Implementation(const FVector& WorldLoc, const FString& Msg, FColor Color, float Duration)
{
    if (!bEnableNetDebugDraw) return;
    UWorld* W = GetWorld(); if (!W) return;

    FTransform T(FRotator::ZeroRotator, WorldLoc);
    TSubclassOf<ANetDebugTextActor> Cls = DebugTextActorClass;
    if (!Cls)
    {
        Cls = TSubclassOf<ANetDebugTextActor>(ANetDebugTextActor::StaticClass());
    }

    // usage:
    if (ANetDebugTextActor* A = W->SpawnActorDeferred<ANetDebugTextActor>(Cls, T))
    {
        A->Init(Msg, Color, DebugTextWorldSize * 1.1f, Duration);
        UGameplayStatics::FinishSpawningActor(A, T);
    }
}

AMatchGameMode::AMatchGameMode()
{
    GameStateClass = AMatchGameState::StaticClass();
    PlayerStateClass   = ATabletopPlayerState::StaticClass();
    DefaultPawnClass = ATabletopCharacter::StaticClass();
    bUseSeamlessTravel = true;
}

void AMatchGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
    Super::HandleSeamlessTravelPlayer(C);
    if (!C) return;
    
    FinalizePlayerJoin(Cast<APlayerController>(C)); 
}

void AMatchGameMode::ResetTurnFor(APlayerState* PS)
{
    for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
    {
        if (It->OwningPS == PS)
        {
            It->MoveBudgetInches = It->MoveMaxInches;
            It->bHasShot = false;
            It->ForceNetUpdate();
        }
    }
}

static inline const TCHAR* CoverTypeToText(ECoverType C)
{
    switch (C) { case ECoverType::Low: return TEXT("in cover (low)");
    case ECoverType::High:return TEXT("in cover (high)");
    default:               return TEXT("no cover"); }
}

void AMatchGameMode::ResolveMoveToBudget(
    const AUnitBase* U,
    const FVector&   WantedDest,
    FVector&         OutFinalDest,
    float&           OutSpentTTIn,
    bool&            bOutClamped) const
{
    OutFinalDest = WantedDest;
    OutSpentTTIn = 0.f;
    bOutClamped  = false;
    if (!U) return;

    const FVector Start   = U->GetActorLocation();
    const FVector Delta   = WantedDest - Start;
    const float   DistCm  = Delta.Size();
    if (DistCm <= KINDA_SMALL_NUMBER) return;

    const float CmPerTTI  = CmPerTabletopInch();
    const float DistTTIn  = DistCm / CmPerTTI;
    const float SpendTTIn = FMath::Min(DistTTIn, U->MoveBudgetInches);

    OutSpentTTIn = SpendTTIn;

    // Scale along the direction to fit inside budget
    const float AllowedCm = SpendTTIn * CmPerTTI;
    if (AllowedCm + KINDA_SMALL_NUMBER < DistCm)
    {
        const FVector Dir = Delta / DistCm;
        OutFinalDest = Start + Dir * AllowedCm;
        bOutClamped = true;
    }

    // --- Networked debug draw (everyone sees it) ---
    if (AMatchGameState* S = GS())
    {
        const FColor Col = bOutClamped ? FColor::Yellow : FColor::Green;
        S->Multicast_DrawSphere(Start,      25.f, 16, Col,           6.f, 2.f);
        S->Multicast_DrawSphere(WantedDest, 25.f, 16, FColor::Silver,6.f, 2.f); // requested
        S->Multicast_DrawSphere(OutFinalDest,25.f,16, Col,           6.f, 2.f); // actual
        S->Multicast_DrawLine  (Start, OutFinalDest, Col, 6.f, 2.f);
        if (bOutClamped)
        {
            S->Multicast_DrawLine(OutFinalDest, WantedDest, FColor::Red, 6.f, 1.f);
        }
    }
}

bool AMatchGameMode::ValidateMove(AUnitBase* U, const FVector& Dest, float& OutSpentTabletopInches) const
{
    OutSpentTabletopInches = 0.f;
    if (!U) return false;

    const FVector Start = U->GetActorLocation();

    // Convert UE centimeters → tabletop inches using the global scale
    const float distCm   = FVector::Dist(Start, Dest);
    const float cmPerTTI = CmPerTabletopInch();
    const float distTTIn = distCm / cmPerTTI;
    OutSpentTabletopInches = distTTIn;

    const bool bAllowed = (distTTIn <= U->MoveBudgetInches);
    const FColor Col = bAllowed ? FColor::Green : FColor::Red;

    if (AMatchGameState* S = GS())
    {
        // world debug
        S->Multicast_DrawSphere(Start, 25.f, 16, Col, 10.f, 2.f);
        S->Multicast_DrawSphere(Dest,  25.f, 16, Col, 10.f, 2.f);
        S->Multicast_DrawLine  (Start, Dest, Col, 10.f, 2.f);

        // screen text
        const FString Msg = FString::Printf(
            TEXT("[MoveCheck] Unit=%s  Dist=%.0f cm (%.1f TT-in)  Budget=%.1f TT-in  Max=%.1f TT-in  Scale=%.2f UE-in/TT-in (%.2f cm/TT-in)  -> %s"),
            *U->GetName(), distCm, distTTIn, U->MoveBudgetInches, U->MoveMaxInches,
            TabletopToUnrealInchScale, cmPerTTI,
            bAllowed ? TEXT("ALLOW") : TEXT("DENY"));
        S->Multicast_ScreenMsg(Msg, Col, 10.f);
    }

    return bAllowed;
}

void AMatchGameMode::Handle_MoveUnit(AMatchPlayerController* PC, AUnitBase* Unit, const FVector& WantedDest)
{
    if (!HasAuthority() || !PC || !Unit) return;

    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle || S->TurnPhase != ETurnPhase::Move) return;
    if (PC->PlayerState != S->CurrentTurn) return;
    if (Unit->OwningPS != PC->PlayerState) return;

    // Resolve clamped movement
    FVector finalDest = WantedDest;
    float   spentTTIn = 0.f;
    bool    bClamped  = false;

    ResolveMoveToBudget(Unit, WantedDest, finalDest, spentTTIn, bClamped);

    // No budget left / no displacement
    if (spentTTIn <= KINDA_SMALL_NUMBER || finalDest.Equals(Unit->GetActorLocation(), 0.1f))
    {
        // Still tell client they’re out of budget to clear drag/select if you like:
        if (Unit->MoveBudgetInches <= KINDA_SMALL_NUMBER && IsValid(PC))
        {
            PC->Client_OnMoveDenied_OverBudget(Unit, /*requested*/0.f, Unit->MoveBudgetInches);
        }
        return;
    }

    // Spend and move
    Unit->MoveBudgetInches = FMath::Max(0.f, Unit->MoveBudgetInches - spentTTIn);
    Unit->SetActorLocation(finalDest);
    NotifyUnitTransformChanged(Unit);   // keep your auto-facing hook here if desired
    Unit->ForceNetUpdate();

    if (AMatchGameState* S2 = GS())
    {
        const FString Msg = FString::Printf(
            TEXT("[MoveApply] %s  Spent=%.1f TT-in  NewBudget=%.1f TT-in  %s"),
            *Unit->GetName(), spentTTIn, Unit->MoveBudgetInches,
            bClamped ? TEXT("(clamped)") : TEXT(""));
        S2->Multicast_ScreenMsg(Msg, bClamped ? FColor::Yellow : FColor::Green, 5.f);
    }

    // Tell the initiating client it succeeded (you can add a 'bClamped' param if you want)
    if (IsValid(PC))
    {
        PC->Client_OnUnitMoved(Unit, spentTTIn, Unit->MoveBudgetInches);
        // Or create a new RPC, e.g. Client_OnUnitMovedClamped(Unit, spentTTIn, Unit->MoveBudgetInches)
    }
}

bool AMatchGameMode::ValidateShoot(AUnitBase* A, AUnitBase* T) const
{
    if (!A || !T || A==T) return false;
    if (A->ModelsCurrent <= 0 || T->ModelsCurrent <= 0) return false;
    if (A->OwningPS == T->OwningPS) return false;
    if (A->bHasShot) return false;

    const float distTT = FVector::Dist(A->GetActorLocation(), T->GetActorLocation()) / CmPerTabletopInch();
    const float rngTT  = A->GetWeaponRange(); // stored as tabletop inches
    return (rngTT > 0.f) && (distTT <= rngTT);
}

void AMatchGameMode::Handle_SelectTarget(AMatchPlayerController* PC, AUnitBase* Attacker, AUnitBase* Target)
{
    if (!HasAuthority() || !PC || !Attacker) return;

    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle || S->TurnPhase != ETurnPhase::Shoot) return;

    // Only current player, and they can only select a target for their own attacker
    if (PC->PlayerState != S->CurrentTurn) return;
    if (Attacker->OwningPS != PC->PlayerState) return;

    // Null/invalid target -> cancel preview
    if (!Target || !ValidateShoot(Attacker, Target))
    {
        if (S->Preview.Attacker == Attacker)
        {
            S->Preview.Attacker = nullptr;
            S->Preview.Target   = nullptr;
            S->Preview.Phase    = S->TurnPhase;

            S->ActionPreview.HitMod   = 0;
            S->ActionPreview.SaveMod  = 0;
            S->ActionPreview.Cover    = ECoverType::None;

            // Optional: revert to nearest enemy when canceling
            Attacker->FaceNearestEnemyInstant();
        }
        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
        return;
    }

    // Valid selection -> set preview and face the explicit target
    S->Preview.Attacker = Attacker;
    S->Preview.Target   = Target;
    S->Preview.Phase    = S->TurnPhase;

    S->ActionPreview.Attacker = Attacker;
    S->ActionPreview.Target   = Target;
    S->ActionPreview.Phase    = S->TurnPhase;

    int32 HitMod = 0, SaveMod = 0;
    ECoverType Cover = ECoverType::None;
    QueryCover(Attacker, Target, HitMod, SaveMod, Cover);
    S->ActionPreview.HitMod  = static_cast<int8>(HitMod);
    S->ActionPreview.SaveMod = static_cast<int8>(SaveMod);
    S->ActionPreview.Cover   = Cover;

    // Face *the target* (not the nearest enemy)
    Attacker->FaceActorInstant(Target);

    S->OnDeploymentChanged.Broadcast();
    S->ForceNetUpdate();
}
void AMatchGameMode::Handle_ConfirmShoot(AMatchPlayerController* PC, AUnitBase* Attacker, AUnitBase* Target)
{
    if (!HasAuthority() || !PC || !Attacker || !Target) return;

    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle || S->TurnPhase != ETurnPhase::Shoot) return;
    if (PC->PlayerState != S->CurrentTurn) return;
    if (Attacker->OwningPS != PC->PlayerState) return;
    if (!ValidateShoot(Attacker, Target)) return;

    // -------- resolution (unchanged) --------
    const int32 Models    = FMath::Max(0, Attacker->ModelsCurrent);
    const int32 AttacksPM = FMath::Max(0, Attacker->GetAttacks());
    const int32 TotalAtk  = Models * AttacksPM;

    int32 HitMod = 0, SaveMod = 0;
    ECoverType Cover = ECoverType::None;
    QueryCover(Attacker, Target, HitMod, SaveMod, Cover);

    int32 HitNeed = FMath::Clamp(Attacker->WeaponSkillToHitRep + (HitMod * -1), 2, 6);

    const int32 Sval = Attacker->WeaponStrengthRep;
    const int32 Dmg  = FMath::Max(1, Attacker->WeaponDamageRep);
    const int32 AP   = FMath::Max(0, Attacker->WeaponAPRep);

    const int32 Tval     = Target->GetToughness();
    const int32 SaveBase = Target->GetSave();

    int32 SaveNeed = ModifiedSaveNeed(SaveBase, AP);
    SaveNeed = FMath::Clamp(SaveNeed - SaveMod, 2, 7);
    const bool bHasSave = (SaveNeed <= 6);

    int32 hits = 0;
    for (int i=0; i<TotalAtk; ++i)
        if (D6() >= HitNeed) ++hits;

    const int32 WoundNeed = ToWoundTarget(Sval, Tval);

    int32 wounds = 0;
    for (int i=0; i<hits; ++i)
        if (D6() >= WoundNeed) ++wounds;

    int32 unsaved = 0;
    if (bHasSave)
    {
        for (int i=0; i<wounds; ++i)
            if (D6() < SaveNeed) ++unsaved; // fail = unsaved
    }
    else
    {
        unsaved = wounds;
    }

    const int32 totalDamage = unsaved * Dmg;

    // mark shooter as having shot right away
    Attacker->bHasShot = true;
    Attacker->ForceNetUpdate();

    // aesthetic rotate (or use your VisualFaceActor if preferred)
    Attacker->FaceActorInstant(Target);

    // LOS: count visible target models
    const int32 TargetModels    = FMath::Max(0, Target->ModelsCurrent);
    const int32 VisibleModels   = CountVisibleTargetModels(Attacker, Target);
    const int32 WoundsPerModel  = Target->GetWoundsPerModel();
    const int32 MaxDamageByLOS  = VisibleModels * WoundsPerModel;

    // Clamp the damage so we can't kill more models than are visible
    const int32 ClampedDamage = FMath::Min(totalDamage, MaxDamageByLOS);

    // World-space debug location
    const FVector L0  = Attacker->GetActorLocation();
    const FVector L1  = Target->GetActorLocation();
    const FVector Mid = (L0 + L1) * 0.5f + FVector(0,0,150.f);

    // Build the pre-message; we'll append actual models killed after damage lands
    const TCHAR* CoverTxt = CoverTypeToText(Cover);
    const int32  savesMade = bHasSave ? (wounds - unsaved) : 0;

    const FString Msg = FString::Printf(
        TEXT("Hit: %d/%d\nWound: %d/%d\nSave: %d/%d (%s)\nLOS: %d/%d models visible (cap %d dmg)\nDamage rolled: %d -> applied: %d on impact"),
        hits, TotalAtk,
        wounds, hits,
        savesMade, wounds, CoverTxt,
        VisibleModels, TargetModels, MaxDamageByLOS,
        totalDamage, ClampedDamage);

    // 1) pick a single delay and use it everywhere
    const float ImpactDelay = Attacker->ImpactDelaySeconds;

    // 2) multicast VFX/SFX with the SAME delay (be sure the function signature takes the float)
    Attacker->Multicast_PlayMuzzleAndImpactFX_AllModels(Target, ImpactDelay);

    // 3) schedule damage and debug at impact time
    FTimerDelegate Del;
    Del.BindUFunction(this, FName("ApplyDelayedDamageAndReport"),
                      Attacker, Target, ClampedDamage, Mid, Msg);

    FTimerHandle Tmp;
    GetWorld()->GetTimerManager().SetTimer(Tmp, Del, FMath::Max(0.f, ImpactDelay), false);

    // clear preview now (or delay if you prefer)
    if (S->Preview.Attacker == Attacker)
    {
        S->Preview.Attacker = nullptr;
        S->Preview.Target   = nullptr;
        S->Preview.Phase    = S->TurnPhase;
    }

    S->OnDeploymentChanged.Broadcast();
    S->ForceNetUpdate();
}

int32 AMatchGameMode::CountVisibleTargetModels(const AUnitBase* Attacker, const AUnitBase* Target) const
{
    if (!Attacker || !Target) return 0;

    UWorld* World = GetWorld();
    if (!World) return 0;

    int32 Visible = 0;

    for (int32 j = 0; j < Target->ModelMeshes.Num(); ++j)
    {
        UStaticMeshComponent* TC = Target->ModelMeshes[j];
        if (!IsValid(TC)) continue;

        // Aim at a stable point on the model (socket or small offset)
        FVector ModelPoint;
        if (TC->DoesSocketExist(Target->ImpactSocketName))
        {
            ModelPoint = TC->GetSocketTransform(Target->ImpactSocketName, RTS_World).GetLocation();
        }
        else
        {
            ModelPoint = TC->GetComponentTransform().TransformPosition(Target->ImpactOffsetLocal);
        }

        // Pick the closest shooter model to this target model as origin
        const int32 ShooterIdx = Attacker->FindBestShooterModelIndex(ModelPoint);
        const FVector From = Attacker->GetMuzzleTransform(ShooterIdx).GetLocation();
        const FVector To   = ModelPoint;

        FHitResult Hit;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(UnitLOS), /*bTraceComplex*/ true);
        Params.AddIgnoredActor(const_cast<AUnitBase*>(Attacker));
        Params.AddIgnoredActor(const_cast<AUnitBase*>(Target));

        const bool bHit = World->LineTraceSingleByChannel(
            Hit, From, To, ECollisionChannel::ECC_GameTraceChannel3 /*LOS*/, Params);

        // Visible if NOTHING blocks on LOS channel
        if (!bHit)
        {
            ++Visible;
        }
    }
    return Visible;
}

static void DrawCoverTrace(UWorld* World,
                           const FVector& A, const FVector& B,
                           const FColor& Col, float Thk, float Time)
{
    if (!World) return;
    if (AMatchGameState* S = World->GetGameState<AMatchGameState>())
    {
        S->Multicast_DrawLine(A, B, Col, Time, Thk);
    }
}

static void DrawCoverNote(UWorld* World, const FVector& At, const FString& Msg,
                          const FColor& Col, float Time)
{
    if (!World) return;
    if (AMatchGameState* S = World->GetGameState<AMatchGameState>())
    {
        S->Multicast_DrawWorldText(At, Msg, Col, Time, 1.0f);
    }
}

bool AMatchGameMode::ComputeCoverBetween(const FVector& From, const FVector& To, ECoverType& OutType) const
{
    OutType = ECoverType::None;

    UWorld* World = GetWorld();
    if (!World) return false;

    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverTrace), /*bTraceComplex*/ false);
    Params.bReturnPhysicalMaterial = false;

    const bool bHit = World->LineTraceSingleByChannel(Hit, From, To, CoverTraceChannel, Params);

    // Draw basic result (center-to-center), color refined later by caller if needed
    if (bDebugCoverTraces)
        DrawCoverTrace(World, From, To, bHit ? FColor::Yellow : FColor::Red, 1.5f, 2.f);

    if (!bHit) return false;

    if (ACoverVolume* CV = Cast<ACoverVolume>(Hit.GetActor()))
    {
        OutType = CV->CoverType;
        return true;
    }
    return false;
}

bool AMatchGameMode::QueryCover(AUnitBase* A, AUnitBase* T,
                                int32& OutHitMod, int32& OutSaveMod, ECoverType& OutType) const
{
    OutHitMod  = 0;
    OutSaveMod = 0;
    OutType    = ECoverType::None;
    if (!A || !T) return false;

    UWorld* World = GetWorld();
    if (!World) return false;

    const float ProxCm = CoverProximityInches * CmPerTabletopInch(); // consistent global scale

    // ignore the two units
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverTraceFull), false);
    Params.AddIgnoredActor(A);
    Params.AddIgnoredActor(T);

    auto TraceOne = [&](const FVector& From, const FVector& TargetPoint)->ECoverType
    {
        FHitResult Hit;
        const bool bHit = World->LineTraceSingleByChannel(Hit, From, TargetPoint, CoverTraceChannel, Params);

        if (!bHit)
        {
            if (bDebugCoverTraces) DrawCoverTrace(World, From, TargetPoint, FColor::Red, 0.75f, 1.5f);
            return ECoverType::None;
        }

        // We hit *something* on the cover channel; only count ACoverVolume, and only if near the target
        ACoverVolume* CV = Cast<ACoverVolume>(Hit.GetActor());
        if (!CV)
        {
            if (bDebugCoverTraces) DrawCoverTrace(World, From, TargetPoint, FColor::Purple, 0.75f, 1.5f); // wrong object
            return ECoverType::None;
        }

        const float dCm = FVector::Dist(Hit.ImpactPoint, TargetPoint);
        const bool bProx = (dCm <= ProxCm);

        if (bDebugCoverTraces)
        {
            const FColor C = bProx ? FColor::Green : FColor::Orange;
            DrawCoverTrace(World, From, TargetPoint, C, 1.75f, 2.0f);
            const FVector Mid = (From + TargetPoint) * 0.5f + FVector(0,0,25.f);
            DrawCoverNote(World, Mid,
                FString::Printf(TEXT("%s cover (%s)\n%.1f cm from target"),
                    CV->CoverType==ECoverType::High?TEXT("High"):TEXT("Low"),
                    bProx?TEXT("valid"):TEXT("too far"),
                    dCm),
                C, 1.5f);
        }

        return bProx ? CV->CoverType : ECoverType::None;
    };

    // 1) Quick center-to-center (fast path)
    {
        const FVector From = A->GetActorLocation();
        const FVector To   = T->GetActorLocation();
        if (ECoverType C = TraceOne(From, To); C != ECoverType::None)
        {
            OutType    = C;
            OutSaveMod = 1;                 // +1 to save
            if (C == ECoverType::High) OutHitMod = -1; // -1 to hit (we keep your sign convention)
            return true;
        }
    }

    // 2) Per-model sampling (early out on first High; otherwise accept Low if found)
    TArray<FVector> FromPts, ToPts;
    A->GetModelWorldLocations(FromPts);
    T->GetModelWorldLocations(ToPts);

    const int32 MaxFrom = FMath::Min(MaxCoverSamplesPerUnit, FromPts.Num());
    const int32 MaxTo   = FMath::Min(MaxCoverSamplesPerUnit, ToPts.Num());

    bool bAnyLow  = false;
    bool bAnyHigh = false;

    for (int32 i=0; i<MaxFrom; ++i)
    {
        for (int32 j=0; j<MaxTo; ++j)
        {
            const ECoverType C = TraceOne(FromPts[i], ToPts[j]);
            if (C == ECoverType::High) { bAnyHigh = true; goto DONE; }
            if (C == ECoverType::Low)  { bAnyLow  = true; }
        }
    }

DONE:
    if (bAnyHigh || bAnyLow)
    {
        OutType    = bAnyHigh ? ECoverType::High : ECoverType::Low;
        OutSaveMod = 1;
        if (bAnyHigh) OutHitMod = -1;
        return true;
    }

    return false;
}

void AMatchGameMode::Handle_CancelPreview(AMatchPlayerController* PC, AUnitBase* Attacker)
{
    if (!HasAuthority() || !PC || !Attacker) return;

    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle) return;

    // Only the active player, and only for their own unit
    if (PC->PlayerState != S->CurrentTurn) return;
    if (Attacker->OwningPS != PC->PlayerState) return;

    // Only clear if the current preview is for this attacker
    if (S->Preview.Attacker == Attacker)
    {
        S->Preview.Attacker = nullptr;
        S->Preview.Target   = nullptr;
        S->Preview.Phase    = S->TurnPhase; // keep phase consistent

        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
    }
}

void AMatchGameMode::BeginPlay()
{
    Super::BeginPlay();
}

void AMatchGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    
    if (AMatchGameState* S = GS())
    {
        if (!S->P1)      S->P1 = NewPlayer->PlayerState;
        else if (!S->P2) S->P2 = NewPlayer->PlayerState;

        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
    }
    
    FinalizePlayerJoin(NewPlayer); // leave the heavy init here

}

void AMatchGameMode::ResetUnitRoundStateFor(APlayerState* TurnOwner)
{
    if (!HasAuthority() || !TurnOwner) return;

    UWorld* World = GetWorld();
    for (TActorIterator<AUnitBase> It(World); It; ++It)
    {
        if (AUnitBase* U = *It)
        {
            if (U->OwningPS == TurnOwner)
            {
                // Movement
                U->MoveBudgetInches = U->MoveMaxInches;
                // Actions
                U->bHasShot         = false;

                U->ForceNetUpdate();
            }
        }
    }
}

APlayerState* AMatchGameMode::OtherPlayer(APlayerState* PS) const
{
    if (const AMatchGameState* S = GS())
        return (PS == S->P1) ? S->P2 : S->P1;
    return nullptr;
}

int32 AMatchGameMode::FindIdx(TArray<FUnitCount>& Arr, FName Unit)
{
    for (int32 i=0;i<Arr.Num();++i) if (Arr[i].UnitId == Unit) return i;
    return INDEX_NONE;
}

bool AMatchGameMode::CanDeployAt(APlayerController* PC, const FVector& Location) const
{
    if (!PC) return false;

    const AMatchGameState* S = GetGameState<AMatchGameState>();
    if (!S || S->Phase != EMatchPhase::Deployment) return false;

    // (Optional) enforce alternating: it must be this PC’s turn
    if (S->CurrentDeployer && PC->PlayerState != S->CurrentDeployer)
        return false;

    // Team gate
    const ATabletopPlayerState* TPS = PC->GetPlayerState<ATabletopPlayerState>();
    const int32 Team = TPS ? TPS->TeamNum : 0;
    if (Team <= 0) return false;

    return ADeploymentZone::IsLocationAllowedForTeam(GetWorld(), Team, Location);
}


void AMatchGameMode::CopyRostersFromPlayerStates()
{
    if (AMatchGameState* S = GS())
    {
        S->P1Remaining.Empty();
        S->P2Remaining.Empty();

        if (auto* P1PS = Cast<ATabletopPlayerState>(S->P1))
            S->P1Remaining = P1PS->Roster;

        if (auto* P2PS = Cast<ATabletopPlayerState>(S->P2))
            S->P2Remaining = P2PS->Roster;
    }
}

bool AMatchGameMode::AnyRemainingFor(APlayerState* PS) const
{
    if (const AMatchGameState* S = GS())
    {
        const bool bIsP1 = (PS == S->P1);
        const TArray<FUnitCount>& R = bIsP1 ? S->P1Remaining : S->P2Remaining;
        for (const FUnitCount& E : R) if (E.Count > 0) return true;
    }
    return false;
}

bool AMatchGameMode::DecrementOne(APlayerState* PS, FName UnitId)
{
    if (AMatchGameState* S = GS())
    {
        const bool bIsP1 = (PS == S->P1);
        TArray<FUnitCount>& R = bIsP1 ? S->P1Remaining : S->P2Remaining;
        if (int32 Idx = FindIdx(R, UnitId); Idx != INDEX_NONE && R[Idx].Count > 0)
        {
            R[Idx].Count -= 1;
            if (R[Idx].Count <= 0) R.RemoveAt(Idx);
            return true;
        }
    }
    return false;
}

UDataTable* AMatchGameMode::UnitsForFaction(EFaction Faction) const
{
    const AMatchGameState* S = GS();
    if (!S || !FactionsTable) return nullptr;
    for (const auto& Pair : FactionsTable->GetRowMap())
    {
        if (const FFactionRow* FR = reinterpret_cast<const FFactionRow*>(Pair.Value))
            if (FR->Faction == Faction) return FR->UnitsTable;
    }
    return nullptr;
}

TSubclassOf<AActor> AMatchGameMode::UnitClassFor(APlayerState* ForPS, FName UnitId) const
{
    const TSubclassOf<AActor> DefaultClass = AActor::StaticClass();

    const ATabletopPlayerState* TPS = ForPS ? Cast<ATabletopPlayerState>(ForPS) : nullptr;
    if (!TPS) return DefaultClass;

    UDataTable* Units = UnitsForFaction(TPS->SelectedFaction);
    if (!Units) return DefaultClass;

    if (const FUnitRow* Row = Units->FindRow<FUnitRow>(UnitId, TEXT("UnitClassFor")))
    {
        return Row->UnitActorClass ? Row->UnitActorClass : DefaultClass;
    }
    return DefaultClass;
}

void AMatchGameMode::HandleRequestDeploy(APlayerController* PC, FName UnitId, const FTransform& Where)
{
    if (!HasAuthority() || !PC) return;

    if (AMatchGameState* S = GS())
    {
        // Must be your turn to deploy
        if (PC->PlayerState.Get() != S->CurrentDeployer) return;

        if (!CanDeployAt(PC, Where.GetLocation()))
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy denied: outside deployment zone or not your turn."));
            return;
        }

        // Validate & decrement from remaining pool
        if (!DecrementOne(PC->PlayerState.Get(), UnitId))
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy denied: unit %s not available in remaining roster."), *UnitId.ToString());
            return;
        }

        // ---- Resolve the unit row from the player's faction table ----
        const ATabletopPlayerState* TPS = Cast<ATabletopPlayerState>(PC->PlayerState.Get());
        if (!TPS)
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy denied: PlayerState is not ATabletopPlayerState."));
            return;
        }

        UDataTable* UnitsDT = UnitsForFaction(TPS->SelectedFaction);
        if (!UnitsDT)
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy denied: No units DataTable for faction %d."), (int32)TPS->SelectedFaction);
            return;
        }

        const FUnitRow* Row = UnitsDT->FindRow<FUnitRow>(UnitId, TEXT("HandleRequestDeploy"));
        if (!Row)
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy denied: UnitId %s not found in faction table."), *UnitId.ToString());
            return;
        }

        // ---- Spawn the actor (class already resolved by faction/unit) ----
        TSubclassOf<AActor> SpawnClass = UnitClassFor(PC->PlayerState.Get(), UnitId);
        if (!*SpawnClass)
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy warning: UnitClassFor returned null for %s; defaulting to AActor."), *UnitId.ToString());
            SpawnClass = AActor::StaticClass();
        }

        FActorSpawnParameters Params;
        Params.Owner = PC;
        AActor* Spawned = GetWorld()->SpawnActor<AActor>(SpawnClass, Where, Params);
        if (!Spawned)
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy failed: spawn returned null for %s."), *UnitId.ToString());
            return;
        }

        // ---- Initialize runtime state if it's a UnitBase ----
        if (AUnitBase* UB = Cast<AUnitBase>(Spawned))
        {
            UB->Server_InitFromRow(PC->PlayerState.Get(), *Row, Row->DefaultWeaponIndex);
            UB->FaceNearestEnemyInstant();
            
            NotifyUnitTransformChanged(UB);
            // Use DefaultWeaponIndex from the row (validated inside Server_InitFromRow if needed)
          
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy note: Spawned class %s is not AUnitBase for %s; skipping runtime init."),
                   *SpawnClass->GetName(), *UnitId.ToString());
        }

        // ---- Alternate or keep turn based on remaining pools ----
        APlayerState* Other = (PC->PlayerState.Get() == S->P1) ? S->P2 : S->P1;
        const bool bSelfLeft  = AnyRemainingFor(PC->PlayerState.Get());
        const bool bOtherLeft = AnyRemainingFor(Other);

        if (!bSelfLeft && !bOtherLeft)
        {
            FinishDeployment();
            return;
        }

        S->CurrentDeployer = bOtherLeft ? Other : PC->PlayerState.Get();

        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
    }
}

void AMatchGameMode::FinishDeployment()
{
    if (AMatchGameState* S = GS())
    {
        S->bDeploymentComplete = true;
        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
    }
}

void AMatchGameMode::HandleStartBattle(APlayerController* PC)
{
    if (!HasAuthority() || !PC) return;
    if (AMatchGameState* S = GS())
    {
        if (PC->PlayerState != S->P1) return; // host-only
        if (!S->bDeploymentComplete)  return;

        S->Phase = EMatchPhase::Battle;
        S->CurrentRound= 1;
        S->MaxRounds   = 5;
        S->TurnInRound = 0;
        S->TurnPhase   = ETurnPhase::Move;

        // TODO Random??
        S->CurrentTurn = S->CurrentDeployer;
        if (AMatchGameMode* GM = GetWorld()->GetAuthGameMode<AMatchGameMode>())
            GM->ResetUnitRoundStateFor(S->CurrentTurn);
        
        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();

        // Kick both clients to refresh (optional)
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
            if (AMatchPlayerController* MPC = Cast<AMatchPlayerController>(*It))
                MPC->Client_KickPhaseRefresh();
    }
}

void AMatchGameMode::HandleEndPhase(APlayerController* PC)
{
    if (!HasAuthority() || !PC) return;
    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle) return;
    if (PC->PlayerState != S->CurrentTurn) return; // only active player may advance

    auto Broadcast = [&]()
    {
        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
    };

    // Move -> Shoot -> (end turn)
    if (S->TurnPhase == ETurnPhase::Move)
    {
        S->TurnPhase = ETurnPhase::Shoot;
        Broadcast();
        return;
    }

    // End of Shoot -> end of this player's turn
    // If first player in round just ended, switch to other player.
    if (S->TurnInRound == 0)
    {
        S->TurnInRound = 1;
        S->CurrentTurn = OtherPlayer(S->CurrentTurn);
        S->TurnPhase   = ETurnPhase::Move;
        ResetUnitRoundStateFor(S->CurrentTurn);
        Broadcast();
        return;
    }

    // Second player ended → next round, or end game
    S->CurrentRound = FMath::Clamp<uint8>(S->CurrentRound + 1, 1, S->MaxRounds);
    S->TurnInRound  = 0;

    ScoreObjectivesForRound();
    S->OnDeploymentChanged.Broadcast();
    S->ForceNetUpdate();

    if (S->CurrentRound > S->MaxRounds)
    {
        S->Phase = EMatchPhase::EndGame;
        Broadcast();
        return;
    }

    // Next round starts with the other player than who just ended
    S->CurrentTurn = OtherPlayer(S->CurrentTurn);
    S->TurnPhase   = ETurnPhase::Move;
    ResetUnitRoundStateFor(S->CurrentTurn);
    Broadcast();
}

void AMatchGameMode::ScoreObjectivesForRound()
{
    if (!HasAuthority()) return;
    AMatchGameState* S = GS();
    if (!S) return;

    int32 P1Delta = 0, P2Delta = 0;

    // If you cached objectives on GS, iterate those; otherwise iterate world
    TArray<AObjectiveMarker*> Objectives;
    for (TActorIterator<AObjectiveMarker> It(GetWorld()); It; ++It)
        Objectives.Add(*It);

    for (AObjectiveMarker* Obj : Objectives)
    {
        if (!Obj) continue;

        // Recompute from current unit positions (server)
        Obj->RecalculateControl();

        APlayerState* Controller = Obj->GetControllingPlayerState();
        if (!Controller) continue;

        if (Controller == S->P1) P1Delta += Obj->PointsPerRound;
        else if (Controller == S->P2) P2Delta += Obj->PointsPerRound;
    }

    S->ScoreP1 += P1Delta;
    S->ScoreP2 += P2Delta;
}

void AMatchGameMode::NotifyUnitTransformChanged(AUnitBase* Changed)
{
    if (!Changed) return;
    const float AffectRadius = 4000.f;
    const FVector C = Changed->GetActorLocation();

    for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
    {
        AUnitBase* U = *It;
        if (!U) continue;
        if (!U->IsEnemy(Changed)) continue;
        if (FVector::DistSquared(C, U->GetActorLocation()) <= AffectRadius * AffectRadius)
        {
            U->FaceNearestEnemyInstant();
        }
    }
}

void AMatchGameMode::ApplyDelayedDamageAndReport(AUnitBase* Attacker, AUnitBase* Target, int32 TotalDamage, FVector DebugMid, FString DebugMsg)
{
    if (!HasAuthority()) return;
    AMatchGameState* S = GS();
    if (!S) return;

    if (IsValid(Target) && TotalDamage > 0)
    {
        Target->ApplyDamage_Server(TotalDamage);
    }

    // show the text at impact time (optional—move earlier if you prefer instant feedback)
    S->Multicast_DrawShotDebug(DebugMid, DebugMsg, FColor::Black, 8.f);

    S->OnDeploymentChanged.Broadcast();
    S->ForceNetUpdate();
}

void AMatchGameMode::FinalizePlayerJoin(APlayerController* PC)
{
    if (!PC) return;
    if (AMatchGameState* S = GS())
    {
        // Make sure P1/P2 are set (idempotent)
        if (!S->P1) S->P1 = PC->PlayerState;
        else if (!S->P2 && S->P1 != PC->PlayerState) S->P2 = PC->PlayerState;

        // One-time init only when both present and not yet initialized
        if (S->P1 && S->P2 && !S->bTeamsAndTurnsInitialized)
        {
            // Copy rosters once
            CopyRostersFromPlayerStates();

            // Randomize or keep your existing function:
            const bool bP1First = FMath::RandBool();
            S->CurrentDeployer = bP1First ? S->P1 : S->P2;

            if (ATabletopPlayerState* P1PS = Cast<ATabletopPlayerState>(S->P1))
                P1PS->TeamNum = bP1First ? 1 : 2;
            if (ATabletopPlayerState* P2PS = Cast<ATabletopPlayerState>(S->P2))
                P2PS->TeamNum = bP1First ? 2 : 1;

            S->Phase = EMatchPhase::Deployment;
            S->bTeamsAndTurnsInitialized = true;

            if (ATabletopPlayerState* P1PS = Cast<ATabletopPlayerState>(S->P1)) P1PS->ForceNetUpdate();
            if (ATabletopPlayerState* P2PS = Cast<ATabletopPlayerState>(S->P2)) P2PS->ForceNetUpdate();
            S->ForceNetUpdate();
        }

        // Let clients refresh UI when any of the replicated props land
        S->OnDeploymentChanged.Broadcast();
    }

    if (AMatchPlayerController* MPC = Cast<AMatchPlayerController>(PC))
    {
        MPC->Client_KickUIRefresh();
    }
}

void AMatchGameMode::TallyObjectives_EndOfRound()
{
    AMatchGameState* GS = GetGameState<AMatchGameState>();
    if (!GS) return;

    int32 RoundP1 = 0, RoundP2 = 0;

    for (AObjectiveMarker* Obj : GS->Objectives)
    {
        if (!Obj) continue;

        int32 OC_P1 = 0, OC_P2 = 0;

        for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
        {
            AUnitBase* U = *It;
            if (!U || !U->OwningPS) continue; // use whatever you already store for ownership

            const int32 OC = U->GetObjectiveControlAt(Obj);
            if (OC <= 0) continue;

            if (U->OwningPS == GS->P1) OC_P1 += OC;
            else if (U->OwningPS == GS->P2) OC_P2 += OC;
        }

        if (OC_P1 > OC_P2) RoundP1 += Obj->PointsPerRound;
        else if (OC_P2 > OC_P1) RoundP2 += Obj->PointsPerRound;

#if !(UE_BUILD_SHIPPING)
        UE_LOG(LogTemp, Log, TEXT("[OBJ %s] P1=%d  P2=%d  -> %s"),
            *Obj->ObjectiveId.ToString(), OC_P1, OC_P2,
            OC_P1>OC_P2?TEXT("P1"):OC_P2>OC_P1?TEXT("P2"):TEXT("Contested/None"));
#endif
    }

    GS->ScoreP1 += RoundP1;
    GS->ScoreP2 += RoundP2;

    // You already broadcast UI updates; reuse your existing multicast
    GS->OnDeploymentChanged.Broadcast();
}