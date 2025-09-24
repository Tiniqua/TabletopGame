// MatchGameMode.cpp
#include "MatchGameMode.h"

#include "EngineUtils.h"
#include "SetupGamemode.h"
#include "Components/LineBatchComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

// Your existing includes
#include "Tabletop/Actors/CoverVolume.h"
#include "Tabletop/Actors/DeploymentZone.h"
#include "Tabletop/Actors/NetDebugTextActor.h"
#include "Tabletop/Actors/ObjectiveMarker.h"
#include "Tabletop/Actors/UnitBase.h"
#include "Tabletop/Characters/TabletopCharacter.h"
#include "Tabletop/Controllers/MatchPlayerController.h"
#include "Tabletop/PlayerStates/TabletopPlayerState.h"

// Data-driven combat system additions
#include "Components/BoxComponent.h"
#include "Tabletop/AbiltyEventSubsystem.h"
#include "Tabletop/ArmyData.h"                  // Faction -> Units DT lookup
#include "Tabletop/WeaponKeywords.h"
#include "Tabletop/CombatEffects.h"
#include "Tabletop/CoverPresetData.h"
#include "Tabletop/KeywordProcessor.h"
#include "Tabletop/UnitActionResourceComponent.h"
#include "Tabletop/WeaponKeywordHelpers.h"
#include "Tabletop/Actors/UnitAction.h"


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

static void GetTargetModelHitPoints(const AUnitBase* Target, TArray<FVector>& Out)
{
	Out.Reset();
	for (UStaticMeshComponent* TC : Target->ModelMeshes)
	{
		if (!IsValid(TC)) continue;
		if (TC->DoesSocketExist(Target->ImpactSocketName))
			Out.Add(TC->GetSocketTransform(Target->ImpactSocketName, RTS_World).GetLocation());
		else
			Out.Add(TC->GetComponentTransform().TransformPosition(Target->ImpactOffsetLocal));
	}
	if (Out.Num() == 0) Out.Add(Target->GetActorLocation());
}

static void CollectCoverVolumes_AllLevels(UWorld* W, TArray<ACoverVolume*>& Out, bool bLog = false)
{
	Out.Reset();
	if (!W) return;

	// Persistent level
	if (ULevel* PL = W->PersistentLevel)
	{
		for (AActor* A : PL->Actors)
			if (auto* CV = Cast<ACoverVolume>(A)) Out.Add(CV);
	}

	// Streaming levels that are currently loaded (and ideally visible)
	for (ULevel* L : W->GetLevels())
	{
		if (!L || L == W->PersistentLevel) continue;
		const bool bLoaded  = (L->bIsVisible || L->IsPersistentLevel()); // UE5 visibility is enough for actors to be spawned
		if (!bLoaded) continue;

		for (AActor* A : L->Actors)
			if (auto* CV = Cast<ACoverVolume>(A)) Out.Add(CV);
	}

	if (bLog)
	{
		UE_LOG(LogCoverNet, Display, TEXT("[GM] World=%s Levels=%d, Found %d ACoverVolume"),
			*GetNameSafe(W), W->GetLevels().Num(), Out.Num());
		int32 i=0;
		for (ACoverVolume* CV : Out)
		{
			UE_LOG(LogCoverNet, Verbose, TEXT("  #%d: %s Class=%s Level=%s"),
				++i, *GetNameSafe(CV), *GetNameSafe(CV->GetClass()), *GetNameSafe(CV->GetLevel()));
		}
	}
}

static void BuildImpactSites_Server(const AUnitBase* Attacker, const AUnitBase* Target, TArray<FImpactSite>& OutSites)
{
    OutSites.Reset();
    if (!Attacker || !Target) return;

    // Pre-read muzzle positions once for better "incoming" directions
    TArray<FVector> Muzzles;
    for (int32 i = 0; i < Attacker->ModelMeshes.Num(); ++i)
        if (UStaticMeshComponent* C = Attacker->ModelMeshes[i])
            Muzzles.Add(Attacker->GetMuzzleTransform(i).GetLocation());

    auto NearestMuzzleTo = [&Muzzles](const FVector& P)->FVector
    {
        int32 BestIdx = 0; float BestD2 = TNumericLimits<float>::Max();
        for (int32 i=0;i<Muzzles.Num();++i)
        {
            const float D2 = FVector::DistSquared(Muzzles[i], P);
            if (D2 < BestD2) { BestD2 = D2; BestIdx = i; }
        }
        return (Muzzles.IsValidIndex(BestIdx) ? Muzzles[BestIdx] : P + FVector(100,0,0));
    };

    for (int32 j = 0; j < Target->ModelMeshes.Num(); ++j)
    {
        UStaticMeshComponent* TC = Target->ModelMeshes[j];
        if (!IsValid(TC)) continue;

        FVector ImpactLoc;
        if (TC->DoesSocketExist(Target->ImpactSocketName))
            ImpactLoc = TC->GetSocketTransform(Target->ImpactSocketName, ERelativeTransformSpace::RTS_World).GetLocation();
        else
            ImpactLoc = TC->GetComponentTransform().TransformPosition(Target->ImpactOffsetLocal);

        const FVector From = NearestMuzzleTo(ImpactLoc);
        FImpactSite S;
        S.Loc = ImpactLoc;
        S.Rot = (From - ImpactLoc).Rotation(); // incoming (target -> shooter)
        OutSites.Add(S);
    }
}

static bool IsCoverNearActor(const ACoverVolume* CV, const AActor* A, float MaxCm)
{
	if (!CV || !CV->Box || !A) return false;

	const FVector AttLoc = A->GetActorLocation();
	FVector OnSurface;
	const float d = CV->Box->GetClosestPointOnCollision(AttLoc, OnSurface, NAME_None);

	// GetClosestPointOnCollision returns distance in cm (or -1 if invalid); treat <= MaxCm as "near"
	return (d >= 0.f) && (d <= MaxCm + KINDA_SMALL_NUMBER);
}

static ADeploymentZone* NearestZoneFor(const FVector& P, const TArray<ADeploymentZone*>& Zones)
{
    ADeploymentZone* Best = nullptr;
    float BestD2 = TNumericLimits<float>::Max();
    for (ADeploymentZone* Z : Zones)
    {
        if (!Z || !Z->bEnabled) continue;
        const float D2 = FVector::DistSquared(P, Z->GetActorLocation());
        if (D2 < BestD2) { BestD2 = D2; Best = Z; }
    }
    return Best;
}

static void GatherZonesByTeam(UWorld* W, TArray<ADeploymentZone*>& OutTeam1, TArray<ADeploymentZone*>& OutTeam2)
{
    OutTeam1.Reset(); OutTeam2.Reset();
    for (TActorIterator<ADeploymentZone> It(W); It; ++It)
    {
        ADeploymentZone* Z = *It;
        if (!Z || !Z->bEnabled) continue;
        if (Z->CurrentOwner == EDeployOwner::Team1) OutTeam1.Add(Z);
        else if (Z->CurrentOwner == EDeployOwner::Team2) OutTeam2.Add(Z);
        // We ignore “Either” for assignment to keep the mapping unambiguous.
    }
}

static FName PickRowForFaction(const UDataTable* DT, EFaction Faction, bool bPreferLow)
{
	if (!DT || Faction == EFaction::None) return NAME_None;

	TArray<FName> Both, LowOnly;

	// Iterate row names, fetch typed row via FindRow (safe on all versions)
	for (const auto& Pair : DT->GetRowMap())
	{
		const FName RowName = Pair.Key;

		const FCoverPresetRow* R = DT->FindRow<FCoverPresetRow>(RowName, TEXT("PickRowForFaction"));
		if (!R || R->Faction != Faction) continue;

		const bool bH = (R->HighCoverMesh != nullptr);
		const bool bL = (R->LowCoverMesh  != nullptr);

		if ( bL &&  bH) Both.Add(RowName);
		if ( bL && !bH) LowOnly.Add(RowName);

		// (Optional) collect HighOnly if you want a third fallback bucket:
		// if ( bH && !bL) HighOnly.Add(RowName);
	}

	auto RandFrom = [](const TArray<FName>& Arr)->FName
	{
		return Arr.Num() ? Arr[FMath::RandRange(0, Arr.Num()-1)] : NAME_None;
	};

	if (bPreferLow)
	{
		if (FName N = RandFrom(LowOnly); N != NAME_None) return N;
		if (FName N = RandFrom(Both);    N != NAME_None) return N;
	}
	else
	{
		if (FName N = RandFrom(Both);    N != NAME_None) return N;
		if (FName N = RandFrom(LowOnly); N != NAME_None) return N;
	}
	return NAME_None;
}

UAbilityEventSubsystem* AMatchGameMode::AbilityBus(UWorld* W)
{
    if (!W) return nullptr;
    if (UGameInstance* GI = W->GetGameInstance())
        return GI->GetSubsystem<UAbilityEventSubsystem>();
    return nullptr;
}

int32 AMatchGameMode::ApplyFeelNoPain(int32 IncomingDamage, int32 Fnp)
{
    if (IncomingDamage <= 0) return 0;
    if (Fnp < 2 || Fnp > 6) return IncomingDamage; // 7 = none

    int32 prevented = 0;
    for (int32 i = 0; i < IncomingDamage; ++i)
    {
        if (D6() >= Fnp) ++prevented;
    }
    return FMath::Max(0, IncomingDamage - prevented);
}

void AMatchGameState::OnRep_Match()
{
	OnDeploymentChanged.Broadcast();

	// Phase might have changed; re-apply selection visual + rings
	OnRep_SelectionVis();
}


ATabletopPlayerState* AMatchGameState::GetPSForTeam(int32 TeamNum) const
{
    if (P1 && P1->TeamNum == TeamNum) return P1;
    if (P2 && P2->TeamNum == TeamNum) return P2;

    // Optional: future-proof for >2 players
    for (APlayerState* PS : PlayerArray)
        if (auto* T = Cast<ATabletopPlayerState>(PS); T && T->TeamNum == TeamNum) return T;

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
    DOREPLIFETIME(AMatchGameState, FinalSummary);
    DOREPLIFETIME(AMatchGameState, bShowSummary);
    DOREPLIFETIME(AMatchGameState, SelectedUnitGlobal);
    DOREPLIFETIME(AMatchGameState, TargetUnitGlobal);
	DOREPLIFETIME(AMatchGameState, CoverPresetsTable);
	DOREPLIFETIME(AMatchGameState, CoverAssignments);
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

    FTransform T(FRotator::ZeroRotator, WorldLoc);
    TSubclassOf<ANetDebugTextActor> Cls = DebugTextActorClass;
    if (!Cls)
    {
        Cls = TSubclassOf<ANetDebugTextActor>(ANetDebugTextActor::StaticClass());
    }

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

void AMatchGameState::OnRep_Preview()
{
	OnDeploymentChanged.Broadcast();
}

void AMatchGameState::OnRep_ActionPreview()
{
	OnDeploymentChanged.Broadcast();
}

void AMatchGameState::Multicast_SetPotentialTargets_Implementation(const TArray<AUnitBase*>& NewPotentials)
{
    // Clear old potentials
    for (TWeakObjectPtr<AUnitBase>& WeakU : LastPotentialApplied)
        if (WeakU.IsValid()) WeakU->SetHighlightLocal(EUnitHighlight::None);
    LastPotentialApplied.Reset();

    // Apply new
    for (AUnitBase* U : NewPotentials)
    {
        if (IsValid(U) && U != TargetUnitGlobal)
            U->SetHighlightLocal(EUnitHighlight::PotentialEnemy);
        LastPotentialApplied.Add(U);
    }

	OnDeploymentChanged.Broadcast();
}

void AMatchGameState::Multicast_SetPotentialAllies_Implementation(const TArray<AUnitBase*>& NewPotentials)
{
	// Clear old potentials
	for (TWeakObjectPtr<AUnitBase>& WeakU : LastPotentialApplied)
		if (WeakU.IsValid()) WeakU->SetHighlightLocal(EUnitHighlight::None);
	LastPotentialApplied.Reset();

	// Apply new (use PotentialAlly instead of PotentialEnemy)
	for (AUnitBase* U : NewPotentials)
	{
		if (IsValid(U) && U != TargetUnitGlobal)
			U->SetHighlightLocal(EUnitHighlight::PotentialAlly);
		LastPotentialApplied.Add(U);
	}

	OnDeploymentChanged.Broadcast();
}

void AMatchGameState::Multicast_ClearPotentialTargets_Implementation()
{
    for (TWeakObjectPtr<AUnitBase>& WeakU : LastPotentialApplied)
    {
        if (!WeakU.IsValid()) continue;

        AUnitBase* U = WeakU.Get();
        // Don’t stomp live highlights
        if (U == SelectedUnitGlobal) continue;
        if (U == TargetUnitGlobal)   continue;

        U->SetHighlightLocal(EUnitHighlight::None);
    }
    LastPotentialApplied.Reset();

	OnDeploymentChanged.Broadcast();
}

void AMatchGameState::BeginPlay()
{
    Super::BeginPlay();
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AObjectiveMarker::StaticClass(), Found);
    Objectives.Reserve(Found.Num());
    for (AActor* A : Found)
        if (auto* M = Cast<AObjectiveMarker>(A)) Objectives.Add(M);

	FWorldDelegates::LevelAddedToWorld.AddUObject(this, &AMatchGameState::OnLevelAdded);
}

