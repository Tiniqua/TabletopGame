#include "WorldspaceUnitStatusWidget.h"

#include "Components/TextBlock.h"
#include "Tabletop/LibraryHelpers.h"
#include "Tabletop/Actors/UnitBase.h"

#define LOCTEXT_NAMESPACE "WorldspaceUnitStatusWidget"

namespace
{
    FString FormatModifierEntry(const FUnitModifier& Mod)
    {
        FString Label;
        if (!Mod.DisplayName.IsEmpty())
        {
            Label = Mod.DisplayName.ToString();
        }
        else if (Mod.SourceId != NAME_None)
        {
            Label = Mod.SourceId.ToString();
        }

        if (Label.IsEmpty())
        {
            return FString();
        }

        FString Suffix;
        switch (Mod.Expiry)
        {
        case EModifierExpiry::NextNOwnerShots:
        case EModifierExpiry::Uses:
            if (Mod.UsesRemaining > 0)
            {
                Suffix = FString::Printf(TEXT(" (%d use%s)"), Mod.UsesRemaining, Mod.UsesRemaining == 1 ? TEXT("") : TEXT("s"));
            }
            break;
        case EModifierExpiry::UntilEndOfTurn:
        case EModifierExpiry::UntilEndOfRound:
            if (Mod.TurnsRemaining > 0)
            {
                Suffix = FString::Printf(TEXT(" (%d turn%s)"), Mod.TurnsRemaining, Mod.TurnsRemaining == 1 ? TEXT("") : TEXT("s"));
            }
            break;
        default:
            break;
        }

        return Label + Suffix;
    }
}

FString UWorldspaceUnitStatusWidget::BuildActionsSummary(const AUnitBase* Unit) const
{
    if (!Unit)
    {
        return FString();
    }

    TArray<FString> Entries;
    for (const FUnitModifier& Mod : Unit->ActiveCombatMods)
    {
        const FString Entry = FormatModifierEntry(Mod);
        if (!Entry.IsEmpty())
        {
            Entries.Add(Entry);
        }
    }

    if (Unit->bOverwatchArmed)
    {
        Entries.Add(TEXT("Overwatch (armed)"));
    }

    if (Entries.Num() == 0)
    {
        return FString();
    }

    FString Result;
    for (int32 i = 0; i < Entries.Num(); ++i)
    {
        Result += Entries[i];
        if (i + 1 < Entries.Num())
        {
            Result += TEXT("\n");
        }
    }
    return Result;
}

void UWorldspaceUnitStatusWidget::ApplyUnitStatus(AUnitBase* Unit, ECoverType CoverType, bool bHasCoverInfo)
{
    if (NameText)
    {
        if (Unit)
        {
            const FText DisplayName = !Unit->UnitName.IsEmpty() ? Unit->UnitName : FText::FromName(Unit->UnitId);
            NameText->SetText(DisplayName);
        }
        else
        {
            NameText->SetText(FText::GetEmpty());
        }
    }

    if (WoundsText)
    {
        if (Unit)
        {
            const int32 MaxWounds = Unit->ModelsMax * FMath::Max(1, Unit->WoundsRep);
            WoundsText->SetText(FText::Format(LOCTEXT("WoundsFmt", "{0} / {1}"), FText::AsNumber(Unit->WoundsPool), FText::AsNumber(MaxWounds)));
        }
        else
        {
            WoundsText->SetText(FText::GetEmpty());
        }
    }

    if (CoverText)
    {
        if (Unit && bHasCoverInfo)
        {
            CoverText->SetText(FText::Format(LOCTEXT("CoverFmt", "Cover: {0}"), FText::FromString(LibraryHelpers::CoverTypeToText(CoverType))));
        }
        else if (Unit)
        {
            CoverText->SetText(LOCTEXT("CoverUnknown", "Cover: --"));
        }
        else
        {
            CoverText->SetText(FText::GetEmpty());
        }
    }

    if (ActiveActionsText)
    {
        if (Unit)
        {
            const FString Summary = BuildActionsSummary(Unit);
            if (!Summary.IsEmpty())
            {
                ActiveActionsText->SetText(FText::FromString(Summary));
            }
            else
            {
                ActiveActionsText->SetText(LOCTEXT("NoActions", "No active actions"));
            }
        }
        else
        {
            ActiveActionsText->SetText(FText::GetEmpty());
        }
    }
}

#undef LOCTEXT_NAMESPACE
