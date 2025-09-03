#include "KeywordChipWidget.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"

void UKeywordChipWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (!ensureMsgf(ChipBorder, TEXT("KeywordChipWidget: ChipBorder not bound. Name must match in the BP."))) {}
	if (!ensureMsgf(LabelText,  TEXT("KeywordChipWidget: LabelText  not bound. Name must match in the BP."))) {}

	
	// In header defaults or in a C++ constructor of the widget
	ActiveBg        = FLinearColor(0.12f,0.62f,0.30f,1.f);
	ActiveText      = FLinearColor::White;
	ActiveOpacity   = 1.0f;

	ConditionalBg   = FLinearColor(0.18f,0.45f,0.72f,1.f);
	ConditionalText = FLinearColor::White;
	ConditionalOpacity = 0.85f;

	InactiveBg      = FLinearColor(0.20f,0.20f,0.20f,1.f);
	InactiveText    = FLinearColor(0.75f,0.75f,0.75f,1.f);
	InactiveOpacity = 0.4f;
	
	ApplyVisuals();
}

void UKeywordChipWidget::InitFromInfo(const FKeywordUIInfo& Info)
{
	SetLabel(Info.Label);
	SetTooltip(Info.Tooltip);
	SetState(Info.State);
}

void UKeywordChipWidget::SetLabel(const FText& InText)
{
	if (LabelText)
	{
		LabelText->SetText(InText);
		// Ensure it’s visible even if the BP style had a transparent color
		LabelText->SetColorAndOpacity(FSlateColor(FLinearColor(1,1,1,1)));
	}
	else
	{
		SetToolTipText(InText); // fallback
	}
}

void UKeywordChipWidget::SetTooltip(const FText& InText)
{
	SetToolTipText(InText);
}

void UKeywordChipWidget::SetState(EKeywordUIState InState)
{
	State = InState;
	ApplyVisuals();
}

void UKeywordChipWidget::ApplyVisuals()
{
	FLinearColor Bg, Txt;
	float Opacity = 1.0f;

	switch (State)
	{
	case EKeywordUIState::ActiveNow:
		Bg = ActiveBg;       Txt = ActiveText;       Opacity = ActiveOpacity;      break;
	case EKeywordUIState::Conditional:
		Bg = ConditionalBg;  Txt = ConditionalText;  Opacity = ConditionalOpacity; break;
	case EKeywordUIState::Inactive:
	default:
		Bg = InactiveBg;     Txt = InactiveText;     Opacity = InactiveOpacity;    break;
	}

	if (ChipBorder)        ChipBorder->SetBrushColor(Bg);
	if (LabelText)         LabelText->SetColorAndOpacity(Txt);
	SetRenderOpacity(Opacity);

	// Leave enabled so tooltips still show for inactive/conditional chips
	SetIsEnabled(true);
}
