// WeaponLoadoutRowWidget.cpp
#include "WeaponLoadoutRowWidget.h"

#include "WeaponDisplayText.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Gamemodes/SetupGamemode.h"
#include "Controllers/SetupPlayerController.h"

ASetupGameState* UWeaponLoadoutRowWidget::GS() const { return GetWorld()? GetWorld()->GetGameState<ASetupGameState>() : nullptr; }
ASetupPlayerController* UWeaponLoadoutRowWidget::PC() const { return GetOwningPlayer<ASetupPlayerController>(); }

void UWeaponLoadoutRowWidget::Init(FName InUnitId, int32 InWeaponIndex, const FWeaponProfile& W)
{
	UnitId = InUnitId;
	WeaponIndex = InWeaponIndex;
	CachedWeapon = W;

        if (WL_NameText)      WL_NameText     ->SetText(FText::FromName(W.WeaponId));
        if (WL_StatsText)     WL_StatsText    ->SetText(FText::FromString(WeaponDisplayText::FormatWeaponStats(W)));
        if (WL_KeywordsText)  WL_KeywordsText ->SetText(FText::FromString(WeaponDisplayText::FormatWeaponKeywords(W.Keywords)));
        if (WL_AbilitiesText) WL_AbilitiesText->SetText(FText::FromString(WeaponDisplayText::FormatAbilityList(W.AbilityClasses)));

	// If you want per-loadout points, set them here (or reuse unit points)
	if (WL_PointsText)
	{
		// Replace with per-loadout cost if you have it; using 0 or unit cost as placeholder:
		WL_PointsText->SetText(FText::GetEmpty());
	}

	RefreshFromState();
}

void UWeaponLoadoutRowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (WL_MinusBtn) WL_MinusBtn->OnClicked.AddDynamic(this, &UWeaponLoadoutRowWidget::HandleMinus);
	if (WL_PlusBtn)  WL_PlusBtn ->OnClicked.AddDynamic(this, &UWeaponLoadoutRowWidget::HandlePlus);

	if (ASetupGameState* S = GS())
		S->OnRosterChanged.AddDynamic(this, &UWeaponLoadoutRowWidget::HandleRosterChanged);

	UE_LOG(LogTemp, Warning, TEXT("[LoadoutRow] Construct %p  UnitId=%s  WIdx=%d  Parent=%s"),
		this, *UnitId.ToString(), WeaponIndex, *GetNameSafe(GetParent()));
}

void UWeaponLoadoutRowWidget::NativeDestruct()
{
	if (ASetupGameState* S = GS())
		S->OnRosterChanged.RemoveDynamic(this, &UWeaponLoadoutRowWidget::HandleRosterChanged);
	Super::NativeDestruct();
}

void UWeaponLoadoutRowWidget::HandleMinus()
{
	const int32 Curr = GetLocalSeatCount();
	if (Curr > 0) SendCountToServer(Curr - 1);
}

void UWeaponLoadoutRowWidget::HandlePlus()
{
	const int32 Curr = GetLocalSeatCount();
	SendCountToServer(Curr + 1);
}

void UWeaponLoadoutRowWidget::HandleRosterChanged()
{
	RefreshFromState();
}

void UWeaponLoadoutRowWidget::RefreshFromState()
{
	if (WL_CountText)
		WL_CountText->SetText(FText::AsNumber(FMath::Max(0, GetLocalSeatCount())));
}

int32 UWeaponLoadoutRowWidget::GetLocalSeatCount() const
{
	if (const ASetupGameState* S = GS())
		if (const ASetupPlayerController* LPC = PC())
			return S->GetCountFor(UnitId, WeaponIndex, LPC->PlayerState == S->Player1);
	return 0;
}

void UWeaponLoadoutRowWidget::SendCountToServer(int32 NewCount) const
{
        if (ASetupPlayerController* LPC = PC())
                LPC->Server_SetUnitCount(UnitId, WeaponIndex, FMath::Clamp(NewCount, 0, 99));
}
