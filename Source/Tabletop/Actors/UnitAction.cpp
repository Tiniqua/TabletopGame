
#include "UnitAction.h"

#include "UnitBase.h"
#include "Tabletop/UnitActionResourceComponent.h"
#include "Tabletop/Gamemodes/MatchGameMode.h"

bool UUnitAction::CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& /*Args*/) const
{
	if (!Unit) return false;
	const AMatchGameState* GS = Unit->GetWorld() ? Unit->GetWorld()->GetGameState<AMatchGameState>() : nullptr;
	if (!GS || GS->TurnPhase != Desc.Phase) return false;

	const UUnitActionResourceComponent* AP = Unit->FindComponentByClass<UUnitActionResourceComponent>();
	// If the component isn't present or hasn't replicated yet, don't block UI.
	if (!AP) return true;

	return AP->CanPay(Desc.Cost);
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
	if (!Super::CanExecute_Implementation(Unit, Args)) return false;
	if (!Unit || Unit->ModelsCurrent <= 0) return false;
	if (Unit->MoveMaxInches <= 0.f) return false;
	return true;
}


void UAction_Move::Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args)
{
	if (!Unit || !Args.InstigatorPC) return;
	if (!PayAP(Unit)) return;

	if (AMatchGameMode* GM = Unit->GetWorld()->GetAuthGameMode<AMatchGameMode>())
	{
		GM->Handle_MoveUnit(Args.InstigatorPC, Unit, Args.TargetLocation);
	}
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

	if (AMatchGameMode* GM = Unit->GetWorld()->GetAuthGameMode<AMatchGameMode>())
	{
		GM->Handle_AdvanceUnit(Args.InstigatorPC, Unit);
	}
}

// -------------- SHOOT ACTION -----------------

bool UAction_Shoot::CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const
{
	if (!Super::CanExecute_Implementation(Unit, Args)) return false;
	if (!Args.InstigatorPC) return false;
	if (Unit->bHasShot) return false;
	// TargetUnit is OPTIONAL for enabling the button.
	// If present, you can also validate it here (range/LOS), but don’t require it.
	return true;
}

void UAction_Shoot::Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args)
{
	if (!Unit || !Args.InstigatorPC) return;
	if (!PayAP(Unit)) return;

	// Require target **here** (or your handler enters target mode if absent).
	if (!Args.TargetUnit) return;

	if (AMatchGameMode* GM = Unit->GetWorld()->GetAuthGameMode<AMatchGameMode>())
	{
		GM->Handle_ConfirmShoot(Args.InstigatorPC, Unit, Args.TargetUnit);
	}
}