#include "KeywordProcessor.h"
#include "WeaponKeywordHelpers.h"
#include "Actors/UnitBase.h"

#include "KeywordProcessor.h"
#include "WeaponKeywordHelpers.h"
#include "Actors/UnitBase.h"

static bool WithinHalfRange(const FAttackContext& Ctx)
{
    return Ctx.RangeInches <= FMath::Max(1.f, Ctx.Weapon->RangeInches * 0.5f);
}

void FKeywordProcessor::GatherPassiveAndConditional(
    const FWeaponProfile& W, const FAttackContext& Ctx,
    ECombatEvent Stage, FRollModifiers& Out)
{
    // HEAVY: +1 to hit if stationary
    if (Stage == ECombatEvent::PreHitCalc)
    {
        if (UWeaponKeywordHelpers::HasKeyword(W, EWeaponKeyword::Heavy) && !Ctx.bAttackerMoved)
        {
            Out.HitNeedOffset -= 1; // easier to hit
        }

        // RAPID FIRE: bonus attacks at ≤ half range  (needs Value => FindKeyword)
        if (const FWeaponKeywordData* RF = UWeaponKeywordHelpers::FindKeyword(W, EWeaponKeyword::RapidFire))
        {
            if (WithinHalfRange(Ctx) && RF->Value != 0)
            {
                Out.AttacksDelta += RF->Value; // per-volley bump (your DT choice)
            }
        }

        // BLAST (deterministic, no RNG here): +Value attacks per 5 enemy models
        // If Value==0, we leave RNG fallback to BuildForStage (server-authoritative).
        if (Ctx.Target)
        {
            if (const FWeaponKeywordData* BL = UWeaponKeywordHelpers::FindKeyword(W, EWeaponKeyword::Blast))
            {
                if (BL->Value > 0)
                {
                    const int32 per = 5; // tune if you expose a DT scalar later
                    const int32 tgt = FMath::Max(0, Ctx.Target->ModelsCurrent);
                    Out.AttacksDelta += (tgt / per) * BL->Value;
                }
            }
        }

        // TORRENT/AUTO-HIT (boolean only)
        if (UWeaponKeywordHelpers::HasKeyword(W, EWeaponKeyword::Torrent))
        {
            Out.bAutoHit = true;
        }
    }

    // ASSAULT: allow shooting after Advance; (else your ValidateShoot can block)
    if (Stage == ECombatEvent::PreValidateShoot)
    {
        if (Ctx.bAttackerAdvanced && !UWeaponKeywordHelpers::HasKeyword(W, EWeaponKeyword::Assault))
        {
            // mark disallowed if you want; ValidateShoot can also enforce
        }
    }

    // TWIN-LINKED: re-roll wound rolls (needs flags => FindKeyword)
    if (Stage == ECombatEvent::PreWoundCalc)
    {
        if (const FWeaponKeywordData* TL = UWeaponKeywordHelpers::FindKeyword(W, EWeaponKeyword::TwinLinked))
        {
            Out.bRerollAllWounds  |= TL->bRerollAllWounds;
            Out.bRerollOnesWounds |= TL->bRerollOnesWounds || (!TL->bRerollAllWounds); // default to reroll 1s
        }
    }

    // SHRED: re-roll 1s to wound (boolean only => HasKeyword)
    if (Stage == ECombatEvent::PreWoundCalc)
    {
        if (UWeaponKeywordHelpers::HasKeyword(W, EWeaponKeyword::Shred))
        {
            Out.bRerollOnesWounds = true;
        }
    }

    // IGNORES COVER (boolean only)
    if (Stage == ECombatEvent::PreSavingThrows)
    {
        if (UWeaponKeywordHelpers::HasKeyword(W, EWeaponKeyword::IgnoresCover))
        {
            Out.bIgnoreCover = true;
        }
    }

    // PIERCING: extra AP at ≤ half range (needs Value => FindKeyword)
    if (Stage == ECombatEvent::PreSavingThrows)
    {
        if (const FWeaponKeywordData* P = UWeaponKeywordHelpers::FindKeyword(W, EWeaponKeyword::Piercing))
        {
            if (WithinHalfRange(Ctx) && P->Value > 0)
            {
                Out.APDelta += P->Value;
            }
        }
    }
}

