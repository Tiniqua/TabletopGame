
#include "UnitRowWidget.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/TextBlock.h"
#include "Controllers/SetupPlayerController.h"
#include "Gamemodes/SetupGamemode.h"

ASetupGameState* UUnitRowWidget::GS() const { return GetWorld() ? GetWorld()->GetGameState<ASetupGameState>() : nullptr; }
ASetupPlayerController* UUnitRowWidget::PC() const { return GetOwningPlayer<ASetupPlayerController>(); }

void UUnitRowWidget::Init(FName InUnitId, const FText& InDisplayName, int32 InUnitPoints)
{
    UnitId = InUnitId;
    UnitPoints = InUnitPoints;

    if (NameText)   NameText->SetText(InDisplayName);
    if (PointsText) PointsText->SetText(FText::AsNumber(UnitPoints));

    if (ASetupGameState* S = GS())
    {
        UDataTable* UnitsTable = S->GetUnitsDTForLocalSeat(PC()->PlayerState);
        if (const FUnitRow* Row = UnitsTable ? UnitsTable->FindRow<FUnitRow>(UnitId, TEXT("UnitRowWeaponList")) : nullptr)
        {
            if (WeaponCombo)
            {
                WeaponCombo->ClearOptions();
                WeaponDisplay.Reset();
                for (int32 i=0;i<Row->Weapons.Num();++i)
                {
                    const FWeaponProfile& W = Row->Weapons[i];
                    const FString Label = FString::Printf(TEXT("%s  (R%d A%d S%d AP%d D%d)"),
                        *W.WeaponId.ToString(), W.RangeInches, W.Attacks, W.Strength, W.AP, W.Damage);
                    WeaponDisplay.Add(*Label);
                    WeaponCombo->AddOption(Label);
                }
                WeaponCombo->SetSelectedIndex(0);
                CurrentWeaponIdx = 0;
                WeaponCombo->OnSelectionChanged.AddDynamic(this, &UUnitRowWidget::OnWeaponChanged);
            }
        }
    }

    // Set initial count from GS if already present
    ApplyCountToUI(GetLocalSeatCount());
}

void UUnitRowWidget::OnWeaponChanged(FString /*Selected*/, ESelectInfo::Type)
{
    if (WeaponCombo)
        CurrentWeaponIdx = WeaponCombo->GetSelectedIndex();
    ApplyCountToUI(GetLocalSeatCount());
}

void UUnitRowWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (MinusBtn) MinusBtn->OnClicked.AddDynamic(this, &UUnitRowWidget::HandleMinus);
    if (PlusBtn)  PlusBtn ->OnClicked.AddDynamic(this, &UUnitRowWidget::HandlePlus);

    if (ASetupGameState* S = GS())
    {
        // Refresh count whenever roster changes (self or opponent)
        S->OnRosterChanged.AddDynamic(this, &UUnitRowWidget::HandleRosterChanged);
    }

    // Ensure UI matches current state
    ApplyCountToUI(GetLocalSeatCount());
}

void UUnitRowWidget::NativeDestruct()
{
    if (ASetupGameState* S = GS())
    {
        S->OnRosterChanged.RemoveDynamic(this, &UUnitRowWidget::HandleRosterChanged);
    }
    Super::NativeDestruct();
}

int32 UUnitRowWidget::GetLocalSeatCount() const
{
    if (const ASetupGameState* S = GS())
        if (const ASetupPlayerController* LPC = PC())
        {
            const bool bP1 = (LPC->PlayerState == S->Player1);
            return S->GetCountFor(UnitId, CurrentWeaponIdx, bP1);
        }
    return 0;
}

void UUnitRowWidget::ApplyCountToUI(int32 Count) const
{
    if (CountText) CountText->SetText(FText::AsNumber(FMath::Max(0, Count)));
}

void UUnitRowWidget::SendCountToServer(int32 NewCount) const
{
    if (ASetupPlayerController* LPC = PC())
        LPC->Server_SetUnitCount(UnitId, CurrentWeaponIdx, FMath::Clamp(NewCount,0,99));
}

void UUnitRowWidget::HandleMinus()
{
    const int32 Curr = GetLocalSeatCount();
    if (Curr > 0)
    {
        SendCountToServer(Curr - 1);
    }
}

void UUnitRowWidget::HandlePlus()
{
    const int32 Curr = GetLocalSeatCount();
    SendCountToServer(Curr + 1);
}

void UUnitRowWidget::HandleRosterChanged()
{
    ApplyCountToUI(GetLocalSeatCount());
}