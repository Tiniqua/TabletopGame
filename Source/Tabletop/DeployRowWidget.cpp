
#include "DeployRowWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"


AMatchGameState* UDeployRowWidget::GS() const { return GetWorld()? GetWorld()->GetGameState<AMatchGameState>() : nullptr; }
AMatchPlayerController* UDeployRowWidget::MPC() const { return GetOwningPlayer<AMatchPlayerController>(); }

void UDeployRowWidget::Init(FName InUnitId, const FText& InName, UTexture2D* InIcon, int32 InCount)
{
	UnitId = InUnitId;
	if (NameText)  NameText->SetText(InName);
	if (IconImg)   IconImg->SetBrushFromTexture(InIcon, true);
	if (CountText) CountText->SetText(FText::AsNumber(InCount));

	if (SelectBtn)
		SelectBtn->OnClicked.AddDynamic(this, &UDeployRowWidget::OnSelectClicked);
}

void UDeployRowWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

void UDeployRowWidget::OnSelectClicked()
{
	if (AMatchPlayerController* PC = GetOwningPlayer<AMatchPlayerController>())
	{
		PC->BeginDeployForUnit(UnitId);
	}
}