FStageResult FKeywordProcessor::BuildForStage(const FAttackContext& Ctx, ECombatEvent Stage, int32 DamageApplied)
{
    FStageResult R;

    if (Stage == ECombatEvent::PreHitCalc)
    {
        // Heavy
        if (UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::Heavy) && !Ctx.bAttackerMoved)
        {
            R.ModsNow.HitNeedOffset -= 1;
        }

        // Rapid Fire
        if (const FWeaponKeywordData* RF = UWeaponKeywordHelpers::FindKeyword(*Ctx.Weapon, EWeaponKeyword::RapidFire))
        {
            if (WithinHalfRange(Ctx) && RF->Value > 0)
            {
                R.ModsNow.AttacksDelta += RF->Value;
            }
        }

        // Blast RNG fallback: if Value==0 in DT, use classic D3/D6 scaling by enemy size
        if (Ctx.Target)
        {
            if (const FWeaponKeywordData* BL = UWeaponKeywordHelpers::FindKeyword(*Ctx.Weapon, EWeaponKeyword::Blast))
            {
                if (BL->Value == 0)
                {
                    const int32 tgt = FMath::Max(0, Ctx.Target->ModelsCurrent);
                    if (tgt >= 11)      R.ModsNow.AttacksDelta += FMath::RandRange(1,6);
                    else if (tgt >= 6)  R.ModsNow.AttacksDelta += FMath::RandRange(1,3);
                }
            }
        }

        // Torrent
        if (UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::Torrent))
        {
            R.ModsNow.bAutoHit = true;
        }
    }

    if (Stage == ECombatEvent::PreSavingThrows)
    {
        // Ignores Cover
        if (UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::IgnoresCover))
        {
            R.ModsNow.bIgnoreCover = true;
        }
    }

    if (Stage == ECombatEvent::PostResolveAttack)
    {
        // HAZARDOUS: D6 per model; each 6 kills exactly one model (no overkill).
        if (UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::Hazardous) && Ctx.Attacker)
        {
            const int32 Models = FMath::Max(0, Ctx.Attacker->ModelsCurrent);
            int32 Casualties = 0;
            for (int32 i = 0; i < Models; ++i)
            {
                if (FMath::RandRange(1,6) == 6) { ++Casualties; }
            }

            if (Casualties > 0)
            {
                const int32 PerModel     = Ctx.Attacker->GetWoundsPerModel();
                const int32 TargetModels = FMath::Max(0, Models - Casualties);
                const int32 TargetPool   = TargetModels * PerModel;
                const int32 DamageNeeded = FMath::Max(0, Ctx.Attacker->WoundsPool - TargetPool);

                if (DamageNeeded > 0)
                {
                    // Reuse existing immediate-damage plumbing in your GM:
                    R.ModsNow.MortalDamageImmediateToOwner += DamageNeeded;
                }
            }
        }

        // SUPPRESSIVE: if damage landed, give target -1 to hit on their next attacks
        if (UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::Suppressive) && DamageApplied > 0)
        {
            FUnitModifier Suppress;
            Suppress.AppliesAt      = ECombatEvent::PreHitCalc;
            Suppress.Targeting      = EModifierTarget::OwnerWhenAttacking;
            Suppress.Mods.HitNeedOffset += +1; // harder to hit
            Suppress.Expiry         = EModifierExpiry::UntilEndOfTurn;
            Suppress.TurnsRemaining = 1;
            R.GrantsToTarget.Add(Suppress);
        }
    }

    return R;
}

static uint8 RerollIfNeeded(uint8 Roll, bool bRerollAll, bool bRerollOnes, TFunctionRef<uint8()> D6)
{
    if (bRerollAll || (bRerollOnes && Roll == 1))
    {
        return D6();
    }
    return Roll;
}

