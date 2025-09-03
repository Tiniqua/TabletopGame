#include "KeywordChipWidget.h"
#include "Components/Border.h"

void UKeywordChipWidget::SetLabel(const FText& InText)
{
	if (Label) Label->SetText(InText);
}
void UKeywordChipWidget::SetTooltip(const FText& InText)
{
	// simplest: plain text tooltip
	SetToolTipText(InText);
	// (optional) SetToolTip() with a custom tooltip widget for richer layout
}
void UKeywordChipWidget::SetState(bool bActiveNow, bool bConditional)
{
	if (!Pill) return;
	// Example styling: green when active-now, amber when conditional
	const FLinearColor C = bActiveNow ? FLinearColor(0.12f,0.55f,0.20f,1.f)
									  : FLinearColor(0.80f,0.55f,0.00f,1.f);
	Pill->SetBrushColor(C);
}
void UKeywordChipWidget::SetIconForKeyword(EWeaponKeyword Keyword)
{
	if (!Icon) return;
	// Map keywords → brush here if you have icons (optional).
}