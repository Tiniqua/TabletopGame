#include "ActionButtonWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"

void UActionButtonWidget::Init(UUnitAction* InAction, FText InLabel, bool bEnabled)
{
	Action = InAction;
	if (LabelText) LabelText->SetText(InLabel);
	if (Button) Button->SetIsEnabled(bEnabled);
}

static void AB_SetBrush(UImage* Img, UTexture2D* Tex)
{
	if (!Img || !Tex) return;
	FSlateBrush B;
	B.SetResourceObject(Tex);
	B.ImageSize = FVector2D(10.f, 10.f);
	Img->SetBrush(B);
}

void UActionButtonWidget::SetCostPips(int32 Cost, UTexture2D* Active, UTexture2D* Inactive)
{
	if (!CostPipsBox) return;
	CostPipsBox->ClearChildren();
	const int32 C = FMath::Clamp(Cost, 0, 4);
	for (int32 i=0;i<C;++i)
	{
		UImage* Pip = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		if (!Pip) continue;
		AB_SetBrush(Pip, Active ? Active : Inactive);
		CostPipsBox->AddChild(Pip);
	}
}

void UActionButtonWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (Button) Button->OnClicked.AddDynamic(this, &UActionButtonWidget::HandleClicked);
}

void UActionButtonWidget::HandleClicked()
{
	OnActionClicked.Broadcast(Action);
}