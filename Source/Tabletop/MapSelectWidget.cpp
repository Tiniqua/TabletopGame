#include "MapSelectWidget.h"
#include "Components/TextBlock.h"
#include "Components/ComboBoxString.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Gamemodes/SetupGamemode.h"
#include "Controllers/SetupPlayerController.h"
#include "Tabletop/ArmyData.h"
#include "Tabletop/MapData.h"
#include "GameFramework/PlayerState.h"

ASetupGameState* UMapSelectWidget::GS() const { return GetWorld()? GetWorld()->GetGameState<ASetupGameState>() : nullptr; }
ASetupPlayerController* UMapSelectWidget::PC() const { return GetOwningPlayer<ASetupPlayerController>(); }

FString UMapSelectWidget::FactionDisplay(EFaction F)
{
    if (UEnum* E = StaticEnum<EFaction>())
        return E->GetDisplayNameTextByValue((int64)F).ToString();
    return TEXT("None");
}

void UMapSelectWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (ASetupGameState* S = GS())
    {
        S->OnPhaseChanged.AddDynamic(this, &UMapSelectWidget::OnPhaseChanged);
        S->OnPlayerReadyUp.AddDynamic(this, &UMapSelectWidget::OnReadyUpChanged);
        S->OnRosterChanged.AddDynamic(this, &UMapSelectWidget::OnRosterChanged);
        S->OnArmySelectionChanged.AddDynamic(this, &UMapSelectWidget::OnArmyChanged);
        S->OnMapSelectionChanged.AddDynamic(this, &UMapSelectWidget::OnMapChanged);
    }

    if (P1ReadyBtn) P1ReadyBtn->OnClicked.AddDynamic(this, &UMapSelectWidget::OnP1ReadyClicked);
    if (P2ReadyBtn) P2ReadyBtn->OnClicked.AddDynamic(this, &UMapSelectWidget::OnP2ReadyClicked);
    if (BothReady)  BothReady ->OnClicked.AddDynamic(this, &UMapSelectWidget::OnBothReadyClicked);

    if (MapDropdown)
    {
        MapDropdown->OnSelectionChanged.AddDynamic(this, &UMapSelectWidget::OnMapDropdownChanged);
    }

    BuildMapDropdown();    // if table already replicated
    RefreshFromState();
}

void UMapSelectWidget::NativeDestruct()
{
    if (ASetupGameState* S = GS())
    {
        S->OnPhaseChanged.RemoveDynamic(this, &UMapSelectWidget::OnPhaseChanged);
        S->OnPlayerReadyUp.RemoveDynamic(this, &UMapSelectWidget::OnReadyUpChanged);
        S->OnRosterChanged.RemoveDynamic(this, &UMapSelectWidget::OnRosterChanged);
        S->OnArmySelectionChanged.RemoveDynamic(this, &UMapSelectWidget::OnArmyChanged);
        S->OnMapSelectionChanged.RemoveDynamic(this, &UMapSelectWidget::OnMapChanged);
    }
    Super::NativeDestruct();
}

void UMapSelectWidget::OnPhaseChanged()  { BuildMapDropdown(); RefreshFromState(); }
void UMapSelectWidget::OnReadyUpChanged(){ RefreshFromState(); }
void UMapSelectWidget::OnRosterChanged() { RefreshFromState(); }
void UMapSelectWidget::OnArmyChanged()   { RefreshFromState(); }
void UMapSelectWidget::OnMapChanged()    { RefreshFromState(); }

void UMapSelectWidget::BuildMapDropdown()
{
    if (!MapDropdown) return;

    ASetupGameState* S = GS();
    if (!S || !S->MapsTable || S->Phase != ESetupPhase::MapSelection) return;

    DisplayToRow.Reset();
    MapDropdown->ClearOptions();

    for (const auto& Pair : S->MapsTable->GetRowMap())
    {
        const FName RowName = Pair.Key;
        if (const FMapRow* Row = reinterpret_cast<const FMapRow*>(Pair.Value))
        {
            const FString Disp = Row->DisplayName.ToString();
            DisplayToRow.Add(Disp, RowName);
            MapDropdown->AddOption(Disp);
        }
    }

    // preselect current if any
    if (!S->SelectedMapRow.IsNone())
    {
        if (const FMapRow* Row = S->MapsTable->FindRow<FMapRow>(S->SelectedMapRow, TEXT("Preselect")))
        {
            MapDropdown->SetSelectedOption(Row->DisplayName.ToString());
        }
    }
}

