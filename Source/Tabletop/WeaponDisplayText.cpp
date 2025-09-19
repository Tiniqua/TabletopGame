#include "WeaponDisplayText.h"

#include "Actors/UnitAbility.h"
#include "WeaponKeywords.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace WeaponDisplayText
{
namespace
{
    FString AbilityClassToDisplay(TSubclassOf<UUnitAbility> C)
    {
        if (!*C) return TEXT("");
        const UClass* Cls = *C;

        if (const UObject* CDO = Cls->GetDefaultObject())
        {
            if (const FTextProperty* P = FindFProperty<FTextProperty>(Cls, TEXT("DisplayName")))
            {
                const FText V = P->GetPropertyValue_InContainer(CDO);
                if (!V.IsEmpty()) return V.ToString();
            }
            if (const FTextProperty* P2 = FindFProperty<FTextProperty>(Cls, TEXT("AbilityName")))
            {
                const FText V = P2->GetPropertyValue_InContainer(CDO);
                if (!V.IsEmpty()) return V.ToString();
            }
        }

        return Cls->GetDisplayNameText().ToString();
    }

    bool TryGetKeywordEnumValue(const FWeaponKeywordData& K, int64& OutVal)
    {
        const UScriptStruct* SS = FWeaponKeywordData::StaticStruct();
        if (!SS) return false;

        static const FName Candidates[] = { TEXT("Keyword"), TEXT("Type"), TEXT("Id"), TEXT("Key"), TEXT("Enum") };

        for (const FName& FieldName : Candidates)
        {
            if (const FProperty* P = SS->FindPropertyByName(FieldName))
            {
                if (const FEnumProperty* EP = CastField<FEnumProperty>(P))
                {
                    const void* Ptr = EP->ContainerPtrToValuePtr<void>(&K);
                    OutVal = (int64)EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(Ptr);
                    return true;
                }
                if (const FByteProperty* BP = CastField<FByteProperty>(P))
                {
                    const void* Ptr = BP->ContainerPtrToValuePtr<void>(&K);
                    OutVal = (int64)BP->GetPropertyValue(Ptr);
                    return true;
                }
                if (const FNumericProperty* NP = CastField<FNumericProperty>(P))
                {
                    const void* Ptr = NP->ContainerPtrToValuePtr<void>(&K);
                    OutVal = (int64)NP->GetSignedIntPropertyValue(Ptr);
                    return true;
                }
            }
        }
        return false;
    }

    bool TryGetKeywordName(const FWeaponKeywordData& K, FString& OutName)
    {
        const UScriptStruct* SS = FWeaponKeywordData::StaticStruct();
        if (!SS) return false;

        static const FName Candidates[] = { TEXT("Name"), TEXT("Id"), TEXT("KeywordName"), TEXT("Tag") };

        for (const FName& FieldName : Candidates)
        {
            if (const FNameProperty* NP = FindFProperty<FNameProperty>(SS, FieldName))
            {
                const void* Ptr = NP->ContainerPtrToValuePtr<void>(&K);
                OutName = NP->GetPropertyValue(Ptr).ToString();
                return !OutName.IsEmpty();
            }
            if (const FStrProperty* SP = FindFProperty<FStrProperty>(SS, FieldName))
            {
                const void* Ptr = SP->ContainerPtrToValuePtr<void>(&K);
                OutName = SP->GetPropertyValue(Ptr);
                return !OutName.IsEmpty();
            }
        }
        return false;
    }

    int32 ReadKeywordValue(const FWeaponKeywordData& K)
    {
        const UScriptStruct* SS = FWeaponKeywordData::StaticStruct();
        if (!SS) return 0;
        if (const FIntProperty* IP = FindFProperty<FIntProperty>(SS, TEXT("Value")))
        {
            const void* Ptr = IP->ContainerPtrToValuePtr<void>(&K);
            return IP->GetPropertyValue(Ptr);
        }
        return 0;
    }
}

FString FormatAbilityList(const TArray<TSubclassOf<UUnitAbility>>& Abilities, const FString& Prefix)
{
    const FString& UsePrefix = Prefix.IsEmpty() ? TEXT("Abilities") : Prefix;

    if (Abilities.Num() == 0)
    {
        return FString::Printf(TEXT("%s: —"), *UsePrefix);
    }

    TArray<FString> Parts;
    Parts.Reserve(Abilities.Num());
    for (TSubclassOf<UUnitAbility> C : Abilities)
    {
        if (*C) Parts.Add(AbilityClassToDisplay(C));
    }

    if (Parts.Num() == 0)
    {
        return FString::Printf(TEXT("%s: —"), *UsePrefix);
    }

    return FString::Printf(TEXT("%s: %s"), *UsePrefix, *FString::Join(Parts, TEXT(", ")));
}

FString FormatWeaponKeywords(const TArray<FWeaponKeywordData>& Keywords, const FString& Prefix)
{
    const FString& UsePrefix = Prefix.IsEmpty() ? TEXT("Keywords") : Prefix;

    if (Keywords.Num() == 0)
    {
        return FString::Printf(TEXT("%s: —"), *UsePrefix);
    }

    const UEnum* Enum = StaticEnum<EWeaponKeyword>();

    TArray<FString> Parts;
    Parts.Reserve(Keywords.Num());

    for (const FWeaponKeywordData& K : Keywords)
    {
        FString Label;
        int64 EnumVal = 0;

        if (Enum && TryGetKeywordEnumValue(K, EnumVal))
        {
            Label = Enum->GetDisplayNameTextByValue(EnumVal).ToString();
            if (Label.IsEmpty())
            {
                Label = Enum->GetNameStringByValue(EnumVal);
            }
        }
        else
        {
            if (!TryGetKeywordName(K, Label))
            {
                Label = TEXT("Keyword");
            }
        }

        const int32 Value = ReadKeywordValue(K);
        if (Value != 0)
        {
            Label += FString::Printf(TEXT(" %d"), Value);
        }

        Parts.Add(Label);
    }

    if (Parts.Num() == 0)
    {
        return FString::Printf(TEXT("%s: —"), *UsePrefix);
    }

    return FString::Printf(TEXT("%s: %s"), *UsePrefix, *FString::Join(Parts, TEXT(", ")));
}

FString FormatWeaponStats(const FWeaponProfile& W)
{
    return FString::Printf(TEXT("Rng %d\"  A %d  S %d  AP %d  D %d"),
                           W.RangeInches, W.Attacks, W.Strength, W.AP, W.Damage);
}

} // namespace WeaponDisplayText