void AMatchGameMode::Emit(ECombatEvent E, AUnitBase* Src, AUnitBase* Tgt,
							  const FVector& Pos, float Radius,
							  const FGameplayTagContainer* CurrentTags)
{
	if (UAbilityEventSubsystem* Bus = AbilityBus(GetWorld()))
	{
		FAbilityEventContext C;
		C.Event   = E;
		C.GM      = const_cast<AMatchGameMode*>(this);
		C.GS      = GS();
		C.Source  = Src;
		C.Target  = Tgt;
		C.WorldPos= Pos;
		C.Radius  = Radius;
		if (CurrentTags) C.Tags = *CurrentTags;
		Bus->Broadcast(C);
	}
}

void AMatchGameState::OnLevelAdded(ULevel* Level, UWorld* World)
{
	if (World == GetWorld())
	{
		ReapplyAllCoverAssignments();
	}
}

void AMatchGameState::OnRep_CoverAssignments()
{
	ReapplyAllCoverAssignments();
}

void AMatchGameState::ReapplyAllCoverAssignments()
{
	for (const FCoverRowAssignment& A : CoverAssignments)
	{
		ApplyCoverAssignment(A);
	}
}

void AMatchGameState::ApplyCoverAssignment(const FCoverRowAssignment& A)
{
	ACoverVolume* CV = A.Volume.Get();
	if (!IsValid(CV)) return;

	CV->HighToLowPct = FMath::Clamp(A.ThresholdPct, 0.f, 1.f);
	CV->ApplyPresetMeshes(A.HighMesh, A.LowMesh, A.NoneMesh);
	CV->SetHealthPercentImmediate(FMath::Clamp(A.StartPct, 0.f, 1.f));

	// (Server) not strictly needed, but harmless
	if (HasAuthority()) { CV->ForceNetUpdate(); }
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

    if (ANetDebugTextActor* A = W->SpawnActorDeferred<ANetDebugTextActor>(Cls, T))
    {
        A->Init(Msg, Color, DebugTextWorldSize * 1.1f, Duration);
        UGameplayStatics::FinishSpawningActor(A, T);
    }
}

static TWeakObjectPtr<AUnitBase> GS_LastSel;
static TWeakObjectPtr<AUnitBase> GS_LastTgt;

void AMatchGameState::OnRep_SelectionVis()
{
	// Clear previous
	if (GS_LastSel.IsValid() && GS_LastSel.Get() != SelectedUnitGlobal)
	{
		GS_LastSel->SetHighlightLocal(EUnitHighlight::None);
		GS_LastSel->HideRangePreview(); // NEW
	}
	if (GS_LastTgt.IsValid() && GS_LastTgt.Get() != TargetUnitGlobal)
	{
		GS_LastTgt->SetHighlightLocal(EUnitHighlight::None);
		GS_LastTgt->HideRangePreview(); // NEW
	}

	// Apply new
	if (SelectedUnitGlobal)
	{
		SelectedUnitGlobal->SetHighlightLocal(EUnitHighlight::Friendly);
		SelectedUnitGlobal->UpdateRangePreview(/*bAsTargetContext*/false); // NEW
	}
	if (TargetUnitGlobal)
	{
		TargetUnitGlobal->SetHighlightLocal(EUnitHighlight::Enemy);
		TargetUnitGlobal->UpdateRangePreview(/*bAsTargetContext*/true); // NEW
	}

	GS_LastSel = SelectedUnitGlobal;
	GS_LastTgt = TargetUnitGlobal;

	OnDeploymentChanged.Broadcast();
}

FText AMatchGameMode::BuildRosterDisplayLabel(APlayerState* ForPS, const FRosterEntry& E) const
{
	const ATabletopPlayerState* TPS = ForPS ? Cast<ATabletopPlayerState>(ForPS) : nullptr;
	if (!TPS) return FText::FromName(E.UnitId);

	UDataTable* UnitsDT = UnitsForFaction(TPS->SelectedFaction);
	if (!UnitsDT) return FText::FromName(E.UnitId);

	const FUnitRow* Row = UnitsDT->FindRow<FUnitRow>(E.UnitId, TEXT("RosterLabel"));
	if (!Row) return FText::FromName(E.UnitId);

	FString Label = E.UnitId.ToString();
	if (Row->Weapons.IsValidIndex(E.WeaponIndex))
	{
		Label += FString::Printf(TEXT(" — %s"), *Row->Weapons[E.WeaponIndex].WeaponId.ToString());
	}
	return FText::FromString(Label);
}

void AMatchGameMode::FillServerLabelsFor(APlayerState* ForPS, TArray<FRosterEntry>& Arr) const
{
	for (FRosterEntry& E : Arr)
	{
		E.ServerDisplayLabel = BuildRosterDisplayLabel(ForPS, E);
	}
}

void AMatchGameState::Multicast_ApplySelectionVis_Implementation(AUnitBase* NewSel, AUnitBase* NewTgt)
{
	// Clear old
	if (LastSelApplied && LastSelApplied != NewSel)
	{
		LastSelApplied->SetHighlightLocal(EUnitHighlight::None);
		LastSelApplied->HideRangePreview(); // NEW
	}
	if (LastTgtApplied && LastTgtApplied != NewTgt)
	{
		LastTgtApplied->SetHighlightLocal(EUnitHighlight::None);
		LastTgtApplied->HideRangePreview(); // NEW
	}

	// Apply new
	if (NewSel)
	{
		NewSel->SetHighlightLocal(EUnitHighlight::Friendly);
		NewSel->UpdateRangePreview(/*bAsTargetContext*/false); // NEW
	}
	if (NewTgt)
	{
		NewTgt->SetHighlightLocal(EUnitHighlight::Enemy);
		NewTgt->UpdateRangePreview(/*bAsTargetContext*/true); // NEW
	}

	LastSelApplied = NewSel;
	LastTgtApplied = NewTgt;

	OnDeploymentChanged.Broadcast();
}

// Server-side helpers (also apply locally so a listen host sees updates)
void AMatchGameState::SetGlobalSelected(AUnitBase* NewSel)
{
    if (!HasAuthority()) return;

    // Only allow selecting your own unit, and only during Battle
    if (!NewSel || Phase != EMatchPhase::Battle || !CurrentTurn || NewSel->OwningPS != CurrentTurn)
    {
        NewSel = nullptr;
    }

    if (SelectedUnitGlobal == NewSel) return;
    SelectedUnitGlobal = NewSel;

    Multicast_ApplySelectionVis(SelectedUnitGlobal, TargetUnitGlobal);
    ForceNetUpdate();

    // Potential targets only make sense if we have a valid attacker in Shoot
    if (Phase == EMatchPhase::Battle && TurnPhase == ETurnPhase::Shoot && SelectedUnitGlobal)
    {
        if (AMatchGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AMatchGameMode>() : nullptr)
            GM->BroadcastPotentialTargets(SelectedUnitGlobal);
    }
    else
    {
        Multicast_ClearPotentialTargets();
    }
}

void AMatchGameState::SetGlobalTarget(AUnitBase* NewTgt)
{
    if (!HasAuthority()) return;

    const bool bAllow =
        (Phase == EMatchPhase::Battle) &&
        (TurnPhase == ETurnPhase::Shoot) &&
        SelectedUnitGlobal &&
        (SelectedUnitGlobal->OwningPS == CurrentTurn) &&
        NewTgt &&
        SelectedUnitGlobal->IsEnemy(NewTgt);

    if (!bAllow) NewTgt = nullptr;

    if (TargetUnitGlobal == NewTgt) return;
    TargetUnitGlobal = NewTgt;

    Multicast_ApplySelectionVis(SelectedUnitGlobal, TargetUnitGlobal);
    ForceNetUpdate();
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

void AMatchGameMode::RebroadcastCoverPresetsReliable()
{
	if (!HasAuthority()) return;
	UWorld* W = GetWorld(); AMatchGameState* S = GS(); if (!W || !S) return;

	for (TActorIterator<ACoverVolume> It(W); It; ++It)
	{
		ACoverVolume* CV = *It;
		if (!CV) continue;

		// Skip rebroadcasting "empty" state; it causes clients to apply nulls
		if (CV->HighMesh == nullptr && CV->LowMesh == nullptr && CV->NoneMesh == nullptr)
		{
			UE_LOG(LogCoverNet, Verbose, TEXT("[GM::Rebroadcast] Skipping %s (no meshes assigned yet)"),
				   *GetNameSafe(CV));
			continue;
		}

		const float StartPct = (CV->MaxHealth > 0.f) ? (CV->Health / CV->MaxHealth) : 0.f;

		S->Multicast_ApplyCoverPreset(CV,
		  CV->HighMesh, CV->LowMesh, CV->NoneMesh,
		  StartPct, CV->HighToLowPct);
	}
}

void AMatchGameMode::OnClientReportedLoaded(APlayerController* PC)
{
	if (!HasAuthority() || !PC) return;
	ClientsLoaded.Add(PC);

	const int32 ExpectedPlayers = 2; // adjust if you support more
	if (ClientsLoaded.Num() >= ExpectedPlayers)
	{
		// 1) Ensure presets chosen (if not already)
		ApplyCoverPresetsFromTableOnce();

		// 2) Rebroadcast the chosen meshes/threshold/health to everyone
		RebroadcastCoverPresetsReliable();

		// 3) Belt-and-suspenders: do it again a fraction later
		FTimerHandle Th;
		GetWorldTimerManager().SetTimer(Th, this,
			&AMatchGameMode::RebroadcastCoverPresetsReliable, 0.35f, false);
	}
}

void AMatchGameMode::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();
	// Server world is ready; clients may still be streaming.
	// Safe to queue a rebroadcast on next tick.
	FTimerHandle Th;
	GetWorldTimerManager().SetTimerForNextTick(this, &AMatchGameMode::RebroadcastCoverPresetsReliable);
}

void AMatchGameMode::CoverPreset_Dump()
{
	if (!HasAuthority()) { UE_LOG(LogCoverNet, Warning, TEXT("[GM] Dump is server-only.")); return; }
	TArray<ACoverVolume*> Covers; CollectCoverVolumes_AllLevels(GetWorld(), Covers, /*bLog*/true);
	UE_LOG(LogCoverNet, Display, TEXT("[GM] Dump found %d ACoverVolume"), Covers.Num());
}

