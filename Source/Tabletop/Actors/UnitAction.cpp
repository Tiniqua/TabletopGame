
#include "UnitAction.h"

#include "UnitBase.h"
#include "Tabletop/UnitActionResourceComponent.h"
#include "Tabletop/WeaponKeywordHelpers.h"
#include "Tabletop/Gamemodes/MatchGameMode.h"

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
	if (Unit->bHasShot)     return false;

	// AP path OR Assault+Advanced free path
	const bool bHasAssault = UWeaponKeywordHelpers::HasKeyword(Unit->GetActiveWeaponProfile(), EWeaponKeyword::Assault);
	if (bHasAssault && Unit->bAdvancedThisTurn)
	{
		// Allowed even if no AP
		return true;
	}

	// Otherwise require enough AP
	const UUnitActionResourceComponent* AP = Unit->FindComponentByClass<UUnitActionResourceComponent>();
	return (AP && AP->CanPay(Desc.Cost));
}

void UAction_Shoot::Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args)
{
	if (!Unit || !Args.InstigatorPC) return;

	const bool bHasAssault = UWeaponKeywordHelpers::HasKeyword(Unit->GetActiveWeaponProfile(), EWeaponKeyword::Assault);
	const bool bFreeThisShot = (bHasAssault && Unit->bAdvancedThisTurn);

	// Only pay AP if not free-by-Assault
	if (!bFreeThisShot)
	{
		if (!PayAP(Unit)) return; // server-side charge; fails safely if short
	}

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