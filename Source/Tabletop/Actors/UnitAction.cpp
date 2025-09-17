
#include "UnitAction.h"

#include "UnitBase.h"
#include "Tabletop/AbiltyEventSubsystem.h"
#include "Tabletop/UnitActionResourceComponent.h"
#include "Tabletop/Gamemodes/MatchGameMode.h"

static UAbilityEventSubsystem* AbilityBus(UWorld* W)
{
	if (!W) return nullptr;
	if (UGameInstance* GI = W->GetGameInstance())
		return GI->GetSubsystem<UAbilityEventSubsystem>();
	return nullptr;
}

bool UUnitAction::CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& /*Args*/) const
{
	if (!Unit) return false;
	const AMatchGameState* GS = Unit->GetWorld() ? Unit->GetWorld()->GetGameState<AMatchGameState>() : nullptr;
	if (!GS || GS->TurnPhase != Desc.Phase) return false;

	if (!Unit->CanUseActionNow(Desc)) return false;

	const auto* AP = Unit->FindComponentByClass<UUnitActionResourceComponent>();
	return AP && AP->CanPay(Desc.Cost);
}

bool UUnitAction::PayAP(AUnitBase* Unit) const
{
	if (!Unit) return false;
	UUnitActionResourceComponent* AP = Unit->FindComponentByClass<UUnitActionResourceComponent>();
	return AP && AP->Pay(Desc.Cost);
}

void UUnitAction::Execute_Implementation(AUnitBase* /*Unit*/, const FActionRuntimeArgs& /*Args*/)
{
	// Base does nothing
}

// DEFAULT IMPLEMENTATIONS FOR BASIC ACTIONS - HERE BECAUSE NO POINT MAKING MORE FILES
// -----------------------------------------------------------------------------------

// -------------- MOVE ACTION -----------------

bool UAction_Move::CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const
{
	//if (!Super::CanExecute_Implementation(Unit, Args)) return false; // we dont need to check Super since we handle unique cases in here that circumvent standard super flow
	if (!Unit || Unit->ModelsCurrent <= 0) return false;

	// must have *some* budget to move at all
	if (Unit->MoveBudgetInches <= 0.f) return false;
	
	if(Unit->bAdvancedThisTurn)
	{
		return true; // if we have advanced ignore Action Point check
	}
	
	const UUnitActionResourceComponent* AP = Unit->FindComponentByClass<UUnitActionResourceComponent>();
	const bool bCanPayAP   = (AP && AP->CanPay(Desc.Cost));
	return bCanPayAP;
}

void UAction_Move::Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args)
{
	if (!Unit || !Args.InstigatorPC) return;

	if(!Unit->bAdvancedThisTurn)
	{
		if (!PayAP(Unit)) return;
	}

	if (Unit->HasAuthority())
		Unit->BumpUsage(Desc);

	if (AMatchGameMode* GM = Unit->GetWorld()->GetAuthGameMode<AMatchGameMode>())
		GM->Handle_MoveUnit(Args.InstigatorPC, Unit, Args.TargetLocation);
}

// -------------- ADVANCE ACTION -----------------

bool UAction_Advance::CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const
{
	if (!Super::CanExecute_Implementation(Unit, Args)) return false;
	if (!Args.InstigatorPC) return false;
	if (Unit->bAdvancedThisTurn) return false;
	return true;
}

void UAction_Advance::Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args)
{
	if (!Unit || !Args.InstigatorPC) return;
	if (!PayAP(Unit)) return;

	if (Unit->HasAuthority() && Desc.NextPhaseAPCost > 0)
	{
		Unit->NextPhaseAPDebt = FMath::Clamp(Unit->NextPhaseAPDebt + Desc.NextPhaseAPCost, 0, 255);
		Unit->ForceNetUpdate();
	}
	
	if (AMatchGameMode* GM = Unit->GetWorld()->GetAuthGameMode<AMatchGameMode>())
	{
		GM->Handle_AdvanceUnit(Args.InstigatorPC, Unit);
	}
}

// -------------- SHOOT ACTION -----------------

