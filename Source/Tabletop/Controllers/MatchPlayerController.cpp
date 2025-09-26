
#include "MatchPlayerController.h"


#include "EngineUtils.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/DecalComponent.h"
#include "Components/TextBlock.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Tabletop/LibraryHelpers.h"
#include "Tabletop/MatchSummaryWidget.h"
#include "Tabletop/TurnContextWidget.h"
#include "Tabletop/Actors/UnitAction.h"
#include "Tabletop/Actors/UnitBase.h"
#include "Tabletop/Gamemodes/MatchGameMode.h"
#include "Math/RotationMatrix.h"


AMatchGameState* AMatchPlayerController::GS() const
{
	return GetWorld() ? GetWorld()->GetGameState<AMatchGameState>() : nullptr;
}

void AMatchPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// UI input
	bShowMouseCursor = true;
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);

	// Bind now or retry until GS exists
	//TryBindToGameState();
	if (!GS())
	{
		GetWorldTimerManager().SetTimer(
			BindGSTimer, this, &AMatchPlayerController::TryBindToGameState, 0.5f, true, 0.5f);
	}

	if (AMatchGameState* S = GS())
	{
		S->OnDeploymentChanged.Broadcast();
	}

	OnSelectedChanged.AddDynamic(this, &AMatchPlayerController::HandleSelectedChanged_Internal);
	CacheTurnContextIfNeeded();
	UpdateTurnContextVisibility();

	RefreshPhaseUI();
}

void AMatchPlayerController::Server_ExecuteAction_Implementation(AUnitBase* Unit, FName ActionId, FActionRuntimeArgs Args)
{
	if (AMatchGameMode* GM = GetWorld()->GetAuthGameMode<AMatchGameMode>())
	{
		Args.InstigatorPC = this; // stamp server-side for safety
		GM->Handle_ExecuteAction(this, Unit, ActionId, Args);
	}
}

void AMatchPlayerController::SetSelectedUnit(AUnitBase* NewSel)
{
	if (SelectedUnit == NewSel) return;
	if (IsValid(SelectedUnit)) SelectedUnit->OnDeselected();
	SelectedUnit = NewSel;
	if (IsValid(SelectedUnit)) SelectedUnit->OnSelected();

	OnSelectedChanged.Broadcast(SelectedUnit);
}

void AMatchPlayerController::CacheTurnContextIfNeeded(bool bForce)
{
	if (!bForce && TurnContextRef) return;

	TArray<UUserWidget*> Hits;
	// TopLevelOnly=false so we can find it even if it lives under SetupWidget / GameplayWidget
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Hits, UTurnContextWidget::StaticClass(), /*TopLevelOnly*/false);
	TurnContextRef = Hits.Num() ? Cast<UTurnContextWidget>(Hits[0]) : nullptr;
}

// --- central visibility rule ---
void AMatchPlayerController::UpdateTurnContextVisibility()
{
	CacheTurnContextIfNeeded();

	if (!TurnContextRef) return; // nothing to show/hide yet

	AMatchGameState* S = GS();
	const bool bBattle = (S && S->Phase == EMatchPhase::Battle);
	const bool bEnd    = (S && S->Phase == EMatchPhase::EndGame && S->bShowSummary);
	const bool bMyTurn = (bBattle && S->CurrentTurn == PlayerState);
	const bool bHaveSel = (SelectedUnit != nullptr);

	const bool bShow = bBattle && !bEnd && bMyTurn && bHaveSel;

	TurnContextRef->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible
										: ESlateVisibility::Collapsed);
}

// react whenever our selection changes
void AMatchPlayerController::HandleSelectedChanged_Internal(AUnitBase* /*NewSel*/)
{
	UpdateTurnContextVisibility();
}

void AMatchPlayerController::Client_KickUIRefresh_Implementation()
{
	// Ensure we’re bound and the correct widget is visible for the current phase
	TryBindToGameState();
	OnPhaseSignalChanged(); // calls RefreshPhaseUI()
	CacheTurnContextIfNeeded(true);    // UI might have been rebuilt; reacquire
	UpdateTurnContextVisibility();

	// If the Deployment widget exists, force one pull-based refresh
	if (DeploymentWidgetInstance)
	{
		DeploymentWidgetInstance->RebuildUnitPanels();
		DeploymentWidgetInstance->RefreshFromState();
	}
}

void AMatchPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	check(InputComponent);

	// Create two actions in Project Settings → Input: "LeftClick" bound to LeftMouseButton, "RightClick" to RightMouseButton
	InputComponent->BindAction("LeftClick",  IE_Pressed, this, &AMatchPlayerController::OnLeftClick);
	InputComponent->BindAction("RightClick", IE_Pressed, this, &AMatchPlayerController::OnRightClickCancel);

	InputComponent->BindAction("UnStuck", IE_Pressed, this, &AMatchPlayerController::OnUnStuckPressed);

}

void AMatchPlayerController::BeginDeployForUnit(FName UnitId, int32 InWeaponIndex) // overload
{
	PendingDeployUnit  = UnitId;
	PendingWeaponIndex = InWeaponIndex;
	StartDeployCursorFeedback();
}

void AMatchPlayerController::OnRightClickCancel()
{
	AMatchGameState* S = GS();
	if (!S) return;

	if (S->Phase == EMatchPhase::Deployment)
	{
		PendingDeployUnit = NAME_None;
		StopDeployCursorFeedback();
		return;
	}

	if (bTargetMode && SelectedUnit)
	{
		PendingGroundActionId = NAME_None;
		PendingActionUnit     = nullptr;

		Server_CancelPreview(SelectedUnit);
		ExitTargetMode();          // <-- was: bTargetMode = false;
		return;
	}

	ClearSelection();
}

void AMatchPlayerController::OnLeftClick()
{
    AMatchGameState* S = GS(); 
    if (!S) return;

    // ---------- Deployment unchanged ----------
    if (S->Phase == EMatchPhase::Deployment)
    {
        if (PendingDeployUnit == NAME_None) return;

        FHitResult Hit;
        if (!TraceGround_Deploy(Hit)) return;

        const FRotator YawOnly(0.f, GetControlRotation().Yaw, 0.f);
        const FTransform Where(YawOnly, Hit.ImpactPoint);

        Server_RequestDeploy(PendingDeployUnit, Where, PendingWeaponIndex);
        PendingDeployUnit  = NAME_None;
        PendingWeaponIndex = INDEX_NONE;
        StopDeployCursorFeedback();
        return;
    }

    // ---------- From here on, ONLY act during your Battle turn ----------
    const bool bMyTurn = (S->Phase == EMatchPhase::Battle) && (S->CurrentTurn == PlayerState);
    if (!bMyTurn)
    {
        // Optional: clear any armed actions/target mode so UI doesn't lie
        PendingGroundActionId = NAME_None;
        PendingActionUnit     = nullptr;
        bTargetMode           = false;
        return;
    }

    // --- SAFETY: if we’re in Shoot, never stay in friendly-only target mode (Field Medic leftovers)
    if (S->TurnPhase == ETurnPhase::Shoot && bFriendlyTargetMode)
    {
        // Drop the friendly gate so enemy clicks will work below.
        bFriendlyTargetMode = false;
        // keep bTargetMode as-is so user remains in a targeting flow if they were.
    }

    // ---------- Phase-agnostic (but only when it's your turn): try unit click first ----------
    if (AUnitBase* Clicked = TraceUnit())
    {
        const bool bClickedFriendly = (Clicked->OwningPS == PlayerState);
        const bool bHaveSelFriendly = (SelectedUnit && SelectedUnit->OwningPS == PlayerState);

        // --- Target mode branch (works in Move or Shoot) ---
        if (bTargetMode && bHaveSelFriendly)
        {
            if (bFriendlyTargetMode)
            {
                // Friendly targeting (Field Medic, etc.)
                if (bClickedFriendly)
                {
                    Server_SelectFriendly(SelectedUnit, Clicked);
                    return;
                }

                // If we somehow still have friendly-mode active but the player clicks an enemy
                // while in Shoot, treat it as a mode switch and proceed with enemy selection.
                if (S->TurnPhase == ETurnPhase::Shoot && !bClickedFriendly)
                {
                    bFriendlyTargetMode = false;                 // drop the friendly-only gate
                    Server_SelectTarget(SelectedUnit, Clicked);  // replace server target with enemy
                    return;
                }

                // Other phases: still ignore enemy clicks while in friendly mode.
                return;
            }

            // Enemy targeting (Shoot) — only accept enemy clicks during Shoot phase
            if (S->TurnPhase == ETurnPhase::Shoot && !bClickedFriendly)
            {
                Server_SelectTarget(SelectedUnit, Clicked);
            }
            // Ignore friendly clicks while aiming at enemies
            return;
        }

        // --- Normal selection when NOT in any target mode ---
        if (bClickedFriendly)
        {
            SelectUnit(Clicked);
            return;
        }
    }

    // ---------- Pending ground-required action? ----------
    if (PendingGroundActionId != NAME_None && SelectedUnit && SelectedUnit->OwningPS == PlayerState)
    {
        FHitResult Hit;
        if (TraceGround_Battle(Hit))
        {
            FActionRuntimeArgs Args;
            Args.TargetLocation = Hit.ImpactPoint;
            Server_ExecuteAction(SelectedUnit, PendingGroundActionId, Args);
            PendingGroundActionId = NAME_None;
            PendingActionUnit     = nullptr;
        }
        return;
    }

    // ---------- Move phase ----------
    if (S->TurnPhase == ETurnPhase::Move)
    {
        // (keep your current behavior / optional hybrid direct-move)
        return;
    }

    // ---------- Shoot phase (not in target mode) ----------
    // Nothing else to do here; selecting friendlies was handled above.
}

