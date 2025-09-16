
#include "OverwatchAbility.h"

UOverwatchAbility::UOverwatchAbility()
{
	AbilityId=TEXT("Overwatch"); DisplayName=FText::FromString("Overwatch");
}

void UOverwatchAbility::Setup(AUnitBase* Owner)
{
	Super::Setup(Owner);
}

void UOverwatchAbility::OnEvent(const FAbilityEventContext& Ctx)
{
	Super::OnEvent(Ctx);
}