void FKeywordProcessor::ApplyStage(ECombatEvent Stage, FAttackContext& Ctx)
{
    FRollModifiers Mods;
    GatherPassiveAndConditional(*Ctx.Weapon, Ctx, Stage, Mods);

    // Apply generic mods to the live context
    switch (Stage)
    {
        case ECombatEvent::PreHitCalc:
            Ctx.Attacks += Mods.AttacksDelta;
            Ctx.HitNeed  = FMath::Clamp(Ctx.HitNeed + Mods.HitNeedOffset, 2, 6);
            if (Mods.bAutoHit)
            {
                Ctx.HitRolls.Reset();
                Ctx.Hits = Ctx.Attacks; // everything hits
            }
            break;

        case ECombatEvent::PreWoundCalc:
        {
            // apply wound need offsets first (if you ever add any)
            Ctx.WoundNeed = FMath::Clamp(Ctx.WoundNeed + Mods.WoundNeedOffset, 2, 6);

            // Roll wounds (respect prior auto-wounds from Lethal)
            const bool bRerollAllWounds  = Mods.bRerollAllWounds;
            const bool bRerollOnesWounds = Mods.bRerollOnesWounds;

            const int32 HitsNeedingWound = FMath::Max(0, Ctx.Hits - Ctx.Wounds);
            int32 NewWounds = 0;
            for (int32 i=0; i<HitsNeedingWound; ++i)
            {
                uint8 r = (uint8)FMath::RandRange(1,6);
                if (r < Ctx.WoundNeed)
                {
                    r = RerollIfNeeded(r, bRerollAllWounds, bRerollOnesWounds, [](){ return (uint8)FMath::RandRange(1,6); });
                }
                if (r >= Ctx.WoundNeed) ++NewWounds;
                Ctx.WoundRolls.Add(r);
            }
            Ctx.Wounds += NewWounds;
            break;
        }

        case ECombatEvent::PreSavingThrows:
        {
            // Brutal: +Damage per natural 6 to wound
            int32 BrutalBonusPer6 = 0;
            if (const FWeaponKeywordData* BR = UWeaponKeywordHelpers::FindKeyword(*Ctx.Weapon, EWeaponKeyword::Brutal))
            {
                BrutalBonusPer6 = (BR->Value != 0) ? BR->Value : 1; // default +1 if unspecified
            }
            if (BrutalBonusPer6 != 0)
            {
                int32 BrutalSixes = 0;
                for (uint8 r : Ctx.WoundRolls) if (r == 6) ++BrutalSixes;
                if (BrutalSixes > 0) Ctx.Damage += BrutalSixes * BrutalBonusPer6;
            }

            Ctx.AP      += Mods.APDelta;
            Ctx.Damage  += Mods.DamageDelta;
            Ctx.bIgnoreCover |= Mods.bIgnoreCover;
            break;
        }

        default: break;
    }

    // Keyword-specific transforms that need dice context
    if (Stage == ECombatEvent::PostHitRolls)
    {
        // SUSTAINED HITS: each crit adds Value extra hits
        if (const FWeaponKeywordData* SH = UWeaponKeywordHelpers::FindKeyword(*Ctx.Weapon, EWeaponKeyword::SustainedHits))
        {
            if (SH->Value > 0)
            {
                int32 Extra = 0;
                for (uint8 r : Ctx.HitRolls)
                {
                    if (r >= Ctx.CritHitThreshold)
                    {
                        Extra += SH->Value;
                    }
                }
                Ctx.Hits += Extra;
            }
        }

        // LETHAL HITS: crits to-hit auto-wound
        if (UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::LethalHits))
        {
            int32 AutoWounds = 0;
            for (uint8 r : Ctx.HitRolls)
            {
                if (r >= Ctx.CritHitThreshold)
                {
                    ++AutoWounds;
                }
            }
            Ctx.Wounds += AutoWounds; // add now; remaining hits still roll to wound
        }
    }

    if (Stage == ECombatEvent::PostWoundRolls)
    {
        // DEVASTATING WOUNDS: crits to-wound bypass saves
        if (UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::DevastatingWounds))
        {
            int32 Crits = 0;
            for (uint8 r : Ctx.WoundRolls) if (r >= Ctx.CritWoundThreshold) ++Crits;
            Ctx.CritWounds_NoSave += Crits;
        }

        // RENDING: any crit to-wound increases AP by +Value (default +1) for the volley
        if (const FWeaponKeywordData* REN = UWeaponKeywordHelpers::FindKeyword(*Ctx.Weapon, EWeaponKeyword::Rending))
        {
            const int32 Inc = (REN->Value != 0) ? REN->Value : 1;
            for (uint8 r : Ctx.WoundRolls)
            {
                if (r >= Ctx.CritWoundThreshold) { Ctx.AP += Inc; break; } // once per volley
            }
        }
    }

    if (Stage == ECombatEvent::PostResolveAttack)
    {
        
    }
}