void AMatchPlayerController::Client_ShowSummary_Implementation()
{
	if (!SummaryWidgetInstance && SummaryWidgetClass)
	{
		SummaryWidgetInstance = CreateWidget<UMatchSummaryWidget>(this, SummaryWidgetClass);
	}
	if (SummaryWidgetInstance)
	{
		if (AMatchGameState* S = GS())
		{
			SummaryWidgetInstance->AddToViewport(1000);
			SummaryWidgetInstance->RefreshFromState(S);
		}
	}
}

void AMatchPlayerController::Client_HideSummary_Implementation()
{
	if (SummaryWidgetInstance)
	{
		SummaryWidgetInstance->RemoveFromParent();
	}
}

// Return to menu from server (works for listen/clients)
void AMatchPlayerController::Server_ExitToMainMenu_Implementation()
{
	// Option A: tell client to return to front end menu
	ClientReturnToMainMenuWithTextReason(FText::FromString(TEXT("Match ended")));
	// Option B (standalone): UGameplayStatics::OpenLevel(GetWorld(), FName("MainMenu"));
}

void AMatchPlayerController::Client_OnUnitMoved_Implementation(AUnitBase* Unit, float SpentTTIn, float NewBudgetTTIn)
{
	if (SelectedUnit == Unit)
	{
		SelectUnit(nullptr);
	}
	
	ExitTargetMode();
	UpdateTurnContextVisibility();

#if !(UE_BUILD_SHIPPING)
	if (GEngine && Unit)
	{
		GEngine->AddOnScreenDebugMessage(
			-1, 3.f, FColor::Silver,
			FString::Printf(TEXT("[Client] Deselect after move: %s  Spent=%.1f TT-in  Budget=%.1f TT-in"),
				*Unit->GetName(), SpentTTIn, NewBudgetTTIn));
	}
#endif
}

void AMatchPlayerController::Client_OnMoveDenied_OverBudget_Implementation(AUnitBase* Unit, float AttemptTTIn, float BudgetTTIn)
{
	// Only clear if we were trying to move this unit
	if (SelectedUnit == Unit)
	{
		SelectUnit(nullptr);   // your helper that broadcasts OnSelectedChanged
	}
	ExitTargetMode();          // just in case
	UpdateTurnContextVisibility();

	#if !(UE_BUILD_SHIPPING)
	if (GEngine && Unit)
	{
		GEngine->AddOnScreenDebugMessage(
			-1, 3.f, FColor::Red,
			FString::Printf(TEXT("[MoveDenied] %s  Attempt=%.1f TT-in  Budget=%.1f TT-in"),
				*Unit->GetName(), AttemptTTIn, BudgetTTIn));
	}
#endif
}

