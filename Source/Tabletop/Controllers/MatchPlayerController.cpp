
#include "MatchPlayerController.h"


#include "EngineUtils.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Tabletop/LibraryHelpers.h"
#include "Tabletop/MatchSummaryWidget.h"
#include "Tabletop/TurnContextWidget.h"
#include "Tabletop/Actors/UnitBase.h"
#include "Tabletop/Gamemodes/MatchGameMode.h"


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

	RefreshPhaseUI();
}

void AMatchPlayerController::SetSelectedUnit(AUnitBase* NewSel)
{
	if (SelectedUnit == NewSel) return;
	if (IsValid(SelectedUnit)) SelectedUnit->OnDeselected();
	SelectedUnit = NewSel;
	if (IsValid(SelectedUnit)) SelectedUnit->OnSelected();

	OnSelectedChanged.Broadcast(SelectedUnit);
}

void AMatchPlayerController::Client_KickUIRefresh_Implementation()
{
	// Ensure we’re bound and the correct widget is visible for the current phase
	TryBindToGameState();
	OnPhaseSignalChanged(); // calls RefreshPhaseUI()

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

void AMatchPlayerController::BeginDeployForUnit(FName UnitId)
{
	PendingDeployUnit = UnitId;
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

    // Battle
    if (bTargetMode && SelectedUnit)
    {
        // Cancel preview on server and exit target mode
        Server_CancelPreview(SelectedUnit);
        bTargetMode = false;
        return;
    }

    // Otherwise clear local selection
    ClearSelection();
}

void AMatchPlayerController::OnLeftClick()
{
    AMatchGameState* S = GS();
    if (!S) return;

    if (S->Phase == EMatchPhase::Deployment)
    {
    	if (PendingDeployUnit == NAME_None) return;

    	FHitResult Hit;
    	if (!TraceGround_Deploy(Hit)) return;

    	const FRotator YawOnly(0.f, GetControlRotation().Yaw, 0.f);
    	const FTransform Where(YawOnly, Hit.ImpactPoint);

    	Server_RequestDeploy(PendingDeployUnit, Where);
    	PendingDeployUnit = NAME_None;
    	StopDeployCursorFeedback();
    	return;
    }

    // ---- Battle routing ----
    const bool bMyTurn = (S->CurrentTurn == PlayerState);
    if (!bMyTurn)
    {
        // Not your turn: ignore clicks (or allow inspecting enemies if you want)
        return;
    }

    switch (S->TurnPhase)
    {
    case ETurnPhase::Move:
    	{
    		// If we already have a unit selected and it's ours…
    		if (SelectedUnit && SelectedUnit->OwningPS == PlayerState)
    		{
    			// First: did we click another friendly unit? If so, reselect instead of moving.
    			if (AUnitBase* ClickedUnit = TraceUnit())
    			{
    				const bool bFriendly = (ClickedUnit->OwningPS == PlayerState);
    				if (bFriendly && ClickedUnit != SelectedUnit)
    				{
    					SelectUnit(ClickedUnit);  // swaps selection; no move issued
    					return;
    				}
    			}

    			// Otherwise, treat as a ground click → attempt to move the selected unit
    			FHitResult Hit;
    			if (TraceGround_Battle(Hit))
    			{
    				Server_MoveUnit(SelectedUnit, Hit.ImpactPoint);
    				return;
    			}
    			// If ground trace failed, fall through to selection attempt below
    		}

    		// No selected unit yet (or selected isn’t ours): try selecting a friendly unit under cursor
    		if (AUnitBase* U = TraceUnit())
    		{
    			if (U->OwningPS == PlayerState)
    			{
    				SelectUnit(U);
    				return;
    			}
    		}
    		break;
    	}

    case ETurnPhase::Shoot:
    {
        // Entered target mode from UI? Then click enemy to set preview
        if (bTargetMode && SelectedUnit && SelectedUnit->OwningPS == PlayerState)
        {
            if (AUnitBase* Target = TraceUnit())
            {
                if (Target->OwningPS != PlayerState)
                {
                    Server_SelectTarget(SelectedUnit, Target);
                    // remain in target mode until Confirm or cancel
                }
            }
            return;
        }

        // Not in target mode: allow selecting one of your units
        if (AUnitBase* U = TraceUnit())
        {
            if (U->OwningPS == PlayerState)
                SelectUnit(U);
        }
        break;
    }
    default: break;
    }
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
	// Only clear if we were actually selecting that unit (safe guard)
	if (SelectedUnit == Unit)
	{
		// Prefer your existing helper so it broadcasts OnSelectedChanged etc.
		SelectUnit(nullptr);
	}

	// Just in case we were in any special mode
	ExitTargetMode();

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

void AMatchPlayerController::Client_OnAdvanced_Implementation(AUnitBase* Unit, int32 BonusInches)
{
	// Update UI label nicely
	if (UTurnContextWidget* W = /* however you obtain your HUD widget */ nullptr)
	{
		if (W->AdvanceLabel)
		{
			W->AdvanceLabel->SetText(FText::FromString(
				FString::Printf(TEXT("+%d inches"), BonusInches)));
		}
		if (W->AdvanceBtn)
		{
			W->AdvanceBtn->SetIsEnabled(false);
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
}

void AMatchPlayerController::RefreshPhaseUI()
{
	AMatchGameState* State = GS();

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
}

void AMatchPlayerController::ClearSelection()
{
	if (SelectedUnit) SelectedUnit->OnDeselected();
	SelectedUnit = nullptr;
	OnSelectedChanged.Broadcast(nullptr);

	SetSelectedUnit(nullptr);
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

void AMatchPlayerController::Server_RequestDeploy_Implementation(FName UnitId, FTransform Where)
{
	if (AMatchGameMode* GM = GetWorld()->GetAuthGameMode<AMatchGameMode>())
		GM->HandleRequestDeploy(this, UnitId, Where);
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
	UWorld* World = GetWorld();
	if (!World) return false;

	// Find the first PlayerStart in the world
	APlayerStart* PlayerStart = nullptr;
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
    // Remember the current cursor so we can restore it later
    DefaultCursorBackup = CurrentMouseCursor;      // (or GetMouseCursor())
    // Drive at ~30 Hz to avoid per-frame calls
    GetWorldTimerManager().SetTimer(DeployCursorTimer, this, &AMatchPlayerController::UpdateDeployCursor, 0.033f, true);
}

void AMatchPlayerController::StopDeployCursorFeedback()
{
    GetWorldTimerManager().ClearTimer(DeployCursorTimer);
	SetCursorType(BackedUpCursor);   
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
}


void AMatchPlayerController::SetCursorType(EMouseCursor::Type Type)
{
	if (CurrentMouseCursor != Type)
	{
		CurrentMouseCursor = Type;
		bShowMouseCursor = true;
	}
}