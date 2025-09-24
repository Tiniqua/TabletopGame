// PassiveAbility.h
#pragma once

#include "Tabletop/AbiltyEventSubsystem.h"
#include "Tabletop/Actors/UnitAction.h"
#include "Tabletop/Actors/UnitBase.h"
#include "Tabletop/Gamemodes/MatchGameMode.h"
#include "PassiveAbility.generated.h"

UCLASS(Abstract, Blueprintable)
class TABLETOP_API UPassiveAbility : public UUnitAction
{
	GENERATED_BODY()
public:
	UPassiveAbility()
	{
		Desc.Cost   = 0;
		Desc.Phase  = ETurnPhase::Move;   // ignored
		Desc.bPassive = true;             // <- key
	}

	virtual void Setup(AUnitBase* Unit) override;
	

protected:
	UFUNCTION()
	void OnAnyEvent(const FAbilityEventContext& Ctx);
	
	// Child must say what to listen to, and what to do.
	virtual bool WantsEvent(ECombatEvent E) const;
	virtual void HandleEvent(const FAbilityEventContext& Ctx);
};

// ---------- PASSIVE HEAL -------------

UCLASS()
class TABLETOP_API UAbility_HealthRegen : public UPassiveAbility
{
    GENERATED_BODY()
public:
    UAbility_HealthRegen()
    {
        Desc.ActionId    = TEXT("HealthRegen");
        Desc.DisplayName = NSLOCTEXT("Abilities","HealthRegen","Regeneration");
        Desc.Tooltip     = NSLOCTEXT("Abilities","HealthRegenTip",
                                     "At the end of each (own) turn, this unit heals up to 3 wounds.");
        Desc.bShowInPassiveList = true;
    }

    UPROPERTY(EditDefaultsOnly) int32 HealPerTurn = 3;

    // If true: only when this unit's owning player ends their turn.
    // If false: trigger on every player's Turn_End.
    UPROPERTY(EditDefaultsOnly)
	bool bOnlyOnOwningPlayersTurn = false;

protected:
    virtual bool WantsEvent(ECombatEvent E) const override;

    virtual void HandleEvent(const FAbilityEventContext& Ctx) override;
};