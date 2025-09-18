// WeaponKeywordHelpers.cpp
#include "WeaponKeywordHelpers.h"

#include "Actors/UnitAbility.h"
#include "Actors/UnitBase.h"
#include "Internationalization/Text.h" // NSLOCTEXT

// -------- internal file-local helpers (not visible outside this .cpp) --------
namespace WeaponHelpers
{
    FORCEINLINE float CmPerTTIn() { return 2.54f; }
    FORCEINLINE float DistInches(const FVector& A, const FVector& B) { return FVector::Dist(A, B) / CmPerTTIn(); }

    static bool WithinHalfRange(const AUnitBase* Attacker, const AUnitBase* Target, const FWeaponProfile& Weapon)
    {
        const float R = static_cast<float>(Weapon.RangeInches);
        if (R <= 0.f) return false;
        const float D = DistInches(Attacker->GetActorLocation(), Target->GetActorLocation());
        return D <= FMath::Max(1.f, R * 0.5f);
    }

    // Minimal find/has helpers (file-local)
    static const FWeaponKeywordData* FindKeyword(const FWeaponProfile& W, EWeaponKeyword K)
    {
        return W.Keywords.FindByPredicate([K](const FWeaponKeywordData& E){ return E.Type == K; });
    }

    static bool HasKeyword(const FWeaponProfile& W, EWeaponKeyword K)
    {
        return FindKeyword(W, K) != nullptr;
    }

    // In your rules, cover grants SaveMod (+1). Ignores Cover only removes the save bonus (not the hit penalty).
    static bool HasAnyCoverSaveBonus(int32 /*HitMod*/, int32 SaveMod) { return SaveMod > 0; }
}

// -------- labels & tooltips --------

FText UWeaponKeywordHelpers::GetKeywordLabel(EWeaponKeyword K)
{
    switch (K)
    {
    case EWeaponKeyword::Assault:            return NSLOCTEXT("KW","Assault","Assault");
    case EWeaponKeyword::Heavy:              return NSLOCTEXT("KW","Heavy","Heavy");
    case EWeaponKeyword::RapidFire:          return NSLOCTEXT("KW","RapidFire","Rapid Fire");
    case EWeaponKeyword::SustainedHits:      return NSLOCTEXT("KW","SustainedHits","Sustained Hits");
    case EWeaponKeyword::LethalHits:         return NSLOCTEXT("KW","LethalHits","Lethal Hits");
    case EWeaponKeyword::TwinLinked:         return NSLOCTEXT("KW","TwinLinked","Twin-linked");
    case EWeaponKeyword::DevastatingWounds:  return NSLOCTEXT("KW","DevWounds","Devastating Wounds");
    case EWeaponKeyword::Blast:              return NSLOCTEXT("KW","Blast","Blast");
    case EWeaponKeyword::Hazardous:          return NSLOCTEXT("KW","Hazardous","Hazardous");
    case EWeaponKeyword::Torrent:            return NSLOCTEXT("KW","Torrent","Torrent");
    case EWeaponKeyword::Precision:          return NSLOCTEXT("KW","Precision","Precision");
    case EWeaponKeyword::IgnoresCover:       return NSLOCTEXT("KW","IgnoresCover","Ignores Cover");
    case EWeaponKeyword::Suppressive:        return NSLOCTEXT("KW","Suppressive","Suppressive");
    case EWeaponKeyword::Piercing:           return NSLOCTEXT("KW","Piercing","Piercing");
    case EWeaponKeyword::Concussive:         return NSLOCTEXT("KW","Concussive","Concussive");
    case EWeaponKeyword::Shred:              return NSLOCTEXT("KW","Shred","Shred");
    case EWeaponKeyword::Brutal:             return NSLOCTEXT("KW","Brutal","Brutal");
    case EWeaponKeyword::Rending:            return NSLOCTEXT("KW","Rending","Rending");
    default:                                 return NSLOCTEXT("KW","None","Keyword");
    }
}

