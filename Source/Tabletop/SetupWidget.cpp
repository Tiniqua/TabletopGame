
#include "SetupWidget.h"

#include "Components/WidgetSwitcher.h"
#include "Gamemodes/SetupGamemode.h"

void USetupWidget::HandlePhaseChanged()
{
	if (ASetupGameState* GS = GetWorld()->GetGameState<ASetupGameState>())
	{
		if (ContentSwitcher)
		{
			const int32 Index = static_cast<int32>(GS->Phase);
			if (Index >= 0 && Index < ContentSwitcher->GetNumWidgets())
			{
				ContentSwitcher->SetActiveWidgetIndex(Index);
			}
		}
	}
}
void USetupWidget::NativeConstruct()
{
	Super::NativeConstruct();
	
	if (ASetupGameState* GS = GetWorld()->GetGameState<ASetupGameState>())
	{
		GS->OnPhaseChanged.AddDynamic(this, &USetupWidget::HandlePhaseChanged);

		HandlePhaseChanged();
	}
}

void USetupWidget::NativeDestruct()
{
	if (ASetupGameState* GS = GetWorld()->GetGameState<ASetupGameState>())
	{
		GS->OnPhaseChanged.RemoveDynamic(this, &USetupWidget::HandlePhaseChanged);
	}
	Super::NativeDestruct();
}

void USetupWidget::GoToNextStep()
{
	if (ContentSwitcher)
	{
		int32 NewIndex = ContentSwitcher->GetActiveWidgetIndex() + 1;
		if (NewIndex < ContentSwitcher->GetNumWidgets())
		{
			ContentSwitcher->SetActiveWidgetIndex(NewIndex);
		}
	}
}

void USetupWidget::SetStepIndex(int32 Index)
{
	if (ContentSwitcher && Index >= 0 && Index < ContentSwitcher->GetNumWidgets())
	{
		ContentSwitcher->SetActiveWidgetIndex(Index);
	}
}