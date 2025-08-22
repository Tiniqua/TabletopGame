
#include "UnitRowWidget.h"

#include "Components/Button.h"
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

    // Set initial count from GS if already present
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
            if (LPC->PlayerState)
            {
                const bool bIsP1 = (LPC->PlayerState == S->Player1);
                const TArray<FUnitCount>& R = bIsP1 ? S->P1Roster : S->P2Roster;
                for (const FUnitCount& E : R)
                    if (E.UnitId == UnitId) return E.Count;
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
    {
        NewCount = FMath::Clamp(NewCount, 0, 99);
        LPC->Server_SetUnitCount(UnitId, NewCount);
    }
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