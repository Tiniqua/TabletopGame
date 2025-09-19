// UnitRowWidget.cpp
#include "UnitRowWidget.h"

#include "UnitSelectionWidget.h"
#include "WeaponLoadoutRowWidget.h"
#include "WeaponDisplayText.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Controllers/SetupPlayerController.h"
#include "Gamemodes/SetupGamemode.h"


ASetupGameState* UUnitRowWidget::GS() const { return GetWorld()? GetWorld()->GetGameState<ASetupGameState>() : nullptr; }

bool UUnitRowWidget::IsLocalP1() const
{
	if (const ASetupGameState* S = GS())
		if (const APlayerController* PC = GetOwningPlayer())
			return PC->PlayerState == S->Player1;
	return true;
}

void UUnitRowWidget::InitFull(FName InRowName, const FUnitRow& Row)
{
	CachedRow   = Row;
	UnitId      = InRowName; 
	UnitPoints  = Row.Points;

	if (NameText)          NameText->SetText(Row.DisplayName);
	if (PointsText)        PointsText->SetText(FText::AsNumber(UnitPoints));
	if (UnitStatsText)     UnitStatsText->SetText(FText::FromString(FormatUnitStats(Row)));
        if (UnitAbilitiesText) UnitAbilitiesText->SetText(FText::FromString(WeaponDisplayText::FormatAbilityList(Row.AbilityClasses)));

	RebuildLoadouts();
	RefreshHeader();
}

void UUnitRowWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ASetupGameState* S = GS())
	{
		S->OnRosterChanged.AddDynamic(this, &UUnitRowWidget::HandleRosterChanged);
	}
	UE_LOG(LogTemp, Warning, TEXT("[UnitRow] Construct %p  UnitId=%s  USel=%s"),
			this, *UnitId.ToString(),
			*GetNameSafe(GetTypedOuter<UUnitSelectionWidget>()));
	
	RefreshHeader();
}

void UUnitRowWidget::NativeDestruct()
{
	if (ASetupGameState* S = GS())
	{
		S->OnRosterChanged.RemoveDynamic(this, &UUnitRowWidget::HandleRosterChanged);
	}
	
	Super::NativeDestruct();
}

void UUnitRowWidget::HandleRosterChanged()
{
	RefreshHeader();

	if (LoadoutsList)
	{
		for (int32 i=0; i<LoadoutsList->GetChildrenCount(); ++i)
			if (auto* C = Cast<UWeaponLoadoutRowWidget>(LoadoutsList->GetChildAt(i)))
				C->RefreshFromState();
	}
}

void UUnitRowWidget::RefreshHeader()
{
	if (!CountText) return;
	if (const ASetupGameState* S = GS())
		CountText->SetText(FText::AsNumber(S->GetTotalCountFor(UnitId, IsLocalP1())));
}

void UUnitRowWidget::RebuildLoadouts()
{
	if (!LoadoutsList || !LoadoutRowClass) return;
	LoadoutsList->ClearChildren();

	for (int32 i=0; i<CachedRow.Weapons.Num(); ++i)
	{
		const FWeaponProfile& W = CachedRow.Weapons[i];
		if (auto* Row = CreateWidget<UWeaponLoadoutRowWidget>(GetOwningPlayer(), LoadoutRowClass))
		{
			Row->Init(UnitId, i, W);
			LoadoutsList->AddChild(Row);
		}
	}
}

FString UUnitRowWidget::FormatUnitStats(const FUnitRow& U)
{
	const FString Inv = (U.InvulnSave >= 2 && U.InvulnSave <= 6) ? FString::Printf(TEXT("Inv %d++"), U.InvulnSave) : TEXT("");
	const FString FNP = (U.FeelNoPain >= 2 && U.FeelNoPain <= 6) ? FString::Printf(TEXT("FNP %d++"), U.FeelNoPain) : TEXT("");
	FString Paren;
	if (!Inv.IsEmpty() || !FNP.IsEmpty())
		Paren = FString::Printf(TEXT("  (%s%s%s)"), *Inv, (!Inv.IsEmpty() && !FNP.IsEmpty())?TEXT("  "):TEXT(""), *FNP);

	return FString::Printf(TEXT("M %d\"  T %d  W %d  Sv %d+  OC %d%s"),
		U.MoveInches, U.Toughness, U.Wounds, U.Save, U.ObjectiveControlPerModel, *Paren);
}