bool UAction_Shoot::CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const
{
	// Manual common checks (don't call Super because it enforces AP strictly)
	if (!Unit) return false;

	const AMatchGameState* GS =
		Unit->GetWorld() ? Unit->GetWorld()->GetGameState<AMatchGameState>() : nullptr;
	if (!GS || GS->TurnPhase != Desc.Phase) return false;

	// Uses-per-Phase/Turn/Match gates
	if (!Unit->CanUseActionNow(Desc)) return false;

	// Per-action checks
	if (!Args.InstigatorPC) return false;
	//if (Unit->bHasShot)     return false; // TODO - RE-ENABLE IF WE WANT MAX 1 SHOOT PER TURN, FOR NOW WE KEEP IT OPEN FOR COOL SCENARIOS
	
	// Otherwise require enough AP
	const UUnitActionResourceComponent* AP = Unit->FindComponentByClass<UUnitActionResourceComponent>();
	return (AP && AP->CanPay(Desc.Cost));
}

void UAction_Shoot::Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args)
{
	if (!Unit || !Args.InstigatorPC) return;

	if (!PayAP(Unit)) return; // server-side charge; fails safely if short

	// Require target here (or enter target mode elsewhere)
	if (!Args.TargetUnit) return;

	// Bump usage counters on the server
	if (Unit->HasAuthority())
	{
		Unit->BumpUsage(Desc);
	}

	// Hand off to authoritative resolution
	if (AMatchGameMode* GM = Unit->GetWorld()->GetAuthGameMode<AMatchGameMode>())
	{
		GM->Handle_ConfirmShoot(Args.InstigatorPC, Unit, Args.TargetUnit);
	}
}

void UAction_Overwatch::Setup(AUnitBase* Unit)
{
    OwnerUnit = Unit;

    if (!Unit) return;
    if (UAbilityEventSubsystem* Bus = AbilityBus(Unit->GetWorld()))
    {
        // Listen from server only (authority resolves shots)
        if (Unit->HasAuthority())
        {
            // Bind to catch movement events
            Bus->OnAny.AddUObject(this, &UAction_Overwatch::OnAnyEvent);
        }
    }
}

bool UAction_Overwatch::CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const
{
    if (!Unit) return false;

    const AMatchGameState* GS = Unit->GetWorld() ? Unit->GetWorld()->GetGameState<AMatchGameState>() : nullptr;
    if (!GS || GS->TurnPhase != ETurnPhase::Shoot) return false;

    // Don’t allow stacking: if already armed, don’t spend again
    if (Unit->bOverwatchArmed) return false;

    if (!Unit->CanUseActionNow(Desc)) return false;

    const UUnitActionResourceComponent* AP = Unit->FindComponentByClass<UUnitActionResourceComponent>();
    return (AP && AP->CanPay(Desc.Cost));
}

void UAction_Overwatch::Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args)
{
    if (!Unit) return;

    if (!PayAP(Unit)) return;

    if (Unit->HasAuthority())
    {
        Unit->bOverwatchArmed = true;   // arm it
        Unit->BumpUsage(Desc);
        Unit->ForceNetUpdate();

        if (AMatchGameState* S = Unit->GetWorld()->GetGameState<AMatchGameState>())
            S->Multicast_ScreenMsg(FString::Printf(TEXT("%s is on Overwatch."), *Unit->GetName()), FColor::Cyan, 2.0f);
    }
}

void UAction_Overwatch::OnAnyEvent(const FAbilityEventContext& Ctx)
{
    if (Ctx.Event != ECombatEvent::Unit_Moved) return;

    AUnitBase* Watcher = OwnerUnit.Get();
    AUnitBase* Mover   = Ctx.Source;
    if (!Watcher || !Mover || Watcher == Mover) return;
    if (!Watcher->HasAuthority()) return;              // only server reacts
    if (!Watcher->bOverwatchArmed) return;             // only if armed
    if (!Watcher->IsEnemy(Mover)) return;              // enemy only

    if (AMatchGameMode* GM = Watcher->GetWorld()->GetAuthGameMode<AMatchGameMode>())
    {
        // Range/LOS using your existing gate
        if (GM->ValidateShoot(Watcher, Mover))
        {
            GM->Handle_OverwatchShot(Watcher, Mover);  // see section 4
            Watcher->bOverwatchArmed = false;          // consume
            Watcher->ForceNetUpdate();
        }
    }
}