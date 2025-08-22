
#include "MatchPlayerController.h"


#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Tabletop/LibraryHelpers.h"
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
    PendingDeployUnit = NAME_None;
	StopDeployCursorFeedback();
}

void AMatchPlayerController::OnLeftClick()
{
	if (PendingDeployUnit == NAME_None) return;

	FHitResult Hit;
	if (!TraceDeployLocation(Hit)) return;

	// Build a sensible transform (face camera yaw, keep upright)
	const FRotator YawOnly(0.f, GetControlRotation().Yaw, 0.f);
	FTransform Where(YawOnly, Hit.ImpactPoint);

	Server_RequestDeploy(PendingDeployUnit, Where);

	// Clear pending—server will validate turn & counts
	PendingDeployUnit = NAME_None;

	StopDeployCursorFeedback();
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
		return;
	}

	const bool bDeployment = (State->Phase == EMatchPhase::Deployment);
	const bool bBattle     = (State->Phase == EMatchPhase::Battle);

	ShowWidgetTyped(DeploymentWidgetInstance, DeploymentWidgetClass, bDeployment);
	ShowWidgetTyped(GameplayWidgetInstance,   GameplayWidgetClass,   bBattle);

	// Keep simple UI mode
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetShowMouseCursor(true);
	bShowMouseCursor = true;
	SetInputMode(Mode);

}

void AMatchPlayerController::Client_KickPhaseRefresh_Implementation()
{
	TryBindToGameState();
	OnPhaseSignalChanged();
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