TArray<FVector> AMatchGameMode::ComputeCohesiveCoveredFormation(
	const FVector& UnitCenter, const FVector& ThreatDir,
	int32 ModelCount, float BaseRadiusCm) const
{
	TArray<FVector> Out; Out.Reserve(ModelCount);
	if (!GetWorld() || ModelCount <= 0) return Out;

	const float cmPer = CmPerTabletopInch();
	const float MaxRadius = 6.f * cmPer;   // within 6"
	const float LinkMax   = 2.f * cmPer;   // <= 2" to at least one neighbor
	const float MinSep    = BaseRadiusCm * 1.9f; // keep small gap between bases

	// Generate candidate ring points (hex spiral) within MaxRadius
	TArray<FVector> Candidates;
	{
		const float d = FMath::Clamp(BaseRadiusCm * 2.1f, 15.f, 120.f);
		auto AxialToXY = [d](int q, int r)->FVector2D {
			const float x = d * (q + r*0.5f);
			const float y = d * (FMath::Sqrt(3.f)*0.5f) * r;
			return {x,y};
		};
		struct FC{int x,y,z;};
		auto Dir = [](int i)->FC{ static const FC D[6]={{1,-1,0},{1,0,-1},{0,1,-1},{-1,1,0},{-1,0,1},{0,-1,1}}; return D[i%6];};
		auto Add = [](FC a,FC b){return FC{a.x+b.x,a.y+b.y,a.z+b.z};};

		TArray<FVector2D> pts; pts.Add({0,0});
		for (int ring=1; pts.Num() < ModelCount*8; ++ring)
		{
			FC cur{ring,-ring,0};
			for(int side=0;side<6;++side)
			{
				const FC step=Dir(side);
				for(int s=0;s<ring;++s)
				{
					pts.Add(AxialToXY(cur.x,cur.z)); cur=Add(cur,step);
					if (pts.Num()>=ModelCount*8) break;
				}
				if (pts.Num()>=ModelCount*8) break;
			}
		}
		// Keep only those within MaxRadius
		for (auto& p: pts) if (p.Size() <= MaxRadius) Candidates.Add(UnitCenter + FVector(p.X, p.Y, 0));
	}

	TArray<ACoverVolume*> AllCovers;
	for (TActorIterator<ACoverVolume> It(GetWorld()); It; ++It) if (*It) AllCovers.Add(*It);
	
	// Returns distance from P to nearest cover surface; OutNormal points *away from cover*.
	// If the point is inside (distance ~ 0), we synthesize a reasonable outward direction.
	auto NearestCoverClearance = [&](const FVector& P, FVector& OutNormal)->float
	{
		float best = TNumericLimits<float>::Max();
		OutNormal = FVector::ZeroVector;

		for (ACoverVolume* CV : AllCovers)
		{
			if (!CV || !CV->Box) continue;

			FVector OnSurface;
			const float d = CV->Box->GetClosestPointOnCollision(P, OnSurface, NAME_None);
			if (d < 0.f) continue; // no valid body instance

			if (d < best)
			{
				best = d;

				// Prefer pushing away from the surface point we got back
				FVector dir = (P - OnSurface);
				if (!dir.IsNearlyZero())
				{
					OutNormal = dir.GetSafeNormal2D();
				}
				else
				{
					// Fallback (e.g., exactly on the surface or degenerate): push away from actor center
					OutNormal = (P - CV->GetActorLocation()).GetSafeNormal2D();
				}
			}
		}
		return best; // FLT_MAX means "no cover found"
	};

	// Treat "base radius" as the minimum desired clearance from cover collision
	const float DesiredClearance = FMath::Clamp(BaseRadiusCm * 0.95f, 10.f, 120.f);

	// Quick cover test near a point (like above)
	auto CoveredAt = [&](const FVector& P)->ECoverType
	{
		const FVector From = P + (-ThreatDir).GetSafeNormal() * (MaxRadius*1.2f) + FVector(0,0,60);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FormCover), false);
		// ignore units in formation solve
		for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It) Params.AddIgnoredActor(*It);
		if (!GetWorld()->LineTraceSingleByChannel(Hit, From, P, CoverTraceChannel, Params)) return ECoverType::None;
		if (ACoverVolume* CV = Cast<ACoverVolume>(Hit.GetActor()))
		{
			const float d = FVector::Dist(Hit.ImpactPoint, P);
			return (d <= 6.f * CmPerTabletopInch()) ? CV->GetCurrentCoverType() : ECoverType::None;
		}
		return ECoverType::None;
	};

	// Score function
	auto Score = [&](const FVector& P, const TArray<FVector>& Placed)->float
	{
		float s = 0.f;
		const ECoverType C = CoveredAt(P);
		if (C == ECoverType::High) s += 10.f;
		else if (C == ECoverType::Low) s += 6.f;

		FVector n;
		const float clear = NearestCoverClearance(P, n);
		if (clear < DesiredClearance)
		{
			// Strong negative so we never pick these unless there is literally no option
			s -= 100000.f - clear; 
		}

		// small preference to be slightly behind cover (dot with -ThreatDir)
		const FVector Off = (P - UnitCenter); 
		s += FVector::DotProduct(Off.GetSafeNormal2D(), (-ThreatDir).GetSafeNormal2D()) * 2.f;

		// cohesion: prefer near center
		s += FMath::Max(0.f, 5.f - (Off.Size2D() / BaseRadiusCm));

		// prefer near some already placed model (to satisfy <=2")
		if (Placed.Num() > 0)
		{
			float best = 1e9f;
			for (const FVector& Q : Placed) best = FMath::Min(best, FVector::Dist2D(P, Q));
			// reward if within link range, light penalty if isolated
			s += (best <= LinkMax) ? 2.f : -2.5f;
		}

		// penalty for being too close (< MinSep)
		for (const FVector& Q : Placed)
		{
			const float d = FVector::Dist2D(P, Q);
			if (d < MinSep) s -= (MinSep - d) * 0.2f;
		}
		return s;
	};

	// Greedy place N models
	for (int i=0;i<ModelCount && Candidates.Num()>0; ++i)
	{
		int32 bestIdx = 0;
		float bestScore = -FLT_MAX;
		for (int32 c=0;c<Candidates.Num();++c)
		{
			const float s = Score(Candidates[c], Out);
			if (s > bestScore) { bestScore = s; bestIdx = c; }
		}
		Out.Add(Candidates[bestIdx]);
		Candidates.RemoveAtSwap(bestIdx);
	}

	// Relaxation: nudge points to satisfy link + separation while staying within MaxRadius
	for (int iter=0; iter<8; ++iter)
	{
		for (int a=0;a<Out.Num();++a)
		{
			FVector P = Out[a];
			// pull towards center if exceeding MaxRadius
			const FVector v = P - UnitCenter;
			const float r = v.Size2D();
			if (r > MaxRadius) P -= v.GetSafeNormal2D() * (r - MaxRadius);

			// push apart if too close, pull a little if no neighbor within LinkMax
			float nearest = 1e9f;
			int   nearIdx = -1;
			for (int b=0;b<Out.Num();++b) if (b!=a)
			{
				const float d = FVector::Dist2D(P, Out[b]);
				if (d < nearest) { nearest=d; nearIdx=b; }
				if (d < MinSep)  P -= (Out[b]-P).GetSafeNormal2D() * (MinSep - d) * 0.5f;
			}
			if (nearIdx>=0 && nearest>LinkMax)
			{
				// softly pull towards nearest to satisfy <=2"
				P += (Out[nearIdx]-P).GetSafeNormal2D() * FMath::Min(15.f, (nearest - LinkMax)*0.33f);
			}

			FVector n;
			float clear = NearestCoverClearance(UnitCenter + P, n);
			if (clear < DesiredClearance && n.SizeSquared() > 0.001f)
			{
				const float push = (DesiredClearance - clear) + 2.f; // tiny extra to clear
				P += n.GetSafeNormal2D() * push;
			}


			Out[a] = P;
		}
	}

	// Return **offsets** local to UnitCenter (so Unit can place its model components)
	for (FVector& P : Out) P -= UnitCenter;
	return Out;
}

bool AMatchGameMode::QueryCoverWithActor(
    AUnitBase* Attacker, AUnitBase* Target,
    int32& OutHitMod, int32& OutSaveMod, ECoverType& OutType,
    ACoverVolume*& OutPrimaryCover, TMap<ACoverVolume*, int32>* OutCoverHits) const
{
    OutHitMod=0; OutSaveMod=0; OutType=ECoverType::None; OutPrimaryCover=nullptr;

    UWorld* W = GetWorld(); if (!W || !Attacker || !Target) return false;
    AMatchGameState* S = GS(); const bool bDraw = (S && bDebugCoverTraces);

    // Collect attacker/target points
    TArray<FVector> APoints;
    for (int i=0;i<Attacker->ModelMeshes.Num();++i) APoints.Add(Attacker->GetMuzzleTransform(i).GetLocation());
    if (APoints.Num()==0) APoints.Add(Attacker->GetActorLocation());

    TArray<FVector> TPoints;
	GetTargetModelHitPoints(Target,TPoints);
    if (TPoints.Num()==0) TPoints.Add(Target->GetActorLocation());

    // Ignore all units; allow hitting cover or world
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverTraceFull), false);
    for (TActorIterator<AUnitBase> It(W); It; ++It) Params.AddIgnoredActor(*It);

    auto DrawRay = [&](const FVector& A, const FVector& B, const FColor& C, float Thk=2.f){
        if (bDraw) S->Multicast_DrawLine(A,B,C,4.f,Thk);
    };
    auto Note = [&](const FVector& P, const FString& Msg, const FColor& C){
        if (bDraw){ S->Multicast_DrawSphere(P,10.f,10,C,4.f,1.5f); S->Multicast_DrawWorldText(P+FVector(0,0,16),Msg,C,4.f,0.9f); }
    };

    // Optional nearest-N selection (fast) or exhaustive cross-matrix
    auto NearestIdxs = [&](const FVector& P, int32 N)->TArray<int32>{
        TArray<int32> Idx; for (int32 i=0;i<APoints.Num();++i) Idx.Add(i);
        Idx.Sort([&](int32 L,int32 R){ return FVector::DistSquared(APoints[L],P) < FVector::DistSquared(APoints[R],P); });
        Idx.SetNum(FMath::Clamp(N,1,Idx.Num()));
        return Idx;
    };

    const int32 RaysN = bExhaustiveCoverCross ? APoints.Num() : FMath::Clamp(RaysPerTargetModel,1,4);

    int32 CoveredModels = 0; bool AnyHigh=false;
    TMap<ACoverVolume*, int32> HitsPerCoverLocal;

    int32 ModelIdx = 0;
    for (const FVector& TP : TPoints)
    {
        ++ModelIdx;
        TArray<int32> Idx = bExhaustiveCoverCross ? TArray<int32>() : NearestIdxs(TP, RaysN);
        if (bExhaustiveCoverCross){ Idx.Reserve(APoints.Num()); for (int32 i=0;i<APoints.Num();++i) Idx.Add(i); }

        bool bModelCovered=false; ECoverType ModelCT=ECoverType::None;

        for (int32 i : Idx)
        {
            const FVector From = APoints[i];
            FHitResult HR;
            const bool bHit = W->LineTraceSingleByChannel(HR, From, TP, CoverTraceChannel, Params);

            if (!bHit)
            {
                DrawRay(From, TP, FColor::Red, 1.5f);
                continue;
            }

        	if (ACoverVolume* CV = Cast<ACoverVolume>(HR.GetActor()))
        	{
        		if (CV->BlocksCoverTrace())
        		{
        			bModelCovered = true;
        			ModelCT = CV->GetCurrentCoverType();
        			HitsPerCoverLocal.FindOrAdd(CV) += 1;

        			DrawRay(From, TP, ModelCT==ECoverType::High?FColor::Emerald:FColor::Cyan, 3.f);
        			Note(HR.ImpactPoint,
						FString::Printf(TEXT("Model %d: %s cover"),
							ModelIdx, ModelCT==ECoverType::High?TEXT("HIGH"):TEXT("LOW")),
						ModelCT==ECoverType::High?FColor::Emerald:FColor::Cyan);
        		}
        		else
        		{
        			DrawRay(From, TP, FColor::Orange, 2.f);
        			Note(HR.ImpactPoint, TEXT("cover actor but no longer blocks"), FColor::Orange);
        		}
        	}
        }

        if (bModelCovered)
        {
            ++CoveredModels;
            if (ModelCT == ECoverType::High) AnyHigh=true;
        }
        else
        {
            Note(TP, FString::Printf(TEXT("Model %d: no cover"), ModelIdx), FColor::Red);
        }
    }

    const int32 Total = FMath::Max(1, TPoints.Num());
    const float Frac  = float(CoveredModels) / float(Total);

    // Hysteresis as before
    const float Now=W->TimeSeconds;
    FCoverPairKey Key{Attacker, Target};
    FCoverPairCache Old{}; if (const FCoverPairCache* F = CoverMemory.Find(Key)) Old=*F;
    const float OnT = CoverModelCoverageThreshold;
    const float OffT= FMath::Max(0.f, OnT - CoverHysteresis);
    const bool bGrant = (Old.LastType==ECoverType::None) ? (Frac>=OnT) : (Frac>=OffT);

    ECoverType SquadCT = bGrant ? (AnyHigh?ECoverType::High:ECoverType::Low) : ECoverType::None;

    // Choose primary cover = most-hit cover (if any)
    ACoverVolume* Primary = nullptr; int32 BestHits=-1;
    for (auto& KV : HitsPerCoverLocal)
        if (KV.Value > BestHits) { BestHits=KV.Value; Primary=KV.Key; }

    OutType = SquadCT;
    OutHitMod  = (SquadCT==ECoverType::High)? -1 : 0;
    OutSaveMod = (SquadCT!=ECoverType::None)?  1 : 0;
    OutPrimaryCover = Primary;
    if (OutCoverHits) *OutCoverHits = HitsPerCoverLocal;

    if (bDraw)
    {
        const FVector Mid=(Attacker->GetActorLocation()+Target->GetActorLocation())*0.5f+FVector(0,0,140.f);
        FString pick = Primary? Primary->GetName() : TEXT("none");
        S->Multicast_DrawShotDebug(Mid,
            FString::Printf(TEXT("[Cover] %d/%d models  frac=%.2f  => %s  primary=%s"),
                CoveredModels, Total, Frac,
                SquadCT==ECoverType::High?TEXT("HIGH"):SquadCT==ECoverType::Low?TEXT("LOW"):TEXT("NONE"),
                *pick),
            SquadCT==ECoverType::None?FColor::Red:FColor::Green, 5.f);
    }

    // Save memory for hysteresis
    FCoverPairCache NewMem; NewMem.LastFraction=Frac; NewMem.LastType=SquadCT;
    const_cast<AMatchGameMode*>(this)->CoverMemory.Add(Key, NewMem);

    return SquadCT != ECoverType::None;
}