void UMapSelectWidget::RebuildRosterPanels()
{
    if (!P1RosterList || !P2RosterList) return;
    ASetupGameState* S = GS();
    if (!S) return;

    auto BuildList = [&](UPanelWidget* Panel, const TArray<FUnitCount>& Roster, UDataTable* UnitsForFaction)
    {
        Panel->ClearChildren();
        if (!UnitsForFaction) return;

        for (const FUnitCount& E : Roster)
        {
            const FUnitRow* Unit = UnitsForFaction->FindRow<FUnitRow>(E.UnitId, TEXT("RosterView"));
            if (!Unit || E.Count <= 0) continue;

            UTextBlock* Line = NewObject<UTextBlock>(Panel);
            Line->SetText(FText::FromString(FString::Printf(TEXT("%s x%d"), *Unit->DisplayName.ToString(), E.Count)));
            Panel->AddChild(Line);
        }
    };

    // Resolve each faction's units table
    auto ResolveUnits = [&](EFaction Faction)->UDataTable*
    {
        if (!S->FactionsTable) return nullptr;
        for (const auto& Pair : S->FactionsTable->GetRowMap())
        {
            if (const FFactionRow* FR = reinterpret_cast<const FFactionRow*>(Pair.Value))
            {
                if (FR->Faction == Faction) return FR->UnitsTable;
            }
        }
        return nullptr;
    };

    UDataTable* P1Units = ResolveUnits(S->P1Faction);
    UDataTable* P2Units = ResolveUnits(S->P2Faction);

    BuildList(P1RosterList, S->P1Roster, P1Units);
    BuildList(P2RosterList, S->P2Roster, P2Units);
}

void UMapSelectWidget::RefreshFromState()
{
    ASetupGameState* S = GS();
    ASetupPlayerController* LPC = PC();
    if (!S || !LPC) return;

    // Names
    if (P1Name) P1Name->SetText(FText::FromString(S->Player1 ? S->Player1->GetPlayerName() : TEXT("---")));
    if (P2Name) P2Name->SetText(FText::FromString(S->Player2 ? S->Player2->GetPlayerName() : TEXT("---")));

    // Faction names
    if (P1FactionText) P1FactionText->SetText(FText::FromString(FactionDisplay(S->P1Faction)));
    if (P2FactionText) P2FactionText->SetText(FText::FromString(FactionDisplay(S->P2Faction)));

    // Map preview (from SelectedMapRow)
    if (MapPreview && S->MapsTable && !S->SelectedMapRow.IsNone())
    {
        if (const FMapRow* Row = S->MapsTable->FindRow<FMapRow>(S->SelectedMapRow, TEXT("Preview")))
        {
            MapPreview->SetBrushFromTexture(Row->Preview, true);
        }
    }

    // Roster lists
    RebuildRosterPanels();

    // Buttons enablement
    const bool bLocalIsP1 = (LPC->PlayerState && LPC->PlayerState == S->Player1);
    const bool bLocalIsP2 = (LPC->PlayerState && LPC->PlayerState == S->Player2);

    if (P1ReadyBtn) P1ReadyBtn->SetIsEnabled(bLocalIsP1);
    if (P2ReadyBtn) P2ReadyBtn->SetIsEnabled(bLocalIsP2);

    if (MapDropdown)  // host-only control
        MapDropdown->SetIsEnabled(bLocalIsP1 && S->Phase == ESetupPhase::MapSelection);

    const bool bHostSeat  = bLocalIsP1;
    const bool bBothReady = (S->bP1Ready && S->bP2Ready);
    const bool bHasMap    = (!S->SelectedMapRow.IsNone());
    if (BothReady) BothReady->SetIsEnabled(bHostSeat && bBothReady && bHasMap && S->Phase == ESetupPhase::MapSelection);
}

void UMapSelectWidget::OnMapDropdownChanged(FString Selected, ESelectInfo::Type)
{
    if (ASetupGameState* S = GS())
        if (ASetupPlayerController* LPC = PC())
            if (LPC->PlayerState == S->Player1) // host only
                if (FName* RowName = DisplayToRow.Find(Selected))
                    LPC->Server_SelectMapRow(*RowName);
}

void UMapSelectWidget::OnP1ReadyClicked()
{
    if (ASetupGameState* S = GS())
    if (ASetupPlayerController* LPC = PC())
    if (LPC->PlayerState == S->Player1)
        LPC->Server_SetReady(!S->bP1Ready);
}

void UMapSelectWidget::OnP2ReadyClicked()
{
    if (ASetupGameState* S = GS())
    if (ASetupPlayerController* LPC = PC())
    if (LPC->PlayerState == S->Player2)
        LPC->Server_SetReady(!S->bP2Ready);
}

void UMapSelectWidget::OnBothReadyClicked()
{
    if (ASetupGameState* S = GS())
    if (ASetupPlayerController* LPC = PC())
    if (LPC->PlayerState == S->Player1 && S->bP1Ready && S->bP2Ready && !S->SelectedMapRow.IsNone())
    {
        LPC->Server_AdvanceFromMap();
    }
}