FText UWeaponKeywordHelpers::GetKeywordTooltip(EWeaponKeyword K, const FWeaponKeywordData* Data,
                                               bool bActiveNow, bool bCond, bool bAtHalf)
{
    auto Txt = [](const TCHAR* S){ return FText::FromString(S); };

    switch (K)
    {
    case EWeaponKeyword::Assault:
        return Txt(bActiveNow ? TEXT("Assault: You advanced and can still shoot.")
                              : TEXT("Assault: Allows shooting after Advancing."));

    case EWeaponKeyword::Heavy:
        return Txt(bActiveNow ? TEXT("Heavy: Stationary this turn → +1 to hit.")
                              : TEXT("Heavy: If stationary, +1 to hit."));

    case EWeaponKeyword::RapidFire:
        return FText::FromString(FString::Printf(
            TEXT("Rapid Fire: At half range you gain +%d Attacks per model%s."),
            Data ? Data->Value : 0, bAtHalf ? TEXT(" (active)") : TEXT("")));

    case EWeaponKeyword::SustainedHits:
        return Txt(bCond ? TEXT("Sustained Hits: Critical hits to-hit add extra hits.")
                         : TEXT("Sustained Hits."));

    case EWeaponKeyword::LethalHits:
        return Txt(bCond ? TEXT("Lethal Hits: Critical hits to-hit automatically wound.")
                         : TEXT("Lethal Hits."));

    case EWeaponKeyword::TwinLinked:
    {
        const bool rerollAll  = Data && Data->bRerollAllWounds;
        const bool rerollOnes = Data && Data->bRerollOnesWounds;
        return Txt(rerollAll ? TEXT("Twin-linked: Re-roll all wound rolls.")
                             : TEXT("Twin-linked: Re-roll 1s to wound."));
    }

    case EWeaponKeyword::DevastatingWounds:
        return Txt(bCond ? TEXT("Devastating Wounds: Critical wounds bypass saves.")
                         : TEXT("Devastating Wounds."));

    case EWeaponKeyword::Blast:
        return Txt(TEXT("Blast: Bonus attacks vs larger units (not evaluated here)."));

    case EWeaponKeyword::Hazardous:
        return Txt(TEXT("Hazardous: After firing, roll one D6 per model; each 6 destroys that model."));

    case EWeaponKeyword::Torrent:
        return Txt(TEXT("Torrent: Attacks auto-hit."));

    case EWeaponKeyword::Precision:
        return Txt(TEXT("Precision: Can target key models (sniping)."));

    case EWeaponKeyword::IgnoresCover:
        return Txt(bActiveNow ? TEXT("Ignores Cover: Target cover does not improve their save.")
                              : TEXT("Ignores Cover: Negates cover bonus if any."));

    case EWeaponKeyword::Suppressive:
        return Txt(TEXT("Suppressive: On damage, apply a hit penalty to the target."));

    case EWeaponKeyword::Piercing:
        return FText::FromString(FString::Printf(
            TEXT("Piercing: At half range, AP +%d%s."),
            Data ? Data->Value : 0, bAtHalf ? TEXT(" (active)") : TEXT("")));

    case EWeaponKeyword::Concussive:
        return Txt(TEXT("Concussive: On hit, reduce OC/Move (game-specific)."));

    case EWeaponKeyword::Shred:
        return Txt(TEXT("Shred: Re-roll 1s to wound."));

    case EWeaponKeyword::Brutal:
        return FText::FromString(FString::Printf(
            TEXT("Brutal: 6s to wound deal +%d Damage."),
            Data ? Data->Value : 1));

    case EWeaponKeyword::Rending:
        return FText::FromString(FString::Printf(
            TEXT("Rending: Critical wounds increase AP by +%d."),
            Data ? Data->Value : 1));

    default:
        return Txt(TEXT("Keyword."));
    }
}

// -------- main entry point --------

const FWeaponKeywordData* UWeaponKeywordHelpers::FindKeyword(const FWeaponProfile& W, EWeaponKeyword K)
{
    for (const FWeaponKeywordData& E : W.Keywords)
    {
        if (E.Type == K) return &E;
    }
    return nullptr;
}

