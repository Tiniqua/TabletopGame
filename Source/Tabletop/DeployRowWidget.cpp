
#include "DeployRowWidget.h"

#include "ArmyData.h"
#include "WeaponPickerWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "PlayerStates/TabletopPlayerState.h"


AMatchGameState* UDeployRowWidget::GS() const { return GetWorld()? GetWorld()->GetGameState<AMatchGameState>() : nullptr; }
AMatchPlayerController* UDeployRowWidget::MPC() const { return GetOwningPlayer<AMatchPlayerController>(); }

void UDeployRowWidget::InitDisplay(const FText& Name, UTexture2D* Icon, int32 Count)
{
	if (NameText)  NameText->SetText(Name);
	if (IconImg)   IconImg->SetBrushFromTexture(Icon, true);
	if (CountText) CountText->SetText(FText::AsNumber(Count));

	if (SelectBtn)
		SelectBtn->OnClicked.AddDynamic(this, &UDeployRowWidget::OnSelectClicked);
}

void UDeployRowWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

void UDeployRowWidget::OnSelectClicked()
{
	MPC()->BeginDeployForUnit(UnitId, WeaponIndex);
}