void AMatchPlayerController::Server_RequestAdvance_Implementation(AUnitBase* Unit)
{
	if (AMatchGameMode* GM = GetWorld()->GetAuthGameMode<AMatchGameMode>())
	{
		GM->Handle_AdvanceUnit(this, Unit);
	}
}

void AMatchPlayerController::Client_OnAdvanced_Implementation(AUnitBase* Unit, int32 Bonus)
{
	if (!IsValid(Unit)) return;

	// Kick the UI & ring right now on THIS client
	Unit->NotifyMoveChanged();     // fires bound widget to UpdateMovementUI + UpdateRangePreview
	Unit->RefreshRangeIfActive();  // safety net to refresh the decal if it’s already visible
}

void AMatchPlayerController::Client_OnOverwatchArmed_Implementation(AUnitBase* Unit)
{
	if (!IsValid(Unit)) return;
	Unit->HideRangePreview();             // drop Shoot/Move ring
	Unit->UpdateOverwatchIndicatorLocal(); // show OW ring (armed)
}

void AMatchPlayerController::Server_SetGlobalSelectedUnit_Implementation(AUnitBase* NewSel)
{
	if (AMatchGameState* S = GS())
	{
		if (S->CurrentTurn == PlayerState)
		{
			if (!NewSel || NewSel->OwningPS == PlayerState)
				S->SetGlobalSelected(NewSel);
		}
	}
}

bool AMatchPlayerController::TraceDeployLocation(FHitResult& OutHit) const
{
	const ETraceTypeQuery TraceType =
		UEngineTypes::ConvertToTraceType(DeployTraceChannel.GetValue());
	
	return GetHitResultUnderCursorByChannel(TraceType, /*bTraceComplex*/ false, OutHit);
}

void AMatchPlayerController::TryBindToGameState()
{
	AMatchGameState* GSNow = GS();
	if (!GSNow) return;

	if (BoundGS.Get() != GSNow)
	{
		// Unbind from old GS (if any)
		if (BoundGS.IsValid())
		{
			BoundGS->OnDeploymentChanged.RemoveDynamic(this, &AMatchPlayerController::OnPhaseSignalChanged);
		}

		// Bind to the new GS
		GSNow->OnDeploymentChanged.AddDynamic(this, &AMatchPlayerController::OnPhaseSignalChanged);
		BoundGS = GSNow;

		// Immediate refresh on rebind (catches phase already changed)
		OnPhaseSignalChanged();
	}
}
void AMatchPlayerController::OnPhaseSignalChanged()
{
	RefreshPhaseUI();
	UpdateTurnContextVisibility();
}

void AMatchPlayerController::RefreshPhaseUI()
{
	AMatchGameState* State = GS();

	UpdateTurnContextVisibility();
	
	if (!State)
	{
		ShowWidgetTyped(DeploymentWidgetInstance, DeploymentWidgetClass, false);
		ShowWidgetTyped(GameplayWidgetInstance,   GameplayWidgetClass,   false);
		Client_HideSummary();
		return;
	}

	const bool bDeployment = (State->Phase == EMatchPhase::Deployment);
	const bool bBattle     = (State->Phase == EMatchPhase::Battle);
	const bool bEnd        = (State->Phase == EMatchPhase::EndGame) && State->bShowSummary;

	ShowWidgetTyped(DeploymentWidgetInstance, DeploymentWidgetClass, bDeployment);
	ShowWidgetTyped(GameplayWidgetInstance,   GameplayWidgetClass,   bBattle && !bEnd);

	if (bEnd)
		Client_ShowSummary();
	else
		Client_HideSummary();

	// Keep simple UI mode
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetShowMouseCursor(true);
	bShowMouseCursor = true;
	SetInputMode(Mode);

}

