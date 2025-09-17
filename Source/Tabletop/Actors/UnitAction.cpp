#include "UnitAction.h"

#include "UnitBase.h"
#include "EngineUtils.h"
#include "Tabletop/CombatEffects.h"
#include "Tabletop/AbiltyEventSubsystem.h"            // (spelling matches your project)
#include "Tabletop/UnitActionResourceComponent.h"
#include "Tabletop/Gamemodes/MatchGameMode.h"

// ====================== UUnitAction (base) ======================

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

void UUnitAction::BeginPreview_Implementation(AUnitBase* /*Unit*/)
{
	// default no-op
}

void UUnitAction::EndPreview_Implementation(AUnitBase* /*Unit*/)
{
	// default no-op
}


// ====================== Move ======================

UAction_Move::UAction_Move()
{
	Desc.ActionId = TEXT("Move");
	Desc.DisplayName = FText::FromString(TEXT("Move"));
	Desc.Cost = 1; 
	Desc.Phase = ETurnPhase::Move;
	Desc.bRequiresGroundClick = true;
}

bool UAction_Move::CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const
{
	if (!Unit || Unit->ModelsCurrent <= 0) return false;
	// must have *some* budget to move at all
	if (Unit->MoveBudgetInches <= 0.f) return false;

	// If advanced, Move is "free" (ignores AP), otherwise check AP
	if (Unit->bAdvancedThisTurn)
	{
		return true;
	}

	const UUnitActionResourceComponent* AP = Unit->FindComponentByClass<UUnitActionResourceComponent>();
	return (AP && AP->CanPay(Desc.Cost));
}

void UAction_Move::Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args)
{
	if (!Unit || !Args.InstigatorPC) return;

	if (!Unit->bAdvancedThisTurn)
	{
		if (!PayAP(Unit)) return;
	}

	if (Unit->HasAuthority())
	{
		Unit->BumpUsage(Desc);
	}

	if (AMatchGameMode* GM = Unit->GetWorld()->GetAuthGameMode<AMatchGameMode>())
	{
		GM->Handle_MoveUnit(Args.InstigatorPC, Unit, Args.TargetLocation);
	}
}


// ====================== Advance ======================

UAction_Advance::UAction_Advance()
{
	Desc.ActionId = TEXT("Advance");
	Desc.DisplayName = FText::FromString(TEXT("Advance"));
	Desc.Cost = 1; 
	Desc.Phase = ETurnPhase::Move;
	Desc.NextPhaseAPCost = 1;
}

bool UAction_Advance::CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const
{
	if (!UUnitAction::CanExecute_Implementation(Unit, Args)) return false;
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


// ====================== Shoot ======================

UAction_Shoot::UAction_Shoot()
{
	Desc.ActionId = TEXT("Shoot");
	Desc.DisplayName = FText::FromString(TEXT("Shoot"));
	Desc.Cost = 2;
	Desc.Phase = ETurnPhase::Shoot;
	Desc.bRequiresEnemyTarget = true;
}

bool UAction_Shoot::CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const
{
	if (!Unit) return false;

	const AMatchGameState* GS = Unit->GetWorld() ? Unit->GetWorld()->GetGameState<AMatchGameState>() : nullptr;
	if (!GS || GS->TurnPhase != Desc.Phase) return false;

	// Uses-per-Phase/Turn/Match gates
	if (!Unit->CanUseActionNow(Desc)) return false;

	// Per-action checks
	if (!Args.InstigatorPC) return false;

	// Require enough AP
	const UUnitActionResourceComponent* AP = Unit->FindComponentByClass<UUnitActionResourceComponent>();
	return (AP && AP->CanPay(Desc.Cost));
}

void UAction_Shoot::Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args)
{
	if (!Unit || !Args.InstigatorPC) return;

	if (!PayAP(Unit)) return; // server-side charge; fails safely if short
	if (!Args.TargetUnit) return;

	if (Unit->HasAuthority())
	{
		Unit->BumpUsage(Desc);
	}

	if (AMatchGameMode* GM = Unit->GetWorld()->GetAuthGameMode<AMatchGameMode>())
	{
		GM->Handle_ConfirmShoot(Args.InstigatorPC, Unit, Args.TargetUnit);
	}
}


