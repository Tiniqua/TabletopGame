#include "ArmyData.h"

bool UArmyData::GetFactionRow(const FName RowName, FFactionRow& OutRow) const
{
    if (!FactionsTable) return false;

    if (const FFactionRow* Row = FactionsTable->FindRow<FFactionRow>(RowName, TEXT("GetFactionRow")))
    {
        OutRow = *Row;
        return true;
    }
    return false;
}

void UArmyData::GetUnitsForFaction(EFaction Faction, TArray<FUnitRow>& OutRows) const
{
    OutRows.Reset();
    if (!FactionsTable) return;

    static const FString Ctx = TEXT("GetUnitsForFaction");

    // loop through all factions, find the matching one
    for (const auto& Pair : FactionsTable->GetRowMap())
    {
        const FFactionRow* FRow = reinterpret_cast<const FFactionRow*>(Pair.Value);
        if (FRow && FRow->Faction == Faction && FRow->UnitsTable)
        {
            // pull all rows from that faction's UnitsTable
            for (const auto& UnitPair : FRow->UnitsTable->GetRowMap())
            {
                if (const FUnitRow* URow = reinterpret_cast<const FUnitRow*>(UnitPair.Value))
                {
                    OutRows.Add(*URow);
                }
            }
            return; // found the faction → exit
        }
    }
}

int32 UArmyData::ComputeRosterPoints(UDataTable* UnitsTable, const TMap<FName, int32>& RowCounts) const
{
    if (!UnitsTable) return 0;

    int32 Total = 0;
    for (const auto& It : RowCounts)
    {
        const FName RowName = It.Key;
        const int32 Count = It.Value;

        if (const FUnitRow* Row = UnitsTable->FindRow<FUnitRow>(RowName, TEXT("ComputeRosterPoints")))
        {
            if (Count > 0)
            {
                Total += Row->Points * Count; // MVP: cost per squad
            }
        }
    }
    return Total;
}