bool UWeaponKeywordHelpers::HasKeyword(const FWeaponProfile& W, EWeaponKeyword K)
{
    return UWeaponKeywordHelpers::FindKeyword(W, K) != nullptr;
}

int32 UWeaponKeywordHelpers::KeywordValue(const FWeaponProfile& W, EWeaponKeyword K, int32 Default)
{
    if (const FWeaponKeywordData* E = UWeaponKeywordHelpers::FindKeyword(W, K)) return E->Value;
    return Default;
}

void UWeaponKeywordHelpers::BuildKeywordUIInfos(
    const AUnitBase* Attacker, const AUnitBase* Target,
    int32 HitMod, int32 SaveMod,
    TArray<FKeywordUIInfo>& Out)
{
    Out.Reset();
    if (!Attacker || !Target) return;

    const FWeaponProfile& W = Attacker->GetActiveWeaponProfile();

    const bool bAtHalf   = WeaponHelpers::WithinHalfRange(Attacker, Target, W);
    const bool bMoved    = Attacker->bMovedThisTurn;
    const bool bAdvanced = Attacker->bAdvancedThisTurn;

    for (const FWeaponKeywordData& KData : W.Keywords)
    {
        const EWeaponKeyword K = KData.Type;
        if (K == EWeaponKeyword::None) continue;

        bool bActive = false;
        bool bCond   = false;

        switch (K)
        {
        // Situational: active if condition met; otherwise conditional (visible but grey)
        case EWeaponKeyword::Heavy:       bActive = !bMoved;                          bCond = !bActive; break;
        case EWeaponKeyword::Assault:     bActive = bAdvanced;                        bCond = !bActive; break;
        case EWeaponKeyword::RapidFire:   bActive = bAtHalf && KData.Value > 0;       bCond = (KData.Value > 0) && !bActive; break;
        case EWeaponKeyword::Piercing:    bActive = bAtHalf && KData.Value > 0;       bCond = (KData.Value > 0) && !bActive; break;

        // Always-on
        case EWeaponKeyword::Torrent:
        case EWeaponKeyword::Shred:       bActive = true; break;

        case EWeaponKeyword::TwinLinked:  bActive = (KData.bRerollAllWounds || KData.bRerollOnesWounds); break;

        // Active only when cover actually improves the save in your rules
        case EWeaponKeyword::IgnoresCover:
            bActive = WeaponHelpers::HasAnyCoverSaveBonus(HitMod, SaveMod);
            // Optional: bCond = !bActive; // show as “would do something if target had cover”
            break;

        // Crit/on-resolution effects → conditional
        case EWeaponKeyword::SustainedHits:
        case EWeaponKeyword::LethalHits:
        case EWeaponKeyword::DevastatingWounds:
        case EWeaponKeyword::Brutal:
        case EWeaponKeyword::Rending:
            bCond = true; break;

        // Informational (present on weapon, may trigger outside this calc)
        case EWeaponKeyword::Blast:
        case EWeaponKeyword::Hazardous:
        case EWeaponKeyword::Precision:
        case EWeaponKeyword::Suppressive:
        case EWeaponKeyword::Concussive:
            bCond = true; break;

        default: break;
        }

        FKeywordUIInfo Info;
        Info.Keyword      = K;
        Info.bActiveNow   = bActive;
        Info.bConditional = bCond;
        Info.State        = bActive ? EKeywordUIState::ActiveNow
                                    : (bCond ? EKeywordUIState::Conditional : EKeywordUIState::Inactive);
        Info.Label        = GetKeywordLabel(K);
        Info.Tooltip      = GetKeywordTooltip(K, &KData, bActive, bCond, bAtHalf);

        // ALWAYS add (no filter)
        Out.Add(Info);
    }

    // Optional: include keywords the profile doesn't list (if you want fixed order)
    // by iterating all enum values and adding missing ones with State=Inactive.
}