// ====================== Overwatch ======================

UAction_Overwatch::UAction_Overwatch()
{
	Desc.ActionId    = TEXT("Overwatch");
	Desc.DisplayName = FText::FromString(TEXT("Overwatch"));
	Desc.Cost        = 2;
	Desc.Phase       = ETurnPhase::Shoot;
}

void UAction_Overwatch::Setup(AUnitBase* Unit)
{
	OwnerUnit = Unit;
	if (!Unit) return;

	AMatchGameMode* GM = Unit->GetWorld()->GetAuthGameMode<AMatchGameMode>();
	if (UAbilityEventSubsystem* Bus = GM ? GM->AbilityBus(Unit->GetWorld()) : nullptr)
	{
		// Listen from server only (authority resolves shots)
		if (Unit->HasAuthority())
		{
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
		{
			S->Multicast_ScreenMsg(FString::Printf(TEXT("%s is on Overwatch."), *Unit->GetName()), FColor::Cyan, 2.0f);
		}
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
			GM->Handle_OverwatchShot(Watcher, Mover);  // authority resolves
			Watcher->bOverwatchArmed = false;          // consume
			Watcher->ForceNetUpdate();
		}
	}
}


// ====================== Take Aim (+1 to hit next shot) ======================

UAction_TakeAim::UAction_TakeAim()
{
	Desc.ActionId    = TEXT("TakeAim");
	Desc.DisplayName = FText::FromString(TEXT("Take Aim"));
	Desc.Cost        = 1;
	Desc.Phase       = ETurnPhase::Shoot;
}

void UAction_TakeAim::Execute_Implementation(AUnitBase* U, const FActionRuntimeArgs& /*Args*/)
{
	if (!U) return;
	if (!PayAP(U)) return;

	FUnitModifier M;
	M.AppliesAt              = ECombatEvent::PreHitCalc;
	M.Targeting              = EModifierTarget::OwnerWhenAttacking;
	M.Mods.HitNeedOffset     = -1;              // +1 to hit
	M.Expiry                 = EModifierExpiry::NextNOwnerShots;
	M.UsesRemaining          = 1;

	U->AddUnitModifier(M);
	if (U->HasAuthority())
	{
		U->BumpUsage(Desc);
	}
}


// ====================== Hunker (FNP easier until end of turn) ======================

UAction_Hunker::UAction_Hunker()
{
	Desc.ActionId    = TEXT("Hunker");
	Desc.DisplayName = NSLOCTEXT("Actions", "Hunker", "Hunker Down");
	Desc.Cost        = 2;
	Desc.Phase       = ETurnPhase::Shoot;
	Desc.UsesPerTurn = 1;
}

bool UAction_Hunker::CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const
{
	return UUnitAction::CanExecute_Implementation(Unit, Args);
}

void UAction_Hunker::Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args)
{
	if (!Unit) return;
	if (!PayAP(Unit)) return;

	FUnitModifier M;
	M.AppliesAt              = ECombatEvent::PostDamageCompute;      // right before FNP application in your pipeline
	M.Targeting              = EModifierTarget::OwnerWhenDefending;
	M.Mods.FnpNeedOffset     = -1;                                   // make FNP 1 pip easier
	M.Expiry                 = EModifierExpiry::UntilEndOfTurn;
	M.TurnsRemaining         = 1;

	Unit->AddUnitModifier(M);
	if (Unit->HasAuthority())
	{
		Unit->BumpUsage(Desc);
	}
}


// ====================== Brace (Invuln easier until end of turn) ======================

UAction_Brace::UAction_Brace()
{
	Desc.ActionId     = TEXT("Brace");
	Desc.DisplayName  = NSLOCTEXT("Actions", "Brace", "Brace");
	Desc.Cost         = 2;
	Desc.Phase        = ETurnPhase::Move;
	Desc.UsesPerTurn  = 1;
	Desc.NextPhaseAPCost = 1;
}

