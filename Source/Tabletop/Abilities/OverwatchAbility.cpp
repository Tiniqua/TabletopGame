
#include "OverwatchAbility.h"

#include "Tabletop/Actors/UnitAction.h"

UOverwatchAbility::UOverwatchAbility()
{
	AbilityId   = TEXT("Overwatch");
	DisplayName = NSLOCTEXT("Abilities", "Overwatch", "Overwatch");
	GrantsAction = UAction_Overwatch::StaticClass();
}

void UOverwatchAbility::Setup(AUnitBase* Owner)
{
	Super::Setup(Owner);
}

void UOverwatchAbility::OnEvent(const FAbilityEventContext& Ctx)
{
	Super::OnEvent(Ctx);
}
