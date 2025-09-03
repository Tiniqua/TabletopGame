// WeaponKeywords.h
#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h" // optional but handy for conditions
#include "WeaponKeywords.generated.h"

UENUM(BlueprintType)
enum class EWeaponKeyword : uint8
{
    None             UMETA(DisplayName="None"),

    // Core 40k-like
    Assault          UMETA(DisplayName="Assault"),
    Heavy            UMETA(DisplayName="Heavy"),
    RapidFire        UMETA(DisplayName="Rapid Fire"),   // Value = bonus Attacks at ≤ half range (often +1*A or x2)
    SustainedHits    UMETA(DisplayName="Sustained Hits"), // Value = extra hits on crits
    LethalHits       UMETA(DisplayName="Lethal Hits"),  // crits to-hit auto-wound
    TwinLinked       UMETA(DisplayName="Twin-linked"),  // re-roll wound (often 1s or all; use flags)
    DevastatingWounds UMETA(DisplayName="Devastating Wounds"), // crits to-wound ignore save / become mortals
    Blast            UMETA(DisplayName="Blast"),        // bonus attacks vs large units
    Hazardous        UMETA(DisplayName="Hazardous"),    // risk to bearer after firing
    Torrent          UMETA(DisplayName="Auto-Hit/Torrent"), // attacks auto-hit

    Precision        UMETA(DisplayName="Precision"),    // can snipe key models (game-specific handling)
    IgnoresCover     UMETA(DisplayName="Ignores Cover"),// bypass cover mod in save calc

    // Custom / new
    Suppressive      UMETA(DisplayName="Suppressive"),  // apply -Hit or -Move debuff on unsaved
    Piercing         UMETA(DisplayName="Piercing"),     // +AP at ≤ half range (Value = AP bonus)
    Concussive       UMETA(DisplayName="Concussive"),   // reduce OC or Move on hit
    Shred            UMETA(DisplayName="Shred"),        // re-roll 1s to wound
    Brutal           UMETA(DisplayName="Brutal"),       // 6s to wound add +Damage (Value = +D)
    Rending         UMETA(DisplayName="Rending"),       // crit to-wound increases AP (Value = extra AP)
};

/** One keyword entry. Value is a generic magnitude (0 = off),
    and Flags fine-tune behavior (e.g., "re-roll all" vs "re-roll 1s").  */
USTRUCT(BlueprintType)
struct FWeaponKeywordData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) EWeaponKeyword Type = EWeaponKeyword::None;

    // Magnitude / threshold / stacks (contextual per keyword). Examples:
    // RapidFire: +Attacks at half range, SustainedHits: extra hits per crit, Piercing: +AP at half range,
    // Brutal: +Damage on crit to-wound, Rending: +AP on crit to-wound.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Value = 0;

    // Optional behavior toggles, usable by multiple keywords.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRerollAllHits   = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRerollOnesHits  = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRerollAllWounds = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRerollOnesWounds= false;

    // Optional tags for conditional “Anti-X 4+” style or “Target: Vehicle” checks.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGameplayTagContainer RequiresTargetTags;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGameplayTagContainer RequiresAttackerTags;
};
