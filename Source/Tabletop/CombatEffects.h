// CombatEffects.h
#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "CombatEffects.generated.h"

UENUM(BlueprintType)
enum class ECombatEvent : uint8
{
    // Game/turn lifecycle
    Game_Begin,              // once
    Game_End,                // once
    Round_Begin,
    Round_End,
    Turn_Begin,              // CurrentTurn set
    Turn_End,                // before handover
    Phase_Begin,             // TurnPhase set
    Phase_End,

    // Movement / generic action hooks
    PreValidateMove,
    PreMoveExecute,
    PostMove,
    PreAdvanceExecute,
    PostAdvance,

    // Existing shooting pipeline (unchanged order)
    PreValidateShoot,
    PreHitCalc,
    PostHitRolls,
    PreWoundCalc,
    PostWoundRolls,
    PreSavingThrows,
    PostSavingThrows,
    PostDamageCompute,
    PostResolveAttack,

    // Generic ability triggers
    Ability_Activated,       // when a unit toggles/uses an ability or action
    Ability_Expired,         // timed / uses burnt
    Unit_Destroyed,
};

USTRUCT(BlueprintType)
struct FRollModifiers
{
    GENERATED_BODY()

    // Threshold / stat deltas (apply immediately when aggregated for that stage)
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 HitNeedOffset   = 0; // -1 means easier to hit
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 WoundNeedOffset = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 APDelta         = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DamageDelta     = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 AttacksDelta    = 0;

    // Re-roll intent flags (you decide where/how to consume)
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRerollAllHits    = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRerollOnesHits   = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRerollAllWounds  = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRerollOnesWounds = false;

    // Other toggles
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bIgnoreCover = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bAutoHit     = false;

    // Immediate effects (only respected when aggregated at PostResolveAttack)
    // Still “mods”, not bespoke functions:
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MortalDamageImmediateToOwner    = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MortalDamageImmediateToOpponent = 0;

    // Utility to add/compose
    void Accumulate(const FRollModifiers& O)
    {
        HitNeedOffset   += O.HitNeedOffset;
        WoundNeedOffset += O.WoundNeedOffset;
        APDelta         += O.APDelta;
        DamageDelta     += O.DamageDelta;
        AttacksDelta    += O.AttacksDelta;

        bRerollAllHits    |= O.bRerollAllHits;
        bRerollOnesHits   |= O.bRerollOnesHits;
        bRerollAllWounds  |= O.bRerollAllWounds;
        bRerollOnesWounds |= O.bRerollOnesWounds;

        bIgnoreCover |= O.bIgnoreCover;
        bAutoHit     |= O.bAutoHit;

        MortalDamageImmediateToOwner    += O.MortalDamageImmediateToOwner;
        MortalDamageImmediateToOpponent += O.MortalDamageImmediateToOpponent;
    }
};

UENUM(BlueprintType)
enum class EModifierTarget : uint8
{
    OwnerWhenAttacking,
    OwnerWhenDefending,
    OwnerAlways
};

UENUM(BlueprintType)
enum class EModifierExpiry : uint8
{
    UntilEndOfTurn,
    UntilEndOfRound,
    NextNOwnerShots,    // consumes on PreHitCalc for the owner
    Uses                // generic counter you consume wherever you choose
};

USTRUCT(BlueprintType)
struct FUnitModifier
{
    GENERATED_BODY()

    // Stage and targeting window when this mod contributes
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ECombatEvent   AppliesAt = ECombatEvent::PreHitCalc;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EModifierTarget Targeting = EModifierTarget::OwnerAlways;

    // The actual roll mods to contribute at that stage
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FRollModifiers Mods;

    // Simple lifespan
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EModifierExpiry Expiry = EModifierExpiry::UntilEndOfTurn;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 UsesRemaining    = 0; // when Expiry==Uses or NextNOwnerShots
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 TurnsRemaining   = 1; // for turn/round duration

    // Optional gates
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGameplayTagContainer RequiresOpponentTags;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGameplayTagContainer RequiresOwnerTags;
};