bool AMatchGameMode::QueryCover(AUnitBase* A, AUnitBase* T,
                                int32& OutHitMod, int32& OutSaveMod, ECoverType& OutType) const
{
    ACoverVolume* Dummy=nullptr;
    return QueryCoverWithActor(A, T, OutHitMod, OutSaveMod, OutType, Dummy, nullptr);
}

void AMatchGameMode::ApplyDelayedCoverDamage(ACoverVolume* Cover, float Damage, FVector DebugLoc, FString DebugMsg)
{
	if (!HasAuthority()) return;

	if (IsValid(Cover) && Damage > 0.f)
	{
		Cover->ApplyCoverDamage(Damage); // your ACoverVolume method
	}

	if (AMatchGameState* S = GS())
	{
		// Optional: small orange note so you can see it landed with the impacts
		S->Multicast_DrawShotDebug(DebugLoc, DebugMsg, FColor::Orange, 4.f);
		S->OnDeploymentChanged.Broadcast();
		S->ForceNetUpdate();
	}
}


void AMatchGameMode::ResetTurnFor(APlayerState* PS)
{
    for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
    {
        if (It->OwningPS == PS)
        {
            It->MoveBudgetInches = It->MoveMaxInches;
            It->bHasShot = false;
            It->bMovedThisTurn = false;
            It->bAdvancedThisTurn = false;
            It->OnTurnAdvanced(); // decay per-turn unit modifiers
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

void AMatchGameMode::BroadcastPotentialTargets(AUnitBase* Attacker)
{
    if (!HasAuthority() || !Attacker) return;
    AMatchGameState* S = GS(); if (!S) return;
    if (S->Phase != EMatchPhase::Battle || S->TurnPhase != ETurnPhase::Shoot) { S->Multicast_ClearPotentialTargets(); return; }

    // Only show for the active player’s selected attacker
    if (Attacker->OwningPS != S->CurrentTurn) { S->Multicast_ClearPotentialTargets(); return; }

    TArray<AUnitBase*> Potentials;
    for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
    {
        AUnitBase* T = *It;
        if (!T || T == Attacker) continue;
        if (!Attacker->IsEnemy(T)) continue;

        // Reuse your existing shoot gate (range, alive, not same owner, etc.)
        if (ValidateShoot(Attacker, T))
        {
            Potentials.Add(T);
        }
    }

    S->Multicast_SetPotentialTargets(Potentials);
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

    const float AllowedCm = SpendTTIn * CmPerTTI;
    if (AllowedCm + KINDA_SMALL_NUMBER < DistCm)
    {
        const FVector Dir = Delta / DistCm;
        OutFinalDest = Start + Dir * AllowedCm;
        bOutClamped = true;
    }

    if (AMatchGameState* S = GS())
    {
        const FColor Col = bOutClamped ? FColor::Yellow : FColor::Green;
        S->Multicast_DrawSphere(Start,      25.f, 16, Col,           6.f, 2.f);
        S->Multicast_DrawSphere(WantedDest, 25.f, 16, FColor::Silver,6.f, 2.f);
        S->Multicast_DrawSphere(OutFinalDest,25.f,16, Col,           6.f, 2.f);
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

    const float distCm   = FVector::Dist(Start, Dest);
    const float cmPerTTI = CmPerTabletopInch();
    const float distTTIn = distCm / cmPerTTI;
    OutSpentTabletopInches = distTTIn;

    const bool bAllowed = (distTTIn <= U->MoveBudgetInches);
    const FColor Col = bAllowed ? FColor::Green : FColor::Red;

    if (AMatchGameState* S = GS())
    {
        S->Multicast_DrawSphere(Start, 25.f, 16, Col, 10.f, 2.f);
        S->Multicast_DrawSphere(Dest,  25.f, 16, Col, 10.f, 2.f);
        S->Multicast_DrawLine  (Start, Dest, Col, 10.f, 2.f);

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

	Emit(ECombatEvent::PreValidateMove, Unit, nullptr, WantedDest);

    FVector finalDest = WantedDest;
    float   spentTTIn = 0.f;
    bool    bClamped  = false;
	
    ResolveMoveToBudget(Unit, WantedDest, finalDest, spentTTIn, bClamped);

    if (spentTTIn <= KINDA_SMALL_NUMBER || finalDest.Equals(Unit->GetActorLocation(), 0.1f))
    {
        if (Unit->MoveBudgetInches <= KINDA_SMALL_NUMBER && IsValid(PC))
        {
            PC->Client_OnMoveDenied_OverBudget(Unit, /*requested*/0.f, Unit->MoveBudgetInches);
        }
        return;
    }

    Unit->MoveBudgetInches = FMath::Max(0.f, Unit->MoveBudgetInches - spentTTIn);
    Unit->bMovedThisTurn = true;      // NEW: track moved for Heavy/Assault logic
	Emit(ECombatEvent::PreMoveExecute, Unit, nullptr, finalDest);
    Unit->SetActorLocation(finalDest);
	Unit->NotifyMoveChanged();
	Unit->OnRep_Move();
	Unit->ForceNetUpdate();
	Emit(ECombatEvent::PostMove, Unit, nullptr, finalDest);

	const FVector Center = Unit->GetActorLocation();
	const FVector ThreatDir = (Unit->CurrentTarget->GetActorLocation() - Center).GetSafeNormal2D(); // or the most threatening direction

	const float BaseRadiusCm = Unit->ModelSpacingApartCm * 0.5f; // rough base radius
	TArray<FVector> Offs = ComputeCohesiveCoveredFormation(Center, ThreatDir, Unit->ModelsCurrent, BaseRadiusCm);

	// Optional: immediately refresh target previews
	if (S)
	{
		S->OnDeploymentChanged.Broadcast();
		S->ForceNetUpdate();
	}

	Emit(ECombatEvent::Unit_Moved, Unit, nullptr, finalDest);
    
    NotifyUnitTransformChanged(Unit);
    Unit->ForceNetUpdate();

    if (AMatchGameState* S2 = GS())
    {
        S2->SetGlobalSelected(nullptr);
    	S->Multicast_ApplySelectionVis(S->SelectedUnitGlobal, S->TargetUnitGlobal);

        const FString Msg = FString::Printf(
             TEXT("[MoveApply] %s  Spent=%.1f TT-in  NewBudget=%.1f TT-in  %s"),
             *Unit->GetName(), spentTTIn, Unit->MoveBudgetInches,
             bClamped ? TEXT("(clamped)") : TEXT(""));
        S2->Multicast_ScreenMsg(Msg, bClamped ? FColor::Yellow : FColor::Green, 5.f);
    }
    
    if (IsValid(PC))
    {
        PC->Client_OnUnitMoved(Unit, spentTTIn, Unit->MoveBudgetInches);
    }
}

bool AMatchGameMode::ValidateShoot(AUnitBase* A, AUnitBase* T) const
{
    if (!A || !T || A==T) return false;
    if (A->ModelsCurrent <= 0 || T->ModelsCurrent <= 0) return false;
    if (A->OwningPS == T->OwningPS) return false;

    const float distTT = FVector::Dist(A->GetActorLocation(), T->GetActorLocation()) / CmPerTabletopInch();
    const float rngTT  = A->GetWeaponRange(); // stored as tabletop inches
    return (rngTT > 0.f) && (distTT <= rngTT);
}

void AMatchGameMode::Handle_SelectTarget(AMatchPlayerController* PC, AUnitBase* Attacker, AUnitBase* Target)
{
    if (!HasAuthority() || !PC || !Attacker) return;

    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle || S->TurnPhase != ETurnPhase::Shoot) return;
    
    if (PC->PlayerState != S->CurrentTurn) return;
    if (Attacker->OwningPS != PC->PlayerState) return;

    if (!Target || !ValidateShoot(Attacker, Target))
    {
        // Clear preview
        if (S->Preview.Attacker == Attacker)
        {
            S->Preview.Attacker = nullptr;
            S->Preview.Target   = nullptr;
            S->Preview.Phase    = S->TurnPhase;

            S->ActionPreview.HitMod  = 0;
            S->ActionPreview.SaveMod = 0;
            S->ActionPreview.Cover   = ECoverType::None;
        }

        S->SetGlobalTarget(nullptr);
    	S->Multicast_ApplySelectionVis(S->SelectedUnitGlobal, S->TargetUnitGlobal);

        if (S->TurnPhase == ETurnPhase::Shoot)
            BroadcastPotentialTargets(Attacker);
        else
            S->Multicast_ClearPotentialTargets();
        
        // Optional facing
        Attacker->FaceNearestEnemyInstant();

        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
        return;
    }

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

    Attacker->FaceActorInstant(Target);

    S->Multicast_ClearPotentialTargets();
    S->SetGlobalTarget(Target);
	S->Multicast_ApplySelectionVis(S->SelectedUnitGlobal, S->TargetUnitGlobal);

    S->OnDeploymentChanged.Broadcast();
    S->ForceNetUpdate();
}

void AMatchGameMode::Handle_SelectFriendly(AMatchPlayerController* PC, AUnitBase* Attacker, AUnitBase* Target)
{
    if (!HasAuthority() || !PC || !Attacker) return;

	AMatchGameState* S = GS();
	if (!S || S->Phase != EMatchPhase::Battle) return;

	// Accept both phases for friendly targeting
	const bool bGoodPhase = (S->TurnPhase == ETurnPhase::Move || S->TurnPhase == ETurnPhase::Shoot);
	if (!bGoodPhase) return;

	if (PC->PlayerState != S->CurrentTurn) return;
	if (Attacker->OwningPS != PC->PlayerState) return;

    // --- Clearing branch (no target) ---
    if (!Target)
    {
        if (S->Preview.Attacker == Attacker)
        {
            S->Preview.Attacker = nullptr;
            S->Preview.Target   = nullptr;
            S->Preview.Phase    = S->TurnPhase;

            S->ActionPreview.Attacker = nullptr;
            S->ActionPreview.Target   = nullptr;
            S->ActionPreview.HitMod   = 0;
            S->ActionPreview.SaveMod  = 0;
            S->ActionPreview.Cover    = ECoverType::None;
        }

        S->SetGlobalTarget(nullptr);
        S->Multicast_ApplySelectionVis(S->SelectedUnitGlobal, S->TargetUnitGlobal);

        // show friendlies again for pick
        BroadcastPotentialFriendlies(Attacker);

        Attacker->FaceNearestEnemyInstant();

        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
        return;
    }

    // --- Validate: target must be friendly (self allowed) ---
    if (Target->OwningPS != PC->PlayerState) return;

    // (Optional) do range/eligibility checks specific to your ability later.

    // Set the preview (non-combat numbers will be zeroed)
    S->Preview.Attacker = Attacker;
    S->Preview.Target   = Target;
    S->Preview.Phase    = S->TurnPhase;

    S->ActionPreview.Attacker = Attacker;
    S->ActionPreview.Target   = Target;
    S->ActionPreview.Phase    = S->TurnPhase;

    S->ActionPreview.HitMod   = 0;
    S->ActionPreview.SaveMod  = 0;
    S->ActionPreview.Cover    = ECoverType::None; // keep combat UI neutral

    Attacker->FaceActorInstant(Target);

    S->Multicast_ClearPotentialTargets();
    S->SetGlobalTarget(Target);
    S->Multicast_ApplySelectionVis(S->SelectedUnitGlobal, S->TargetUnitGlobal);

    S->OnDeploymentChanged.Broadcast();
    S->ForceNetUpdate();
}

void AMatchGameMode::BroadcastPotentialFriendlies(AUnitBase* Attacker)
{
	if (!Attacker) return;
	AMatchGameState* S = GS(); if (!S) return;

	const float rCm = 12.f * CmPerTabletopInch();
	const float r2  = rCm * rCm;
	const FVector src = Attacker->GetActorLocation();

	TArray<AUnitBase*> out;
	for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
	{
		AUnitBase* U = *It;
		if (!U || U->ModelsCurrent <= 0) continue;
		if (U->OwningPS != Attacker->OwningPS) continue;
		if (FVector::DistSquared(src, U->GetActorLocation()) > r2) continue;
		// Optional: skip full-health squads
		// if (!IsHealable(U)) continue;
		out.Add(U);
	}
	S->Multicast_SetPotentialTargets(out);
}

FShotResolveResult AMatchGameMode::ResolveRangedAttack_Internal(
	AUnitBase* Attacker, AUnitBase* Target, const TCHAR* DebugPrefix)
{
	FShotResolveResult Out;
	if (!HasAuthority() || !Attacker || !Target) return Out;
	if (Attacker->ModelsCurrent <= 0 || Target->ModelsCurrent <= 0) return Out;

	// Use the currently-equipped weapon
	const FWeaponProfile& Weapon = Attacker->GetActiveWeaponProfile();

	// ---- build context ----
	FAttackContext Ctx;
	Ctx.Attacker          = Attacker;
	Ctx.Target            = Target;
	Ctx.Weapon            = &Weapon; // pointer valid here
	Ctx.RangeInches       = FVector::Dist(Attacker->GetActorLocation(), Target->GetActorLocation()) / CmPerTabletopInch();
	Ctx.bAttackerMoved    = Attacker->bMovedThisTurn;
	Ctx.bAttackerAdvanced = Attacker->bAdvancedThisTurn;

	// base numbers
	const int32 Models = FMath::Max(0, Attacker->ModelsCurrent);
	Ctx.Attacks   = FMath::Max(0, Weapon.Attacks) * Models;
	Ctx.HitNeed   = FMath::Clamp(Weapon.SkillToHit, 2, 6);
	Ctx.WoundNeed = ToWoundTarget(Weapon.Strength, Target->GetToughness());
	Ctx.AP        = FMath::Max(0, Weapon.AP);
	Ctx.Damage    = FMath::Max(1, Weapon.Damage);

	// ---- Movement-gated weapon rules ----
	const bool bHasHeavy   = UWeaponKeywordHelpers::HasKeyword(Weapon, EWeaponKeyword::Heavy);
	const bool bHasAssault = UWeaponKeywordHelpers::HasKeyword(Weapon, EWeaponKeyword::Assault);
	const FWeaponKeywordData* RF = UWeaponKeywordHelpers::FindKeyword(Weapon, EWeaponKeyword::RapidFire);

	// TODO - Disabling block as now action points are the only blocker for shooting - we should not prevent if we get to this point
	// Assault: allow shooting after Advance; if not Assault and advanced, disallow
	// if (Ctx.bAttackerAdvanced && !bHasAssault)
	// {
	// 	if (AMatchGameState* S2 = GS())
	// 		S2->Multicast_ScreenMsg(
	// 			FString::Printf(TEXT("%s Cannot shoot after Advancing (weapon is not Assault)."),
	// 			DebugPrefix ? DebugPrefix : TEXT("[Shot]")),
	// 			FColor::Red, 3.f);
	// 	return Out;
	// }

	// Heavy: +1 to hit if did not move
	if (bHasHeavy && !Ctx.bAttackerMoved)
	{
		Ctx.HitNeed = FMath::Clamp(Ctx.HitNeed - 1, 2, 6);
	}

	// Rapid Fire X: +X attacks per model at half-range or less
	if (RF && RF->Value > 0)
	{
		const bool bHalfRange = (Ctx.RangeInches <= (float(Weapon.RangeInches) * 0.5f + KINDA_SMALL_NUMBER));
		if (bHalfRange)
		{
			Ctx.Attacks += RF->Value * FMath::Max(0, Attacker->ModelsCurrent);
		}
	}

	// Cover baseline (keywords/mods can override some effects like Ignores Cover)
	int32 HitMod = 0, SaveMod = 0;
	ECoverType Cover = ECoverType::None;
	ACoverVolume* PrimaryCover = nullptr;
	TMap<ACoverVolume*, int32> CoverHits;
	QueryCoverWithActor(Attacker, Target, HitMod, SaveMod, Cover, PrimaryCover, &CoverHits);

	Emit(ECombatEvent::PreHitCalc, Attacker, Target);
	// ===== Stage: PreHitCalc =====
	{
		FStageResult K = FKeywordProcessor::BuildForStage(Ctx, ECombatEvent::PreHitCalc);
		FRollModifiers AMods = Attacker->CollectStageMods(ECombatEvent::PreHitCalc, /*bAsAttacker*/true, Target);
		FRollModifiers TMods = Target  ->CollectStageMods(ECombatEvent::PreHitCalc, /*bAsAttacker*/false, Attacker);
		FRollModifiers M = K.ModsNow; M.Accumulate(AMods); M.Accumulate(TMods);

		Ctx.Attacks += M.AttacksDelta;
		Ctx.HitNeed  = FMath::Clamp(Ctx.HitNeed + M.HitNeedOffset, 2, 6);
		const bool bAutoHit = M.bAutoHit;

		Attacker->ConsumeForStage(ECombatEvent::PreHitCalc, true);
		Target  ->ConsumeForStage(ECombatEvent::PreHitCalc, false);

		// Apply cover hit mod AFTER keyword offsets (not affected by IgnoresCover)
		Ctx.HitNeed = FMath::Clamp(Ctx.HitNeed + (HitMod * -1), 2, 6);

		// Roll hits
		if (bAutoHit)
		{
			Ctx.Hits = Ctx.Attacks;
		}
		else
		{
			for (int32 i=0; i<Ctx.Attacks; ++i)
			{
				const int32 r = D6();
				if (r >= Ctx.HitNeed) ++Ctx.Hits;
				Ctx.HitRolls.Add((uint8)r);
			}
		}
		Emit(ECombatEvent::PostHitRolls, Attacker, Target);

		// PostHitRolls (Sustained Hits, Lethal Hits)
		if (Ctx.Weapon)
		{
			if (const FWeaponKeywordData* SH =
					UWeaponKeywordHelpers::FindKeyword(*Ctx.Weapon, EWeaponKeyword::SustainedHits))
			{
				int32 Extra = 0;
				for (uint8 r : Ctx.HitRolls)
				{
					if (r >= Ctx.CritHitThreshold)
					{
						Extra += FMath::Max(0, SH->Value);
					}
				}
				Ctx.Hits += Extra;
			}

			if (UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::LethalHits))
			{
				int32 AutoWounds = 0;
				for (uint8 r : Ctx.HitRolls)
				{
					if (r >= Ctx.CritHitThreshold)
					{
						++AutoWounds;
					}
				}
				Ctx.Wounds += AutoWounds;
			}
		}
	}

	const float CmPerTT = CmPerTabletopInch();
	const float FriendlyIgnoreCm = FriendlyCoverIgnoreProximityInches * CmPerTT;

	// Remove attacker-near covers from damage candidates
	for (auto It = CoverHits.CreateIterator(); It; ++It)
	{
		ACoverVolume* CV = It.Key();
		if (!CV) { It.RemoveCurrent(); continue; }

		if (IsCoverNearActor(CV, Attacker, FriendlyIgnoreCm))
		{
			// Don’t let the shooter destroy their own nearby cover
			It.RemoveCurrent();
		}
	}
	
	int32 BestHits = -1;
	for (auto& KV : CoverHits)
	{
		if (KV.Value > BestHits)
		{
			BestHits   = KV.Value;
			PrimaryCover = KV.Key;
		}
	}

	if (PrimaryCover /* && PrimaryCover->BlocksCoverTrace() */)
	{
		const int32 Misses = FMath::Max(0, Ctx.Attacks - Ctx.Hits);

		// Choose the damage model you want; this uses the (already modified) shot damage.
		const float CoverDamage = float(Misses * FMath::Max(1, Ctx.Damage));

		if (CoverDamage > 0.f)
		{
			const FVector L0  = Attacker->GetActorLocation();
			const FVector L1  = Target->GetActorLocation();
			const FVector Mid = (L0 + L1) * 0.5f + FVector(0,0,150.f);

			const FString CoverMsg = FString::Printf(TEXT("[Cover] %d misses -> %.0f damage to cover"), Misses, CoverDamage);

			const float ImpactDelay = Attacker->ImpactDelaySeconds; // same as unit impact

			FTimerDelegate DelCover;
			DelCover.BindUFunction(this, FName("ApplyDelayedCoverDamage"),
								   PrimaryCover, CoverDamage, Mid, CoverMsg);

			FTimerHandle TmpCover;
			GetWorld()->GetTimerManager().SetTimer(TmpCover, DelCover,
												   FMath::Max(0.f, ImpactDelay), false);
		}
	}

	Emit(ECombatEvent::PreWoundCalc, Attacker, Target);
	// ===== Stage: PreWoundCalc =====
	{
		FStageResult K = FKeywordProcessor::BuildForStage(Ctx, ECombatEvent::PreWoundCalc);
		FRollModifiers AMods = Attacker->CollectStageMods(ECombatEvent::PreWoundCalc, true, Target);
		FRollModifiers TMods = Target  ->CollectStageMods(ECombatEvent::PreWoundCalc, false, Attacker);
		FRollModifiers M = K.ModsNow; M.Accumulate(AMods); M.Accumulate(TMods);

		Ctx.WoundNeed = FMath::Clamp(Ctx.WoundNeed + M.WoundNeedOffset, 2, 6);

		Attacker->ConsumeForStage(ECombatEvent::PreWoundCalc, true);
		Target  ->ConsumeForStage(ECombatEvent::PreWoundCalc, false);

		const int32 HitsNeedingWound = FMath::Max(0, Ctx.Hits - Ctx.Wounds); // subtract auto-wounds
		int32 NewWounds = 0;
		for (int32 i=0; i<HitsNeedingWound; ++i)
		{
			const int32 r = D6();
			if (r >= Ctx.WoundNeed) ++NewWounds;
			Ctx.WoundRolls.Add((uint8)r);
		}
		Ctx.Wounds += NewWounds;

		// Devastating Wounds -> no-save crits
		if (Ctx.Weapon && UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::DevastatingWounds))
		{
			int32 Crits = 0;
			for (uint8 r : Ctx.WoundRolls) if (r >= Ctx.CritWoundThreshold) ++Crits;
			Ctx.CritWounds_NoSave += Crits;
		}
		Emit(ECombatEvent::PostWoundRolls, Attacker, Target);
	}

	Emit(ECombatEvent::PreSavingThrows, Attacker, Target);
	// ===== Stage: PreSavingThrows =====
	bool bIgnoreCover = false;
	int32 InvulnOffset = 0;
	{
		FStageResult K = FKeywordProcessor::BuildForStage(Ctx, ECombatEvent::PreSavingThrows);
		FRollModifiers AMods = Attacker->CollectStageMods(ECombatEvent::PreSavingThrows, true, Target);
		FRollModifiers TMods = Target  ->CollectStageMods(ECombatEvent::PreSavingThrows, false, Attacker);
		FRollModifiers M = K.ModsNow; M.Accumulate(AMods); M.Accumulate(TMods);

		Ctx.AP     += M.APDelta;
		Ctx.Damage += M.DamageDelta;
		bIgnoreCover = M.bIgnoreCover;

		InvulnOffset = M.InvulnNeedOffset;

		Attacker->ConsumeForStage(ECombatEvent::PreSavingThrows, true);
		Target  ->ConsumeForStage(ECombatEvent::PreSavingThrows, false);
	}

	const int32 SaveBase = Target->GetSave();

	// 1) Apply AP to armour
	int32 ArmourNeed = ModifiedSaveNeed(SaveBase, Ctx.AP);

	// 2) Apply cover to *armour* only
	if (bIgnoreCover) { SaveMod = 0; }
	ArmourNeed = FMath::Clamp(ArmourNeed - SaveMod, 2, 7);

	// 3) Cap by invulnerable
	int32 InvTN = Target->GetInvuln();                   // 2..6, 7 = none
	InvTN = FMath::Clamp(InvTN + InvulnOffset, 2, 7);    // NEW
	int32 SaveNeed = (InvTN >= 2 && InvTN <= 6) ? FMath::Min(ArmourNeed, InvTN) : ArmourNeed;

	const bool bHasSave = (SaveNeed <= 6);

	// Split normal vs no-save crit wounds
	const int32 NormalWounds = FMath::Max(0, Ctx.Wounds - Ctx.CritWounds_NoSave);

	int32 Unsaved = 0;
	if (bHasSave)
	{
		for (int i=0; i<NormalWounds; ++i)
			if (D6() < SaveNeed) ++Unsaved; // fail = unsaved
	}
	else
	{
		Unsaved = NormalWounds;
	}

	// Add crit-no-save wounds directly
	Unsaved += Ctx.CritWounds_NoSave;

	const int32 totalDamage = Unsaved * Ctx.Damage;

	// mark shooter as having shot
	Attacker->bHasShot = true;
	Attacker->ForceNetUpdate();

	// Face for aesthetics
	Attacker->FaceActorInstant(Target);

	Emit(ECombatEvent::PostSavingThrows, Attacker, Target);
	
	// LOS clamp
	const int32 TargetModels    = FMath::Max(0, Target->ModelsCurrent);
	const int32 VisibleModels   = CountVisibleTargetModels(Attacker, Target);
	const int32 WoundsPerModel  = Target->GetWoundsPerModel();
	const int32 MaxDamageByLOS  = VisibleModels * WoundsPerModel;
	const int32 ClampedDamage   = FMath::Min(totalDamage, MaxDamageByLOS);

	// Feel No Pain
	int32 FnpTN = Target->GetFeelNoPain(); // 2..6; 7 none

	// Allow mods at PostDamageCompute (right before FNP application)
	{
		FStageResult K = FKeywordProcessor::BuildForStage(Ctx, ECombatEvent::PostDamageCompute, /*DamageApplied*/ClampedDamage);
		FRollModifiers AMods = Attacker->CollectStageMods(ECombatEvent::PostDamageCompute, true, Target);
		FRollModifiers TMods = Target  ->CollectStageMods(ECombatEvent::PostDamageCompute, false, Attacker);
		FRollModifiers M = K.ModsNow; M.Accumulate(AMods); M.Accumulate(TMods);

		FnpTN = FMath::Clamp(FnpTN + M.FnpNeedOffset, 2, 7); // NEW

		Attacker->ConsumeForStage(ECombatEvent::PostDamageCompute, true);
		Target  ->ConsumeForStage(ECombatEvent::PostDamageCompute, false);
	}

	const int32 FinalDamage = ApplyFeelNoPain(ClampedDamage, FnpTN);
	Emit(ECombatEvent::PostDamageCompute, Attacker, Target);

	// Debug / FX
	const FVector L0  = Attacker->GetActorLocation();
	const FVector L1  = Target->GetActorLocation();
	const FVector Mid = (L0 + L1) * 0.5f + FVector(0,0,150.f);

	const TCHAR* CoverTxt = CoverTypeToText(Cover);
	const int32 savesMade = bHasSave ? (NormalWounds - (Unsaved - Ctx.CritWounds_NoSave)) : 0;

	FString fnpNote;
	if (FnpTN >= 2 && FnpTN <= 6)
	{
		fnpNote = FString::Printf(TEXT("\nFNP %d++ applied: %d -> %d"), FnpTN, ClampedDamage, FinalDamage);
	}

	const FString Prefix = DebugPrefix ? DebugPrefix : TEXT("[Shot]");
	const FString Msg = FString::Printf(
		TEXT("%s\nHit: %d/%d\nWound: %d/%d\nSave: %d/%d (%s%s)\nLOS: %d/%d models visible (cap %d dmg)\nDamage rolled: %d -> clamped: %d%s\nApplied on impact: %d"),
		*Prefix,
		Ctx.Hits, Ctx.Attacks,
		Ctx.Wounds, Ctx.Hits,
		savesMade, NormalWounds, CoverTxt, bIgnoreCover?TEXT(", ignores cover"):TEXT(""),
		VisibleModels, TargetModels, MaxDamageByLOS,
		totalDamage, ClampedDamage, *fnpNote,
		FinalDamage);

	const float ImpactDelay = Attacker->ImpactDelaySeconds;

	// Cache target center & per-model sites NOW (before we apply damage)
	TArray<FImpactSite> Sites;
	BuildImpactSites_Server(Attacker, Target, Sites);
	const FVector TargetCenter = Target->GetActorLocation();

	// Tell everyone to play muzzle now and impacts later using cached sites
	Attacker->Multicast_PlayMuzzleAndImpactFX_AllModels_WithSites(TargetCenter, Sites, ImpactDelay);

	// Schedule damage after the same delay
	FTimerDelegate Del;
	Del.BindUFunction(this, FName("ApplyDelayedDamageAndReport"),
					  Attacker, Target, FinalDamage, Mid, Msg);

	FTimerHandle Tmp;
	GetWorld()->GetTimerManager().SetTimer(Tmp, Del, FMath::Max(0.f, ImpactDelay), false);

	// ===== Stage: PostResolveAttack =====
	{
		FStageResult K = FKeywordProcessor::BuildForStage(Ctx, ECombatEvent::PostResolveAttack, /*DamageApplied*/ClampedDamage);
		FRollModifiers AMods = Attacker->CollectStageMods(ECombatEvent::PostResolveAttack, true, Target);
		FRollModifiers TMods = Target  ->CollectStageMods(ECombatEvent::PostResolveAttack, false, Attacker);
		FRollModifiers M = K.ModsNow; M.Accumulate(AMods); M.Accumulate(TMods);

		if (M.MortalDamageImmediateToOwner    > 0) { Attacker->ApplyMortalDamage_Server(M.MortalDamageImmediateToOwner); }
		if (M.MortalDamageImmediateToOpponent > 0) { Target  ->ApplyMortalDamage_Server(M.MortalDamageImmediateToOpponent); }

		Attacker->ConsumeForStage(ECombatEvent::PostResolveAttack, true);
		Target  ->ConsumeForStage(ECombatEvent::PostResolveAttack, false);

		for (const FUnitModifier& G : K.GrantsToAttacker) Attacker->AddUnitModifier(G);
		for (const FUnitModifier& G : K.GrantsToTarget)   Target  ->AddUnitModifier(G);
	}

	// Fill result for optional UI
	Out.FinalDamage = FinalDamage;
	Out.Attacks     = Ctx.Attacks;
	Out.Hits        = Ctx.Hits;
	Out.Wounds      = Ctx.Wounds;
	Out.SavesMade   = savesMade;
	Out.bIgnoredCover = bIgnoreCover;
	Out.Cover       = Cover;

	// No selection/target touching here; just refresh HUD/state.
	if (AMatchGameState* S2 = GS())
	{
		S2->OnDeploymentChanged.Broadcast();
		S2->ForceNetUpdate();
	}

	CoverMemory.Empty(); // clear cover memory to prevent memory growth

	return Out;
}


void AMatchGameMode::Handle_ConfirmShoot(AMatchPlayerController* PC, AUnitBase* Attacker, AUnitBase* Target)
{
    if (!HasAuthority() || !PC || !Attacker || !Target) return;

    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle || S->TurnPhase != ETurnPhase::Shoot) return;
    if (PC->PlayerState != S->CurrentTurn) return;
    if (Attacker->OwningPS != PC->PlayerState) return;
    if (!ValidateShoot(Attacker, Target)) return;

	Emit(ECombatEvent::PreValidateShoot, Attacker, Target);
    ResolveRangedAttack_Internal(Attacker,Target,TEXT("[Shoot]"));

	if (S->Preview.Attacker == Attacker)
	{
		S->Preview.Attacker = nullptr;
		S->Preview.Target   = nullptr;
		S->Preview.Phase    = S->TurnPhase;
	}
	
    S->SetGlobalTarget(nullptr);
    S->SetGlobalSelected(nullptr);
	S->Multicast_ApplySelectionVis(S->SelectedUnitGlobal, S->TargetUnitGlobal);

    if (IsValid(PC))
    {
        PC->Client_ClearSelectionAfterConfirm();
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

    // NEW: Build a one-time ignore list of ALL units (AUnitBase subclasses)
    TArray<const AActor*> IgnoreUnits;
    IgnoreUnits.Reserve(128);
    for (TActorIterator<AUnitBase> It(World); It; ++It)
    {
        if (const AUnitBase* U = *It)
        {
            IgnoreUnits.Add(U);
        }
    }

    for (int32 j = 0; j < Target->ModelMeshes.Num(); ++j)
    {
        UStaticMeshComponent* TC = Target->ModelMeshes[j];
        if (!IsValid(TC)) continue;

        FVector ModelPoint;
        if (TC->DoesSocketExist(Target->ImpactSocketName))
        {
            ModelPoint = TC->GetSocketTransform(Target->ImpactSocketName, RTS_World).GetLocation();
        }
        else
        {
            ModelPoint = TC->GetComponentTransform().TransformPosition(Target->ImpactOffsetLocal);
        }

        const int32 ShooterIdx = Attacker->FindBestShooterModelIndex(ModelPoint);
        const FVector From = Attacker->GetMuzzleTransform(ShooterIdx).GetLocation();
        const FVector To   = ModelPoint;

        FHitResult Hit;

        // NEW: params that ignore ALL units (attacker/target included)
        FCollisionQueryParams Params(SCENE_QUERY_STAT(UnitLOS), /*bTraceComplex*/ true);
        Params.AddIgnoredActors(IgnoreUnits);

        // Keep your existing LOS channel; just don’t let units block
        const bool bHit = World->LineTraceSingleByChannel(
            Hit, From, To, ECollisionChannel::ECC_GameTraceChannel5 /*LOS*/, Params);

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

void AMatchGameMode::Handle_CancelPreview(AMatchPlayerController* PC, AUnitBase* Attacker)
{
    if (!HasAuthority() || !PC || !Attacker) return;

    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle) return;

   

    if (PC->PlayerState != S->CurrentTurn) return;
    if (Attacker->OwningPS != PC->PlayerState) return;

    if (S->Preview.Attacker == Attacker)
    {
        S->Preview.Attacker = nullptr;
        S->Preview.Target   = nullptr;
        S->Preview.Phase    = S->TurnPhase;
        
        S->SetGlobalTarget(nullptr);
    	S->Multicast_ApplySelectionVis(S->SelectedUnitGlobal, S->TargetUnitGlobal);
        S->Multicast_ClearPotentialTargets();
        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
    }
}

void AMatchGameState::Multicast_ApplyCoverPreset_Implementation(
	ACoverVolume* Volume, UStaticMesh* HighMesh, UStaticMesh* LowMesh,
	UStaticMesh* NoneMesh, float StartHealthPct, float ThresholdPct)
{
	if (!IsValid(Volume)) return;

	Volume->HighToLowPct = FMath::Clamp(ThresholdPct, 0.f, 1.f);
	Volume->ApplyPresetMeshes(HighMesh, LowMesh, NoneMesh);

	const bool bAuth = HasAuthority();
	UE_LOG(LogCoverNet, Log, TEXT("[GS::ApplyPreset] Vol=%s High=%s Low=%s None=%s Start=%.2f Thr=%.2f Auth=%d"),
		*GetNameSafe(Volume),
		HighMesh? *HighMesh->GetName():TEXT("null"),
		LowMesh ? *LowMesh ->GetName():TEXT("null"),
		NoneMesh?*NoneMesh->GetName():TEXT("null"),
		StartHealthPct, ThresholdPct, bAuth?1:0);

	if (bAuth)
	{
		// If you have immediate health setter on CV:
		// Volume->SetHealthPercentImmediate(FMath::Clamp(StartHealthPct, 0.f, 1.f));
		Volume->ForceNetUpdate();
	}
}

void AMatchGameMode::BeginPlay()
{
    Super::BeginPlay();

	if (AMatchGameState* S = GS())
	{
		S->CmPerTTInchRep = CmPerTabletopInch();
		S->ForceNetUpdate();
	}

	if (AMatchGameState* S = GS())
	{
		S->CoverPresetsTable = CoverPresetsTable; // replicate pointer to clients
		S->ForceNetUpdate();
	}
}

void AMatchGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    if (AMatchGameState* S = GS())
    {
        ATabletopPlayerState* NewTPS = NewPlayer ? NewPlayer->GetPlayerState<ATabletopPlayerState>() : nullptr;
        if (!NewTPS)
        {
            UE_LOG(LogTemp, Error, TEXT("PostLogin: PlayerState is not ATabletopPlayerState. "
                                        "Make sure PlayerStateClass = ATabletopPlayerState in GameMode."));
            return;
        }

        if (!S->P1)                    S->P1 = NewTPS;
        else if (!S->P2 && S->P1 != NewTPS) S->P2 = NewTPS;

        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
    }

    FinalizePlayerJoin(NewPlayer);
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
            	if (U->bOverwatchArmed)
            	{
            		Emit(ECombatEvent::Ability_Expired, U); // never triggered -> expired now
            	}

            	U->SetOverwatchArmed(false);          // <-- server gets an immediate local hide
            	U->bOverwatchVisibleToEnemies = false; // optional: also clear the telegraph flag
            	U->ForceNetUpdate();
            	
                U->MoveBudgetInches = U->MoveMaxInches;
                U->bHasShot         = false;
                U->bMovedThisTurn   = false;
                U->bAdvancedThisTurn= false;

                U->OnTurnAdvanced(); // decay turn-based unit mods
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


bool AMatchGameMode::CanDeployAt(APlayerController* PC, const FVector& Location) const
{
    if (!PC) return false;

    const AMatchGameState* S = GetGameState<AMatchGameState>();
    if (!S || S->Phase != EMatchPhase::Deployment) return false;

    if (S->CurrentDeployer && PC->PlayerState != S->CurrentDeployer)
        return false;

    const ATabletopPlayerState* TPS = PC->GetPlayerState<ATabletopPlayerState>();
    const int32 Team = TPS ? TPS->TeamNum : 0;
    if (Team <= 0) return false;

    return ADeploymentZone::IsLocationAllowedForTeam(GetWorld(), Team, Location);
}

void AMatchGameMode::CopyRostersFromPlayerStates()
{
	if (AMatchGameState* S = GS())
	{
		S->P1Remaining = S->P1 ? S->P1->Roster : TArray<FRosterEntry>{};
		S->P2Remaining = S->P2 ? S->P2->Roster : TArray<FRosterEntry>{};

		// NEW: compute once on the server; replicates to everyone
		if (S->P1) { FillServerLabelsFor(S->P1, S->P1Remaining); }
		if (S->P2) { FillServerLabelsFor(S->P2, S->P2Remaining); }
	}
}

bool AMatchGameMode::AnyRemainingFor(APlayerState* PS) const
{
    if (const AMatchGameState* S = GS())
    {
        const bool bIsP1 = (PS == S->P1);
        const TArray<FRosterEntry>& R = bIsP1 ? S->P1Remaining : S->P2Remaining;
        for (const FRosterEntry& E : R) if (E.Count > 0) return true;
    }
    return false;
}

static int32 FindIdx(TArray<FRosterEntry>& Arr, FName Unit, int32 WeaponIndex)
{
	for (int32 i=0; i<Arr.Num(); ++i)
		if (Arr[i].UnitId == Unit && Arr[i].WeaponIndex == WeaponIndex)
			return i;
	return INDEX_NONE;
}

bool AMatchGameMode::DecrementOne(APlayerState* PS, FName UnitId, int32 WeaponIndex)
{
	if (AMatchGameState* S = GS())
	{
		const bool bIsP1 = (PS == S->P1);
		TArray<FRosterEntry>& R = bIsP1 ? S->P1Remaining : S->P2Remaining;
		if (int32 Idx = FindIdx(R, UnitId, WeaponIndex); Idx != INDEX_NONE && R[Idx].Count > 0)
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

void AMatchGameMode::HandleRequestDeploy(APlayerController* PC, FName UnitId, const FTransform& Where, int32 WeaponIndex)
{
    if (!HasAuthority() || !PC) return;

    if (AMatchGameState* S = GS())
    {
        if (PC->PlayerState.Get() != S->CurrentDeployer) return;

        if (!CanDeployAt(PC, Where.GetLocation()))
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy denied: outside deployment zone or not your turn."));
            return;
        }

    	if (!DecrementOne(PC->PlayerState.Get(), UnitId, WeaponIndex))
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy denied: unit %s not available in remaining roster."), *UnitId.ToString());
            return;
        }

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
    	
    	if (AUnitBase* UB = Cast<AUnitBase>(Spawned))
    	{
    		UB->Server_InitFromRow(PC->PlayerState.Get(), *Row, WeaponIndex);
    		UB->FaceNearestEnemyInstant();
    		NotifyUnitTransformChanged(UB);
    	}
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Deploy note: Spawned class %s is not AUnitBase for %s; skipping runtime init."),
                   *SpawnClass->GetName(), *UnitId.ToString());
        }

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
        if (PC->PlayerState != S->P1) return;
        if (!S->bDeploymentComplete)  return;

        S->Phase       = EMatchPhase::Battle;
        S->CurrentRound= 1;
        S->MaxRounds   = 5;
        S->TurnInRound = 0;
        S->TurnPhase   = ETurnPhase::Move;

        S->CurrentTurn = S->CurrentDeployer;

    	if (S->P1 && S->P2 /*&& !bCoverPresetsApplied*/)
    	{
    		ApplyCoverPresetsFromTableOnce();
    		RebroadcastCoverPresetsReliable();
    		bCoverPresetsApplied = true;
    	}

        ResetUnitRoundStateFor(S->CurrentTurn);

        S->SetGlobalSelected(nullptr);
        S->SetGlobalTarget(nullptr);
        S->Multicast_ClearPotentialTargets();
    	S->Multicast_ApplySelectionVis(S->SelectedUnitGlobal, S->TargetUnitGlobal);

    	Emit(ECombatEvent::Game_Begin);
    	Emit(ECombatEvent::Round_Begin);
    	Emit(ECombatEvent::Turn_Begin, /*Src=*/nullptr);     // optional: pass a unit owned by CurrentTurn if you prefer
    	Emit(ECombatEvent::Phase_Begin);                     // Move phase begins

        // Recalc objectives ONCE, then award Move-phase AP (with Round1/Move guard inside unit)
        for (TActorIterator<AObjectiveMarker> It(GetWorld()); It; ++It)
            if (AObjectiveMarker* Obj = *It) Obj->RecalculateControl();

        for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
            if (AUnitBase* U = *It)
                if (U->OwningPS == S->CurrentTurn)
                    U->ApplyAPPhaseStart(ETurnPhase::Move);

        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();

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
    if (PC->PlayerState != S->CurrentTurn) return;

    S->SetGlobalSelected(nullptr);
    S->SetGlobalTarget(nullptr);
    S->Multicast_ClearPotentialTargets();
	S->Multicast_ApplySelectionVis(S->SelectedUnitGlobal, S->TargetUnitGlobal);

    auto ApplyPhaseStartAP = [&](APlayerState* TurnOwner, ETurnPhase NewPhase)
    {
        // 1) Refresh objective controllers once
        for (TActorIterator<AObjectiveMarker> It(GetWorld()); It; ++It)
            if (AObjectiveMarker* Obj = *It) Obj->RecalculateControl();

        // 2) Apply per-unit AP start logic
        for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
        {
            AUnitBase* U = *It;
            if (!U || U->OwningPS != TurnOwner) continue;
            U->ApplyAPPhaseStart(NewPhase);
        }
    };

    if (S->TurnPhase == ETurnPhase::Move)
    {
    	Emit(ECombatEvent::Phase_End);
        S->TurnPhase = ETurnPhase::Shoot;
    	Emit(ECombatEvent::Phase_Begin); 
        ApplyPhaseStartAP(S->CurrentTurn, ETurnPhase::Shoot);
        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
        return;
    }

    // Swap player or advance round, as you already do…
    if (S->TurnInRound == 0)
    {
        S->TurnInRound = 1;
    	Emit(ECombatEvent::Phase_End);       // Shoot ends
    	Emit(ECombatEvent::Turn_End);
        S->CurrentTurn = OtherPlayer(S->CurrentTurn);
    	Emit(ECombatEvent::Turn_Begin);      // new player’s turn begins
    	Emit(ECombatEvent::Phase_Begin);
        S->TurnPhase   = ETurnPhase::Move;

        ResetUnitRoundStateFor(S->CurrentTurn);
        ApplyPhaseStartAP(S->CurrentTurn, ETurnPhase::Move);

        S->OnDeploymentChanged.Broadcast();
        S->ForceNetUpdate();
        return;
    }

	Emit(ECombatEvent::Phase_End);       // Shoot ends
	Emit(ECombatEvent::Turn_End);        // second player’s turn ends
	Emit(ECombatEvent::Round_End);
    S->CurrentRound = FMath::Clamp<uint8>(S->CurrentRound + 1, 1, S->MaxRounds);
    S->TurnInRound  = 0;

    ScoreObjectivesForRound();
    S->OnDeploymentChanged.Broadcast();
    S->ForceNetUpdate();

    // Per-round decays...
    for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
        if (AUnitBase* U = *It) U->OnRoundAdvanced();

    // If we've finished the last round, end the game and show summary
    if (S->CurrentRound >= S->MaxRounds)
    {
    	Emit(ECombatEvent::Game_End);
        BuildMatchSummaryAndReveal();
        return;
    }

    // Otherwise continue normally
    S->CurrentTurn = OtherPlayer(S->CurrentTurn);
    S->TurnPhase   = ETurnPhase::Move;

	Emit(ECombatEvent::Round_Begin);     // new round starts
	Emit(ECombatEvent::Turn_Begin);      // new active player’s turn begins
	Emit(ECombatEvent::Phase_Begin);
    
    ResetUnitRoundStateFor(S->CurrentTurn);
    ApplyPhaseStartAP(S->CurrentTurn, ETurnPhase::Move);

    S->SetGlobalSelected(nullptr);
    S->SetGlobalTarget(nullptr);
    S->Multicast_ClearPotentialTargets();
	S->Multicast_ApplySelectionVis(S->SelectedUnitGlobal, S->TargetUnitGlobal);

    S->OnDeploymentChanged.Broadcast();
    S->ForceNetUpdate();
}

void AMatchGameMode::ScoreObjectivesForRound()
{
    if (!HasAuthority()) return;
    AMatchGameState* S = GS();
    if (!S) return;

    int32 P1Delta = 0, P2Delta = 0;

    TArray<AObjectiveMarker*> Objectives;
    for (TActorIterator<AObjectiveMarker> It(GetWorld()); It; ++It)
        Objectives.Add(*It);

    for (AObjectiveMarker* Obj : Objectives)
    {
        if (!Obj) continue;

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

void AMatchGameMode::Handle_OverwatchShot(AUnitBase* Attacker, AUnitBase* Target)
{
    if (!HasAuthority() || !Attacker || !Target) return;
    AMatchGameState* S = GS(); if (!S || S->Phase != EMatchPhase::Battle) return;
    if (!Attacker->IsEnemy(Target)) return;
    if (!ValidateShoot(Attacker, Target)) return;

	Emit(ECombatEvent::PreValidateShoot, Attacker, Target);
    ResolveRangedAttack_Internal(Attacker, Target, TEXT("[Overwatch]"));
	
}

void AMatchGameMode::Handle_AdvanceUnit(AMatchPlayerController* PC, AUnitBase* Unit)
{
    if (!HasAuthority() || !PC || !Unit) return;

    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle || S->TurnPhase != ETurnPhase::Move) return;
    if (PC->PlayerState != S->CurrentTurn) return;
    if (Unit->OwningPS != PC->PlayerState) return;
    if (Unit->bAdvancedThisTurn) return; // already advanced
	
	Emit(ECombatEvent::PreAdvanceExecute, Unit);

    // Roll bonus in [1 .. MoveMaxInches] (integers)
    const int32 Max = FMath::Max(1, (int32)FMath::RoundToInt(Unit->MoveMaxInches));
    const int32 Bonus = FMath::RandRange(1, Max);

    Unit->MoveBudgetInches += (float)Bonus;
    Unit->bAdvancedThisTurn = true;
	Emit(ECombatEvent::PostAdvance, Unit);
	Unit->NotifyMoveChanged();
	Unit->OnRep_Move();
    Unit->ForceNetUpdate();

    if (AMatchGameState* S2 = GS())
    {
        S2->Multicast_ScreenMsg(
            FString::Printf(TEXT("%s advanced +%d\""), *Unit->GetName(), Bonus),
            FColor::Cyan, 3.f);
    }

    if (IsValid(PC))
    {
        PC->Client_OnAdvanced(Unit, Bonus);
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
    	Emit(ECombatEvent::PostResolveAttack, Attacker, Target);
    }

    S->Multicast_DrawShotDebug(DebugMid, DebugMsg, FColor::Black, 8.f);

    S->OnDeploymentChanged.Broadcast();
    S->ForceNetUpdate();
}

void AMatchGameMode::BuildMatchSummaryAndReveal()
{
    AMatchGameState* S = GS(); if (!S) return;

    

    FMatchSummary Sum;
    Sum.ScoreP1      = S->ScoreP1;
    Sum.ScoreP2      = S->ScoreP2;
    Sum.RoundsPlayed = S->CurrentRound;

    // Collect all surviving units (both teams), with model counts
    for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
    {
        AUnitBase* U = *It;
        if (!U || U->ModelsCurrent <= 0) continue;

        FSurvivorEntry E;
        E.UnitName      = U->UnitName;
        E.ModelsCurrent = U->ModelsCurrent;
        E.ModelsMax     = U->ModelsMax;

        const ATabletopPlayerState* TPS = U->OwningPS ? Cast<ATabletopPlayerState>(U->OwningPS) : nullptr;
        E.TeamNum = TPS ? TPS->TeamNum : 0;

        Sum.Survivors.Add(E);
    }

    S->SetGlobalSelected(nullptr);
    S->SetGlobalTarget(nullptr);
	S->Multicast_ApplySelectionVis(S->SelectedUnitGlobal, S->TargetUnitGlobal);

    S->FinalSummary = Sum;
    S->bShowSummary = true;
    S->Phase        = EMatchPhase::EndGame;

    S->OnDeploymentChanged.Broadcast();
    S->ForceNetUpdate();
}

void AMatchGameMode::FinalizePlayerJoin(APlayerController* PC)
{
    if (!PC) return;

    if (AMatchGameState* S = GS())
    {
    	
        ATabletopPlayerState* TPS = PC->GetPlayerState<ATabletopPlayerState>();
        if (!TPS)
        {
            UE_LOG(LogTemp, Error, TEXT("FinalizePlayerJoin: PlayerState is not ATabletopPlayerState."));
            return;
        }

        if (!S->P1)                    S->P1 = TPS;
        else if (!S->P2 && S->P1 != TPS) S->P2 = TPS;

        if (S->P1 && S->P2 && !S->bTeamsAndTurnsInitialized)
        {
            CopyRostersFromPlayerStates();

            const bool bP1First = FMath::RandBool();
            S->CurrentDeployer = bP1First ? static_cast<APlayerState*>(S->P1)
                                          : static_cast<APlayerState*>(S->P2);

            S->P1->TeamNum = bP1First ? 1 : 2;
            S->P2->TeamNum = bP1First ? 2 : 1;

            S->Phase = EMatchPhase::Deployment;
            S->bTeamsAndTurnsInitialized = true;

            S->P1->ForceNetUpdate();
            S->P2->ForceNetUpdate();
            S->ForceNetUpdate();
        }

        S->OnDeploymentChanged.Broadcast();

    	if (AMatchPlayerController* MPC = Cast<AMatchPlayerController>(PC))
    	{
    		MPC->Client_KickUIRefresh();
    	}

    	if (S->P1 && S->P2)
    	{
    		ApplyCoverPresetsFromTableOnce();
    		RebroadcastCoverPresetsReliable();
    		
    		bCoverPresetsApplied = true;
    	}
    }
}

void AMatchGameMode::ApplyCoverPresetsFromTableOnce()
{
	if (!HasAuthority()) return;

	UWorld* W = GetWorld();
	AMatchGameState* GS = GetGameState<AMatchGameState>();
	if (!W || !GS || !CoverPresetsTable)
	{
		UE_LOG(LogCoverNet, Warning, TEXT("[GM] Missing world/state/table, skipping cover preset apply."));
		return;
	}

	GS->CoverPresetsTable = CoverPresetsTable; // debug/visibility

	TArray<ACoverVolume*> Covers;
	for (TActorIterator<ACoverVolume> It(W); It; ++It) if (*It) Covers.Add(*It);
	if (Covers.Num() == 0)
	{
		UE_LOG(LogCoverNet, Warning, TEXT("[GM] No ACoverVolume found; will try again when levels stream in."));
		return;
	}

	const EFaction F1 = GS->P1 ? GS->P1->SelectedFaction : EFaction::None;
	const EFaction F2 = GS->P2 ? GS->P2->SelectedFaction : EFaction::None;

	auto FactionForVolume = [&](ACoverVolume* CV)->EFaction
	{
		if (!CV || !W || !GS) return EFaction::None;

		// nearest enabled zone by actor world location
		ADeploymentZone* Closest = nullptr;
		float BestD2 = FLT_MAX;
		const FVector L = CV->GetActorLocation();

		for (TActorIterator<ADeploymentZone> It(W); It; ++It)
		{
			ADeploymentZone* Z = *It;
			if (!Z || !Z->bEnabled) continue;

			const float D2 = FVector::DistSquared(L, Z->GetActorLocation());
			if (D2 < BestD2) { BestD2 = D2; Closest = Z; }
		}

		if (!Closest) return EFaction::None;

		int32 TeamNum = 0;
		switch (Closest->CurrentOwner)
		{
		case EDeployOwner::Team1: TeamNum = 1; break;
		case EDeployOwner::Team2: TeamNum = 2; break;
		default:                  TeamNum = 0; break; // Either → choose default or None
		}

		const ATabletopPlayerState* TPS =
			TeamNum ? Cast<ATabletopPlayerState>(GS->GetPSForTeam(TeamNum)) : nullptr;

		return TPS ? TPS->SelectedFaction : EFaction::None;
	};

	GS->CoverAssignments.Reset(Covers.Num());
	int32 Applied = 0;

	for (ACoverVolume* CV : Covers)
	{
		if (!CV) continue;

		const EFaction Fac = FactionForVolume(CV);
		const bool bPreferLow = CV->bPreferLowCover;

		const FName RowName = PickRowForFaction(CoverPresetsTable, Fac, bPreferLow);
		if (RowName.IsNone())
		{
			UE_LOG(LogCoverNet, Warning, TEXT("[GM] No preset row for CV=%s Fac=%d PreferLow=%d"),
				*GetNameSafe(CV), (int32)Fac, bPreferLow?1:0);
			continue;
		}

		const FCoverPresetRow* Row =
			CoverPresetsTable->FindRow<FCoverPresetRow>(RowName, TEXT("ApplyCoverPresetsFromTableOnce"));
		if (!Row) continue;

		const float Thr = FMath::Clamp(Row->HighToLowPct, 0.f, 1.f);
		const float Eps = 0.005f;
		const float StartPct = (bPreferLow && Row->LowCoverMesh)
			? FMath::Clamp(Thr - Eps, 0.f, 1.f)
			: FMath::Clamp(Row->StartHealthPct, 0.f, 1.f);

		FCoverRowAssignment A;
		A.Volume       = CV;
		A.RowName      = RowName;           // optional debug
		A.bPreferLow   = bPreferLow ? 1 : 0;
		A.HighMesh     = Row->HighCoverMesh;
		A.LowMesh      = Row->LowCoverMesh;
		A.NoneMesh     = Row->NoCoverMesh;
		A.StartPct     = StartPct;
		A.ThresholdPct = Thr;

		GS->CoverAssignments.Add(A);
		++Applied;
	}

	// Apply locally (server) and replicate (clients use OnRep to apply)
	GS->OnRep_CoverAssignments();
	GS->ForceNetUpdate();

	UE_LOG(LogCoverNet, Display, TEXT("[GM] Cover presets assigned to %d volumes."), Applied);
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
            if (!U || !U->OwningPS) continue;

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

    GS->OnDeploymentChanged.Broadcast();
}

void AMatchGameMode::Handle_ExecuteAction(AMatchPlayerController* PC, AUnitBase* Unit, FName ActionId, const FActionRuntimeArgs& Args)
{
    if (!HasAuthority() || !PC || !Unit) return;

    AMatchGameState* S = GS();
    if (!S || S->Phase != EMatchPhase::Battle) return;

    // Turn ownership
    if (PC->PlayerState != S->CurrentTurn) return;
    if (Unit->OwningPS != PC->PlayerState) return;

    // Find the action
    UUnitAction* Action = nullptr;
    for (UUnitAction* A : Unit->GetActions())
        if (A && A->Desc.ActionId == ActionId) { Action = A; break; }
    if (!Action) return;

    // Phase gate + AP inside CanExecute
    if (!Action->CanExecute(Unit, Args)) return;

    // Broadcast before
    if (UAbilityEventSubsystem* Bus = AbilityBus(GetWorld()))
    {
        FAbilityEventContext Ctx;
        Ctx.Event   = ECombatEvent::Ability_Activated;
        Ctx.GM      = this;
        Ctx.GS      = S;
        Ctx.Source  = Unit;
        Ctx.Target  = Args.TargetUnit;
        Ctx.WorldPos= Args.TargetLocation;
        Bus->Broadcast(Ctx);
    }

    // Execute (action pays AP internally)
    Action->Execute(Unit, Args);
    
	if (!Action->LeavesLingeringState())
	{
		if (AMatchGameMode* GM = GetWorld()->GetAuthGameMode<AMatchGameMode>())
		{
			GM->Emit(ECombatEvent::Ability_Expired, Unit);
		}
	}

    // You can optionally broadcast a "Post" event if needed
}