void UAction_Brace::Execute_Implementation(AUnitBase* U, const FActionRuntimeArgs& /*Args*/)
{
	if (!U) return;
	if (!PayAP(U)) return;

	FUnitModifier M;
	M.AppliesAt               = ECombatEvent::PreSavingThrows;     // where invuln is applied
	M.Targeting               = EModifierTarget::OwnerWhenDefending;
	M.Mods.InvulnNeedOffset   = -1;                                // e.g. 4++ -> 3++
	M.Expiry                  = EModifierExpiry::UntilEndOfTurn;
	M.TurnsRemaining          = 1;

	U->AddUnitModifier(M);
	if (U->HasAuthority())
	{
		U->BumpUsage(Desc);
	}
}


// ====================== Medpack (self-heal D3) ======================

UAction_Medpack::UAction_Medpack()
{
	Desc.ActionId     = TEXT("Medpack");
	Desc.DisplayName  = NSLOCTEXT("Actions", "Medpack", "Medpack");
	Desc.Cost         = 2;
	Desc.Phase        = ETurnPhase::Move;   // also valid in Shoot (see CanExecute)
	Desc.UsesPerMatch = 3;
	Desc.UsesPerTurn  = 1;
}

bool UAction_Medpack::CanExecute_Implementation(AUnitBase* U, const FActionRuntimeArgs& Args) const
{
	if (!U) return false;

	// Let this action be usable in Move OR Shoot
	if (const AMatchGameState* S = U->GetWorld() ? U->GetWorld()->GetGameState<AMatchGameState>() : nullptr)
	{
		if (!(S->TurnPhase == ETurnPhase::Move || S->TurnPhase == ETurnPhase::Shoot))
			return false;
	}

	if (!U->CanUseActionNow(Desc)) return false;

	if (const UUnitActionResourceComponent* AP = U->FindComponentByClass<UUnitActionResourceComponent>())
		if (AP->CurrentAP < Desc.Cost) return false;

	// Nothing to do if already full
	const int32 PerModel = FMath::Max(1, U->GetWoundsPerModel());
	const int32 MaxPool  = FMath::Max(0, U->ModelsMax * PerModel);
	return U->WoundsPool < MaxPool;
}

void UAction_Medpack::Execute_Implementation(AUnitBase* U, const FActionRuntimeArgs& /*Args*/)
{
	if (!U) return;
	if (!PayAP(U)) return;
	if (U->HasAuthority()) U->BumpUsage(Desc);

	// D3 = ceil(d6 / 2)
	const int32 d6       = FMath::RandRange(1, 6);
	const int32 HealAmt  = (d6 + 1) / 2;

	const int32 PerModel = FMath::Max(1, U->GetWoundsPerModel());
	const int32 MaxPool  = FMath::Max(0, U->ModelsMax * PerModel);

	const int32 NewPool  = FMath::Clamp(U->WoundsPool + HealAmt, 0, MaxPool);
	U->WoundsPool        = NewPool;

	int32 NewModels = (NewPool + PerModel - 1) / PerModel;
	NewModels = FMath::Clamp(NewModels, 0, U->ModelsMax);
	if (NewModels != U->ModelsCurrent)
	{
		U->ModelsCurrent = NewModels;
		U->RebuildFormation();
	}
	U->ForceNetUpdate();
}


// ====================== Field Medic (heal closest ally within 12") ======================

UAction_FieldMedic::UAction_FieldMedic()
{
	Desc.ActionId     = TEXT("FieldMedic");
	Desc.DisplayName  = NSLOCTEXT("Actions", "FieldMedic", "Field Medic");
	Desc.Cost         = 2;
	Desc.Phase        = ETurnPhase::Move;   // also allowed in Shoot via CanExecute
	Desc.UsesPerMatch = 2;
	Desc.UsesPerTurn  = 1;
}

