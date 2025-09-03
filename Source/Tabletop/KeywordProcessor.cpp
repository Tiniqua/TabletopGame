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
    }

    // ASSAULT: allow shooting after Advance; (else your ValidateShoot can block)
    if (Stage == ECombatEvent::PreValidateShoot)
    {
        if (Ctx.bAttackerAdvanced && !UWeaponKeywordHelpers::HasKeyword(W, EWeaponKeyword::Assault))
        {
            // mark disallowed if you want; ValidateShoot can also enforce
        }
    }

    // RAPID FIRE: bonus attacks at ≤ half range  (needs Value => FindKeyword)
    if (Stage == ECombatEvent::PreHitCalc)
    {
        if (const FWeaponKeywordData* RF = UWeaponKeywordHelpers::FindKeyword(W, EWeaponKeyword::RapidFire))
        {
            if (WithinHalfRange(Ctx) && RF->Value != 0)
            {
                Out.AttacksDelta += RF->Value; // e.g., +1A per model or flat +X total
            }
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

    // TORRENT/AUTO-HIT (boolean only)
    if (Stage == ECombatEvent::PreHitCalc)
    {
        if (UWeaponKeywordHelpers::HasKeyword(W, EWeaponKeyword::Torrent))
        {
            Out.bAutoHit = true;
        }
    }
}


FStageResult FKeywordProcessor::BuildForStage(const FAttackContext& Ctx, ECombatEvent Stage, int32 DamageApplied)
{
    FStageResult R;

    // --- examples of "now" mods ---
    if (Stage == ECombatEvent::PreHitCalc)
    {
        // Heavy: easier to hit if stationary (boolean only)
        if (UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::Heavy) && !Ctx.bAttackerMoved)
        {
            R.ModsNow.HitNeedOffset -= 1;
        }

        // Rapid Fire: +attacks at half range (needs Value => FindKeyword)
        if (const FWeaponKeywordData* RF = UWeaponKeywordHelpers::FindKeyword(*Ctx.Weapon, EWeaponKeyword::RapidFire))
        {
            if (WithinHalfRange(Ctx) && RF->Value > 0)
            {
                R.ModsNow.AttacksDelta += RF->Value;
            }
        }

        // Torrent/Auto-Hit (boolean only)
        if (UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::Torrent))
        {
            R.ModsNow.bAutoHit = true;
        }
    }

    if (Stage == ECombatEvent::PreSavingThrows)
    {
        // Ignores Cover (boolean only)
        if (UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::IgnoresCover))
        {
            R.ModsNow.bIgnoreCover = true;
        }
    }

    if (Stage == ECombatEvent::PostResolveAttack)
    {
        // Hazardous: generic immediate self-mortal via Mods (boolean only)
        if (UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::Hazardous))
        {
            R.ModsNow.MortalDamageImmediateToOwner += 3;
        }

        // Suppressive: grant a -1 hit mod to the TARGET on their next attack (boolean only)
        if (UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::Suppressive) && DamageApplied > 0)
        {
            FUnitModifier Suppress;
            Suppress.AppliesAt   = ECombatEvent::PreHitCalc;
            Suppress.Targeting   = EModifierTarget::OwnerWhenAttacking;
            Suppress.Mods.HitNeedOffset += +1; // harder to hit
            Suppress.Expiry      = EModifierExpiry::UntilEndOfTurn;
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
            Ctx.WoundNeed = FMath::Clamp(Ctx.WoundNeed + Mods.WoundNeedOffset, 2, 6);
            break;

        case ECombatEvent::PreSavingThrows:
            Ctx.AP      += Mods.APDelta;
            Ctx.Damage  += Mods.DamageDelta;
            Ctx.bIgnoreCover |= Mods.bIgnoreCover;
            break;

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
    }

    if (Stage == ECombatEvent::PostResolveAttack)
    {
        // HAZARDOUS: simple version—if fired, roll a D6, on 1 take Damage 3 (tune to your game)
        if (UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::Hazardous))
        {
            const uint8 r = FMath::RandRange(1,6);
            if (r == 1 && Ctx.Attacker)
            {
                // Apply self-damage method you already have:
                Ctx.Attacker->ApplyMortalDamage_Server(3); // implement in your actor
            }
        }

        // SUPPRESSIVE: if unsaved damage landed, apply a debuff tag for 1 round
        if (UWeaponKeywordHelpers::HasKeyword(*Ctx.Weapon, EWeaponKeyword::Suppressive) && Ctx.Target)
        {
            // You already know if any damage was applied in your GM logic—call this after you compute applied damage.
            // Example:
            // Ctx.Target->ApplySuppressedDebuff(DurationTurns=1, HitPenalty=-1);
        }
    }
}
