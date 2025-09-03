// KeywordProcessor.h
#pragma once
#include "CoreMinimal.h"
#include "ArmyData.h"
#include "CombatEffects.h"
#include "WeaponKeywords.h"

struct FAttackContext
{
	class AUnitBase* Attacker = nullptr;
	class AUnitBase* Target   = nullptr;
	const FWeaponProfile* Weapon = nullptr;

	float RangeInches = 0.f;
	bool  bAttackerMoved   = false;
	bool  bAttackerAdvanced= false;

	// live numbers (mutable during pipeline)
	int32 Attacks = 0;
	int32 HitNeed = 4;
	int32 WoundNeed = 4;
	int32 AP = 0;
	int32 Damage = 1;
	bool  bIgnoreCover = false;

	// dice we keep so keywords can inspect crits etc.
	TArray<uint8> HitRolls;
	TArray<uint8> WoundRolls;

	// critical thresholds (tweakable if you add effects that change what a "crit" is)
	uint8 CritHitThreshold   = 6;
	uint8 CritWoundThreshold = 6;

	// counts that keywords may augment
	int32 Hits   = 0;
	int32 Wounds = 0;

	// split wounds so Devastating Wounds (or similar) can bypass saves
	int32 CritWounds_NoSave = 0;
};

struct FStageResult
{
	FRollModifiers ModsNow;                // to apply immediately this stage
	TArray<FUnitModifier> GrantsToAttacker;// future time-boxed mods to add to attacker
	TArray<FUnitModifier> GrantsToTarget;  // future time-boxed mods to add to target
};

class FKeywordProcessor
{
public:
	static void GatherPassiveAndConditional(const FWeaponProfile& Wpn,
											const FAttackContext& Ctx,
											ECombatEvent Stage,
											FRollModifiers& OutMods);

	// Convenience pass that applies common keywords at each stage
	static void ApplyStage(ECombatEvent Stage, FAttackContext& Ctx);
	
	static FStageResult BuildForStage(const FAttackContext& Ctx, ECombatEvent Stage, int32 DamageAppliedOptional=0);
};