bool UAction_FieldMedic::CanExecute_Implementation(AUnitBase* U, const FActionRuntimeArgs& Args) const
{
	if (!U) return false;

	// Allow in Move OR Shoot
	const AMatchGameState* S = U->GetWorld() ? U->GetWorld()->GetGameState<AMatchGameState>() : nullptr;
	if (!S || !(S->TurnPhase == ETurnPhase::Move || S->TurnPhase == ETurnPhase::Shoot))
		return false;

	if (!U->CanUseActionNow(Desc)) return false;

	if (const UUnitActionResourceComponent* AP = U->FindComponentByClass<UUnitActionResourceComponent>())
		if (AP->CurrentAP < Desc.Cost) return false;

	// Must have at least one valid friendly target within 12"
	return FindClosestAllyWithin12(U) != nullptr;
}

void UAction_FieldMedic::Execute_Implementation(AUnitBase* U, const FActionRuntimeArgs& /*Args*/)
{
	if (!U) return;
	if (!PayAP(U)) return;

	AUnitBase* Ally = FindClosestAllyWithin12(U);
	if (!Ally) { EndPreview_Implementation(U); return; }

	if (U->HasAuthority()) U->BumpUsage(Desc);

	const int32 HealAmt = FMath::RandRange(1, 6); // D6

	const int32 PerModel = FMath::Max(1, Ally->GetWoundsPerModel());
	const int32 MaxPool  = FMath::Max(0, Ally->ModelsMax * PerModel);

	const int32 NewPool  = FMath::Clamp(Ally->WoundsPool + HealAmt, 0, MaxPool);
	Ally->WoundsPool     = NewPool;

	int32 NewModels = (NewPool + PerModel - 1) / PerModel;
	NewModels = FMath::Clamp(NewModels, 0, Ally->ModelsMax);
	if (NewModels != Ally->ModelsCurrent)
	{
		Ally->ModelsCurrent = NewModels;
		Ally->RebuildFormation();
	}
	Ally->ForceNetUpdate();

	EndPreview_Implementation(U);
}

void UAction_FieldMedic::BeginPreview_Implementation(AUnitBase* U)
{
	if (!U) return;
	if (AMatchGameState* GS = U->GetWorld() ? U->GetWorld()->GetGameState<AMatchGameState>() : nullptr)
	{
		if (AUnitBase* Ally = FindClosestAllyWithin12(U))
		{
			CachedPreviewTarget = Ally;
			// Requires your GameState to implement this multicast (see previous guidance)
			GS->Multicast_SetPotentialAllies({ Ally });
		}
	}
}

void UAction_FieldMedic::EndPreview_Implementation(AUnitBase* U)
{
	if (AMatchGameState* GS = U && U->GetWorld() ? U->GetWorld()->GetGameState<AMatchGameState>() : nullptr)
	{
		GS->Multicast_ClearPotentialTargets();
	}
	CachedPreviewTarget.Reset();
}

AUnitBase* UAction_FieldMedic::FindClosestAllyWithin12(AUnitBase* U) const
{
	if (!U) return nullptr;
	UWorld* World = U->GetWorld(); 
	if (!World) return nullptr;

	// 12" in centimeters using your GM scale
	const AMatchGameMode* GM = World->GetAuthGameMode<AMatchGameMode>();
	const float cmPerIn = GM ? GM->CmPerTabletopInch() : 2.54f * 20.f; // conservative fallback scale
	const float Rcm = 12.f * cmPerIn;
	const float R2  = Rcm * Rcm;

	AUnitBase* Best = nullptr;
	float BestD2 = R2;

	const FVector MyLoc = U->GetActorLocation();

	for (TActorIterator<AUnitBase> It(World); It; ++It)
	{
		AUnitBase* Other = *It;
		if (!Other || Other == U) continue;
		if (Other->OwningPS != U->OwningPS) continue; // friendly only

		const float D2 = FVector::DistSquared(MyLoc, Other->GetActorLocation());
		if (D2 <= BestD2)
		{
			BestD2 = D2;
			Best   = Other;
		}
	}
	return Best;
}
