#include "PassiveAbility.h"

#include "GameFramework/PlayerState.h"

void UPassiveAbility::Setup(AUnitBase* Unit)
{
	Super::Setup(Unit);

	if(OwnerUnit)
	{
		if (OwnerUnit->HasAuthority())
		{
			if (AMatchGameMode* GM = OwnerUnit->GetWorld()->GetAuthGameMode<AMatchGameMode>())
			{
				if (UAbilityEventSubsystem* Bus = GM->AbilityBus(OwnerUnit->GetWorld()))
				{
					Bus->OnAny.AddUObject(this, &UPassiveAbility::OnAnyEvent);
				}
			}
		}
	}
	
}

void UPassiveAbility::OnAnyEvent(const FAbilityEventContext& Ctx)
{
	if (!OwnerUnit || !OwnerUnit->HasAuthority())
		return;
	
	if (WantsEvent(Ctx.Event))
	{
		HandleEvent(Ctx);
	}
}

bool UPassiveAbility::WantsEvent(ECombatEvent E) const
{
	return E == ECombatEvent::Turn_End;
}

void UPassiveAbility::HandleEvent(const FAbilityEventContext& Ctx)
{
	// blank for now
}

// ---------- PASSIVE HEAL -------------

void UAbility_HealthRegen::HandleEvent(const FAbilityEventContext& Ctx)
{
	Super::HandleEvent(Ctx);
	
	AUnitBase* U = OwnerUnit;
	
	if (!U || !U->HasAuthority()) return;
	if (U->ModelsCurrent <= 0)    return;
	if (Ctx.Event != ECombatEvent::Turn_End) return;

	const APlayerState* EndingPS = Ctx.GS ? Ctx.GS->CurrentTurn : nullptr;

	if (bOnlyOnOwningPlayersTurn && EndingPS && U->OwningPS != EndingPS)
		return;

	const int32 perModel = FMath::Max(1, U->WoundsRep);
	const int32 maxPool  = perModel * FMath::Max(0, U->ModelsMax);
	const int32 missing  = FMath::Max(0, maxPool - U->WoundsPool);
	const int32 toHeal   = FMath::Clamp(HealPerTurn, 0, missing);
	if (toHeal <= 0) return;

	U->ApplyHealing_Server(toHeal);

	if (AMatchGameState* S = Ctx.GS)
	{
		S->Multicast_ScreenMsg(
			FString::Printf(TEXT("%s regenerates %d wounds."), *U->GetName(), toHeal),
			FColor::Emerald, 1.5f);
	}
}

bool UAbility_HealthRegen::WantsEvent(ECombatEvent E) const
{
	return Super::WantsEvent(E);
}