bool AMatchPlayerController::TraceGround(FHitResult& OutHit, TEnumAsByte<ECollisionChannel> Channel) const
{
	const ETraceTypeQuery TraceType = UEngineTypes::ConvertToTraceType(Channel.GetValue());
	return GetHitResultUnderCursorByChannel(TraceType, /*bTraceComplex*/ false, OutHit);
}


AUnitBase* AMatchPlayerController::TraceUnit() const
{
	FHitResult Hit;
	const ETraceTypeQuery TraceType =
		UEngineTypes::ConvertToTraceType(UnitTraceChannel.GetValue());
	if (GetHitResultUnderCursorByChannel(TraceType, /*bTraceComplex*/ false, Hit))
	{
		return Cast<AUnitBase>(Hit.GetActor());
	}
	return nullptr;
}

void AMatchPlayerController::SelectUnit(AUnitBase* U)
{
	if (SelectedUnit == U) return;

	if (SelectedUnit) SelectedUnit->OnDeselected();
	SelectedUnit = U;
	if (SelectedUnit) SelectedUnit->OnSelected();

	OnSelectedChanged.Broadcast(SelectedUnit);
	SetSelectedUnit(U);

	Server_SetGlobalSelectedUnit(U);
	UpdateTurnContextVisibility();
}

void AMatchPlayerController::ClearSelection()
{
	if (SelectedUnit) SelectedUnit->OnDeselected();
	SelectedUnit = nullptr;
	OnSelectedChanged.Broadcast(nullptr);
	SetSelectedUnit(nullptr);
	UpdateTurnContextVisibility();

	Server_SetGlobalSelectedUnit(nullptr);
	UpdateTurnContextVisibility();
}


void AMatchPlayerController::Client_KickPhaseRefresh_Implementation()
{
	TryBindToGameState();
	OnPhaseSignalChanged();
}

void AMatchPlayerController::Client_ClearSelectionAfterConfirm_Implementation()
{
	ClearSelection();
	ExitTargetMode();
}

void AMatchPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (BoundGS.IsValid())
	{
		BoundGS->OnDeploymentChanged.RemoveDynamic(this, &AMatchPlayerController::OnPhaseSignalChanged);
		BoundGS.Reset();
	}
	Super::EndPlay(Reason);
}

void AMatchPlayerController::Server_RequestDeploy_Implementation(FName UnitId, const FTransform& Where, int32 WeaponIndex)
{
	if (AMatchGameMode* GM = GetWorld()->GetAuthGameMode<AMatchGameMode>())
	{
		GM->HandleRequestDeploy(this, UnitId, Where, WeaponIndex);
	}
}

void AMatchPlayerController::Server_StartBattle_Implementation()
{
	if (AMatchGameMode* GM = GetWorld()->GetAuthGameMode<AMatchGameMode>())
		GM->HandleStartBattle(this);
}

void AMatchPlayerController::Server_EndPhase_Implementation()
{
	if (AMatchGameMode* GM = GetWorld()->GetAuthGameMode<AMatchGameMode>())
		GM->HandleEndPhase(this);
}

void AMatchPlayerController::Server_MoveUnit_Implementation(AUnitBase* Unit, FVector Dest)
{
	if (AMatchGameMode* GM = GetWorld()->GetAuthGameMode<AMatchGameMode>())
		GM->Handle_MoveUnit(this, Unit, Dest);
}

void AMatchPlayerController::Server_SelectTarget_Implementation(AUnitBase* Attacker, AUnitBase* Target)
{
	if (AMatchGameMode* GM = GetWorld()->GetAuthGameMode<AMatchGameMode>())
		GM->Handle_SelectTarget(this, Attacker, Target);
}

void AMatchPlayerController::Server_CancelPreview_Implementation(AUnitBase* Attacker)
{
	if (AMatchGameMode* GM = GetWorld()->GetAuthGameMode<AMatchGameMode>())
	{
		GM->Handle_CancelPreview(this, Attacker);
	}
}

void AMatchPlayerController::Server_SelectFriendly_Implementation(AUnitBase* Attacker, AUnitBase* Target)
{
	if (AMatchGameMode* GM = GetWorld()->GetAuthGameMode<AMatchGameMode>())
	{
		GM->Handle_SelectFriendly(this, Attacker, Target);
	}
}

