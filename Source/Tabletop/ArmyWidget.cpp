
#include "ArmyWidget.h"

#include "ArmyData.h"
#include "NameUtils.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Controllers/SetupPlayerController.h"
#include "Gamemodes/SetupGamemode.h"

namespace
{
    // fixed tile size
    constexpr float kTile = 128.f;
    constexpr int32 kCols = 5; // tweak to taste
}

namespace FactionNameConverter
{
    static FString FactionDisplay(EFaction F)
    {
        if (UEnum* Enum = StaticEnum<EFaction>())
            return Enum->GetDisplayNameTextByValue((int64)F).ToString();
        return TEXT("Unknown");
    }
}


void UArmyWidget::BuildFactionGrid()
{
    if (!FactionGrid) return;
    FactionGrid->ClearChildren();
    ButtonToFaction.Empty();

    TArray<FFactionRow> Rows;

    if (ASetupGameState* S = GS(); S && S->FactionsTable)
    {
        for (const auto& KV : S->FactionsTable->GetRowMap())
        {
            if (const FFactionRow* Row = reinterpret_cast<const FFactionRow*>(KV.Value))
            {
                if (Row->Faction != EFaction::None)
                {
                    Rows.Add(*Row);
                }
            }
        }
    }

    // Fallback: build from enum with no icons (optional)
    if (Rows.Num() == 0)
    {
        if (const UEnum* Enum = StaticEnum<EFaction>())
        {
            const int32 Num = Enum->NumEnums();
            for (int32 i=0;i<Num;++i)
            {
#if WITH_EDITOR
                if (Enum->HasMetaData(TEXT("Hidden"), i) || Enum->HasMetaData(TEXT("HiddenByDefault"), i))
                    continue;
#endif
                const int64 Value = Enum->GetValueByIndex(i);
                if (!Enum->IsValidEnumValue(Value)) continue;
                const FName Name = Enum->GetNameByIndex(i);
                if (Name.ToString().EndsWith(TEXT("_MAX"))) continue;

                const EFaction F = static_cast<EFaction>(Value);
                if (F == EFaction::None) continue;

                FFactionRow R;
                R.Faction = F;
                R.DisplayName = FText::FromString(FactionNameConverter::FactionDisplay(F));
                R.Icon = nullptr; // no icon known here
                Rows.Add(R);
            }
        }
    }

    // Sort by display name
    Rows.Sort([](const FFactionRow& A, const FFactionRow& B){
        return A.DisplayName.ToString() < B.DisplayName.ToString();
    });

    // Build fixed-size tiles
    int32 idx = 0;
    for (const FFactionRow& R : Rows)
    {
        const int32 Row = idx / kCols;
        const int32 Col = idx % kCols;
        ++idx;

        USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
        Size->SetWidthOverride(kTile);
        Size->SetHeightOverride(kTile);

        // Button fills the SizeBox
        UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());

        // Make the button's content fill its slot
        if (UButtonSlot* BS = Cast<UButtonSlot>(Btn->Slot))
        {
            BS->SetHorizontalAlignment(HAlign_Fill);
            BS->SetVerticalAlignment(VAlign_Fill);
        }

        // ScaleBox makes the image fill uniformly (choose one)
        // EStretch::Fill        -> stretches (can distort aspect)
        // EStretch::ScaleToFit  -> letterboxes, preserves aspect
        // EStretch::ScaleToFill -> fills without letterbox, preserves aspect (may crop)
        UScaleBox* Scale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass());
        Scale->SetStretch(EStretch::Fill); // or ScaleToFit / ScaleToFill

        UImage* Img = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
        if (R.Icon)
        {
            Img->SetBrushFromTexture(R.Icon, /*bMatchSize*/ true);
        }
        else
        {
            // optional: placeholder tint
            Img->SetColorAndOpacity(FLinearColor(0.1f,0.1f,0.1f,1.f));
        }

        // IMPORTANT: don't set Brush.ImageSize or SetBrushSize here.
        // Let the ScaleBox control the final size.

        Scale->AddChild(Img);
        Btn->AddChild(Scale);
        Size->AddChild(Btn);

        // Add to grid (as you already do)
        if (UUniformGridSlot* CurrentSlot = FactionGrid->AddChildToUniformGrid(Size, Row, Col))
        {
            CurrentSlot->SetHorizontalAlignment(HAlign_Center);
            CurrentSlot->SetVerticalAlignment(VAlign_Center);
        }

        // Remember mapping and bind click
        ButtonToFaction.Add(Btn, R.Faction);
        Btn->OnClicked.AddDynamic(this, &UArmyWidget::HandleFactionTileClicked);
    }
}

static bool FactionFromString(const FString& S, EFaction& Out)
{
    const UEnum* Enum = StaticEnum<EFaction>();
    if (!Enum) return false;

    for (int32 i = 0; i < Enum->NumEnums(); ++i)
    {
#if WITH_EDITOR
        // Skip entries marked Hidden/HiddenByDefault in editor builds
        if (Enum->HasMetaData(TEXT("Hidden"), i) || Enum->HasMetaData(TEXT("HiddenByDefault"), i))
        {
            continue;
        }
#endif
        // Skip autogenerated _MAX sentinel if present
        if (Enum->GetNameByIndex(i).ToString().EndsWith(TEXT("_MAX")))
        {
            continue;
        }

        const int64 Value = Enum->GetValueByIndex(i);
        const FString Disp = Enum->GetDisplayNameTextByValue(Value).ToString();

        if (Disp.Equals(S, ESearchCase::CaseSensitive))
        {
            Out = static_cast<EFaction>(Value);
            return true;
        }
    }
    return false;
}

