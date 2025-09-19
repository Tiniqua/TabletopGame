#pragma once

#include "CoreMinimal.h"
#include "ArmyData.h"

class UUnitAbility;

namespace WeaponDisplayText
{
TABLETOP_API FString FormatWeaponStats(const FWeaponProfile& W);

TABLETOP_API FString FormatWeaponKeywords(const TArray<FWeaponKeywordData>& Keywords,
                                          const FString& Prefix = TEXT("Keywords"));

TABLETOP_API FString FormatAbilityList(const TArray<TSubclassOf<UUnitAbility>>& Abilities,
                                       const FString& Prefix = TEXT("Abilities"));
}