void AMatchPlayerController::Server_ConfirmShoot_Implementation(AUnitBase* Attacker, AUnitBase* Target)
{
	if (AMatchGameMode* GM = GetWorld()->GetAuthGameMode<AMatchGameMode>())
		GM->Handle_ConfirmShoot(this, Attacker, Target);
}

void AMatchPlayerController::OnUnStuckPressed()
{
	// If we're the server (listen host) we can do it immediately
	if (HasAuthority())
	{
		TeleportPawnToFirstPlayerStart();
	}
	else
	{
		// Remote client → ask server to do it so it replicates
		Server_UnStuck();
	}
}

void AMatchPlayerController::Server_UnStuck_Implementation()
{
	TeleportPawnToFirstPlayerStart();
}

bool AMatchPlayerController::TeleportPawnToFirstPlayerStart()
{
	const UWorld* World = GetWorld();
	if (!World) return false;

	// Find the first PlayerStart in the world
	const APlayerStart* PlayerStart = nullptr;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		PlayerStart = *It;
		break;
	}

	if (!PlayerStart)
	{
		UE_LOG(LogTemp, Warning, TEXT("UnStuck: No APlayerStart found in the world."));
		return false;
	}

	APawn* P = GetPawn();
	if (!P)
	{
		UE_LOG(LogTemp, Warning, TEXT("UnStuck: Controller has no Pawn to teleport."));
		return false;
	}

	// Optional: stop movement so we don't keep sliding after teleport
	if (ACharacter* C = Cast<ACharacter>(P))
	{
		if (UCharacterMovementComponent* Move = C->GetCharacterMovement())
		{
			Move->StopMovementImmediately();
		}
	}

	const FVector DestLoc   = PlayerStart->GetActorLocation();
	const FRotator DestRot  = PlayerStart->GetActorRotation();

	// Force the teleport even if overlapping (since it's an "unstuck")
	const bool bSuccess = P->TeleportTo(DestLoc, DestRot, /*bIsATest*/ false, /*bNoCheck*/ true);

	if (bSuccess)
	{
		// Keep controller yaw in sync with the new facing
		SetControlRotation(DestRot);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("UnStuck: TeleportTo failed."));
	}

	return bSuccess;
}

void AMatchPlayerController::StartDeployCursorFeedback()
{
	DefaultCursorBackup = CurrentMouseCursor;

	if (DeployPreviewDecal)
	{
		// Make sure any previous fade is fully disabled
		DeployPreviewDecal->SetFadeOut(0.f, 0.f, false);
		DeployPreviewDecal->SetFadeScreenSize(0.00001f);   // effectively “never fade by size”
		DeployPreviewDecal->SetHiddenInGame(false);
		return;
	}

	if (!DeployPreviewDecalMaterial) return;

	DeployPreviewDecal = UGameplayStatics::SpawnDecalAtLocation(
		GetWorld(),
		DeployPreviewDecalMaterial,
		DeployPreviewDecalSize,
		FVector::ZeroVector,
		FRotator::ZeroRotator);

	if (DeployPreviewDecal)
	{
		// Kill both fade mechanisms
		DeployPreviewDecal->SetFadeOut(0.f, 0.f, false);
		DeployPreviewDecal->SetFadeScreenSize(0.00001f);

		// Make sure it draws on top of other decals if needed
		DeployPreviewDecal->SetSortOrder(1000);

		// Give the decal some depth so rapid normal changes don’t clip it
		FVector S = DeployPreviewDecal->DecalSize;
		S.X = FMath::Max(S.X, 256.f);              // depth along the projection axis
		DeployPreviewDecal->DecalSize = S;

		DeployPreviewDecal->SetHiddenInGame(true);
	}
}

void AMatchPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Only drive the deploy preview during Deployment AND when a unit is pending
	AMatchGameState* S = GS();
	if (!S || S->Phase != EMatchPhase::Deployment)
	{
		// If we left deployment mid-preview, clean up
		if (DeployPreviewDecal)
		{
			StopDeployCursorFeedback();
		}
		return;
	}

	if (PendingDeployUnit == NAME_None)
	{
		// No active deploy → hide the decal if it exists
		if (DeployPreviewDecal)
		{
			DeployPreviewDecal->SetHiddenInGame(true);
		}
		return;
	}

	// Ensure the decal exists (StartDeployCursorFeedback spawns it)
	if (!DeployPreviewDecal)
	{
		StartDeployCursorFeedback();
		if (!DeployPreviewDecal) return;
	}

	FHitResult Hit;
	const bool bHit = TraceDeployLocation(Hit);

	bool bValid = false;
	if (bHit)
	{
		// Use your existing helper for validity
		bValid = ULibraryHelpers::IsDeployLocationValid(this, this, Hit.ImpactPoint);
	}

	// Cursor feedback (optional, keep your existing cursor API)
	SetCursorType(bValid ? EMouseCursor::Crosshairs : EMouseCursor::SlashedCircle);

	// Update/Hide decal
	if (bHit)
	{
		// Face the surface normal
		const FRotator FaceSurface = UKismetMathLibrary::MakeRotFromX(-Hit.ImpactNormal);
		DeployPreviewDecal->SetHiddenInGame(false);
		DeployPreviewDecal->SetWorldLocation(Hit.ImpactPoint);
		DeployPreviewDecal->SetWorldRotation(FaceSurface); // set each frame in case the surface changes
	}
	else
	{
		DeployPreviewDecal->SetHiddenInGame(true);
	}
}

void AMatchPlayerController::StopDeployCursorFeedback()
{
	SetCursorType(BackedUpCursor);

	// Destroy & null (you could also keep & hide if you prefer pooling)
	if (DeployPreviewDecal)
	{
		DeployPreviewDecal->DestroyComponent();
		DeployPreviewDecal = nullptr;
	}
}

void AMatchPlayerController::UpdateDeployCursor()
{
	if (PendingDeployUnit == NAME_None)
	{
		StopDeployCursorFeedback();
		return;
	}

	FHitResult Hit;
	const bool bHit = TraceDeployLocation(Hit);

    bool bValid = false;
    if (bHit)
    {
            // Use your helper or direct zone call
            bValid = ULibraryHelpers::IsDeployLocationValid(this, this, Hit.ImpactPoint);
            // or:
            // const int32 Slot = ADeploymentZone::ResolvePlayerSlot(this);
            // bValid = ADeploymentZone::IsLocationAllowedForPlayer(GetWorld(), Slot, Hit.ImpactPoint);
    }

    SetCursorType(bValid ? EMouseCursor::Crosshairs : EMouseCursor::SlashedCircle);

    if (!DeployPreviewDecalMaterial)
    {
    	return;
    }

    if (bHit)
    {
    	const FRotator FaceSurface = UKismetMathLibrary::MakeRotFromX(-Hit.ImpactNormal);
    	const float TextureRollDeg = 0.f; // try 90 or -90 if your texture is rotated
    	FRotator DecalRot = FaceSurface;
    	DecalRot.Roll += TextureRollDeg;
    	
    	if (!DeployPreviewDecal)
    	{
    		DeployPreviewDecal = UGameplayStatics::SpawnDecalAtLocation(
				GetWorld(),
				DeployPreviewDecalMaterial,
				DeployPreviewDecalSize,      // X = depth, Y = width, Z = height
				Hit.ImpactPoint,
				DecalRot);

    		if (DeployPreviewDecal)
    		{
    			DeployPreviewDecal->SetFadeScreenSize(0.f);
    		}
    	}
    	if (DeployPreviewDecal)
    	{
    		DeployPreviewDecal->SetHiddenInGame(false);
    		DeployPreviewDecal->SetWorldLocation(Hit.ImpactPoint);
    		DeployPreviewDecal->SetWorldRotation(DecalRot); // set once
    	}
    }
	
    else if (DeployPreviewDecal)
    {
            DeployPreviewDecal->SetHiddenInGame(true);
    }
}


void AMatchPlayerController::SetCursorType(EMouseCursor::Type Type)
{
	if (CurrentMouseCursor != Type)
	{
		CurrentMouseCursor = Type;
		bShowMouseCursor = true;
	}
}