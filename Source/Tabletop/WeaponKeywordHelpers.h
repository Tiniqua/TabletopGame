// WeaponKeywordHelpers.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WeaponKeywords.h"
#include "WeaponKeywordHelpers.generated.h"

struct FWeaponProfile;
class AUnitBase;

UENUM(BlueprintType)
enum class EKeywordUIState : uint8
{
	ActiveNow       UMETA(DisplayName="Active"),
	Conditional     UMETA(DisplayName="Conditional"),
	Inactive        UMETA(DisplayName="Inactive"),
};

USTRUCT(BlueprintType)
struct FKeywordUIInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) EWeaponKeyword Keyword = EWeaponKeyword::None;
	UPROPERTY(BlueprintReadOnly) FText Label;
	UPROPERTY(BlueprintReadOnly) FText Tooltip;

	UPROPERTY(BlueprintReadOnly) bool bActiveNow = false;
	UPROPERTY(BlueprintReadOnly) bool bConditional = false;

	// NEW: single state you can bind to for visuals
	UPROPERTY(BlueprintReadOnly) EKeywordUIState State = EKeywordUIState::Inactive;
};

/**
 * Keyword → UI helper (labels, tooltips, and “is it relevant now?” logic).
 * Keep as a class (no namespaces) to avoid linker/IntelliSense headaches.
 */
UCLASS()
class TABLETOP_API UWeaponKeywordHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Build the list of keyword chips relevant to the current previewed shot. */
	UFUNCTION(BlueprintCallable, Category="Tabletop|Keywords")
	static void BuildKeywordUIInfos(const AUnitBase* Attacker,
									const AUnitBase* Target,
									int32 HitMod, int32 SaveMod,
									TArray<FKeywordUIInfo>& Out);

	/** Label for a keyword (localized). */
	static FText GetKeywordLabel(EWeaponKeyword Keyword);

	/** Tooltip text explaining the keyword in the current context. */
	static FText GetKeywordTooltip(EWeaponKeyword Keyword,
								   const FWeaponKeywordData* Data,
								   bool bActiveNow, bool bConditional, bool bAtHalfRange);
								   
	static const FWeaponKeywordData* FindKeyword(const FWeaponProfile& W, EWeaponKeyword K);
    static bool HasKeyword(const FWeaponProfile& W, EWeaponKeyword K);
    static int32 KeywordValue(const FWeaponProfile& W, EWeaponKeyword K, int32 Default = 0);
};