ASetupGameState* UArmyWidget::GS() const
{
    return GetWorld() ? GetWorld()->GetGameState<ASetupGameState>() : nullptr;
}

ASetupPlayerController* UArmyWidget::PC() const
{
    return GetOwningPlayer<ASetupPlayerController>();
}

void UArmyWidget::OnReadyUpChanged()
{
    RefreshFromState();
}

void UArmyWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (ASetupGameState* S = GS())
    {
        S->OnPlayerReadyUp.AddDynamic(this, &UArmyWidget::OnReadyUpChanged);
        S->OnArmySelectionChanged.AddDynamic(this, &UArmyWidget::RefreshFromState);
        S->OnPhaseChanged.AddDynamic(this, &UArmyWidget::RefreshFromState);
    }

    if (ASetupGameState* S = GS())
    {
        S->bP1Ready = false;
        S->bP2Ready = false;
    }

    // REMOVE: BuildFactionDropdown();
    BuildFactionGrid();

    if (P1ReadyBtn) P1ReadyBtn->OnClicked.AddDynamic(this, &UArmyWidget::OnP1ReadyClicked);
    if (P2ReadyBtn) P2ReadyBtn->OnClicked.AddDynamic(this, &UArmyWidget::OnP2ReadyClicked);
    if (BothReady)  BothReady ->OnClicked.AddDynamic(this, &UArmyWidget::OnBothReadyClicked);

    RefreshFromState();
}

void UArmyWidget::NativeDestruct()
{
    if (ASetupGameState* S = GS())
    {
        S->OnArmySelectionChanged.RemoveDynamic(this, &UArmyWidget::RefreshFromState);
        S->OnPhaseChanged.RemoveDynamic(this, &UArmyWidget::RefreshFromState);
    }
    Super::NativeDestruct();
}

void UArmyWidget::RefreshFromState()
{
    ASetupGameState* S = GS();
    ASetupPlayerController* LPC = PC();
    if (!S || !LPC) return;

    const FString N1 = UNameUtils::GetShortPlayerName(S->Player1);
    const FString N2 = UNameUtils::GetShortPlayerName(S->Player2);
    if (P1Name) P1Name->SetText(FText::FromString(N1));
    if (P2Name) P2Name->SetText(FText::FromString(N2));

    if (P1PickText) P1PickText->SetText(FText::FromString(FactionNameConverter::FactionDisplay(S->P1Faction)));
    if (P2PickText) P2PickText->SetText(FText::FromString(FactionNameConverter::FactionDisplay(S->P2Faction)));

    // Enable only your own Ready button
    const bool bLocalIsP1 = (LPC->PlayerState && LPC->PlayerState == S->Player1);
    const bool bLocalIsP2 = (LPC->PlayerState && LPC->PlayerState == S->Player2);
    if (P1ReadyBtn)
        P1ReadyBtn->SetIsEnabled(bLocalIsP1);
    if (P2ReadyBtn)
        P2ReadyBtn->SetIsEnabled(bLocalIsP2);

    // Host can advance when both ready & both picked
    if (BothReady)
    {
        const bool bHostSeat = bLocalIsP1; // host = player1
        const bool bAllReady = S->bP1Ready && S->bP2Ready;

        // Host can advance if both players ready and we're not already at final phase
        BothReady->SetIsEnabled(bHostSeat && bAllReady && S->Phase == ESetupPhase::ArmySelection);
    }
}

void UArmyWidget::OnFactionChanged(FString Selected, ESelectInfo::Type)
{
    EFaction Pick = EFaction::None;
    if (FactionFromString(Selected, Pick))
    {
        if (ASetupPlayerController* LPC = PC())
        {
            LPC->Server_SelectFaction(Pick);
        }
    }
}

void UArmyWidget::OnP1ReadyClicked()
{
    if (ASetupGameState* S = GS())
    if (ASetupPlayerController* LPC = PC())
    if (LPC->PlayerState == S->Player1)
        LPC->Server_SetReady(!S->bP1Ready);
}

void UArmyWidget::OnP2ReadyClicked()
{
    if (ASetupGameState* S = GS())
    if (ASetupPlayerController* LPC = PC())
    if (LPC->PlayerState == S->Player2)
        LPC->Server_SetReady(!S->bP2Ready);
}

void UArmyWidget::OnBothReadyClicked()
{
    if (ASetupGameState* S = GS())
    if (ASetupPlayerController* LPC = PC())
    if (LPC->PlayerState == S->Player1 && S->bP1Ready && S->bP2Ready &&
        S->P1Faction != EFaction::None && S->P2Faction != EFaction::None)
    {
        LPC->Server_SnapshotSetupToPS();
        LPC->Server_AdvanceFromArmy();
    }
}

void UArmyWidget::HandleFactionTileClicked()
{
    // Find which button fired
    UButton* Sender = nullptr;

    // Unreal’s dynamic delegates don’t pass the sender, so we query focus/hover as a practical workaround:
    // (If you want a rock-solid pattern, use a small custom tile widget that stores EFaction and binds through it.)
    for (const auto& KV : ButtonToFaction)
    {
        if (KV.Key && (KV.Key->HasKeyboardFocus() || KV.Key->IsHovered()))
        {
            Sender = KV.Key;
            break;
        }
    }

    if (!Sender) return;

    if (EFaction* Pick = ButtonToFaction.Find(Sender))
    {
        if (ASetupPlayerController* LPC = PC())
        {
            LPC->Server_SelectFaction(*Pick);
        }
    }
}
