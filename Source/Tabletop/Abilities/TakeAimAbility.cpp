#include "TakeAimAbility.h"
#include "Tabletop/Actors/UnitAction.h"

UTakeAimAbility::UTakeAimAbility()
{
	AbilityId    = TEXT("TakeAim");
	DisplayName  = NSLOCTEXT("Abilities", "TakeAim", "Take Aim");
	GrantsAction = UAction_TakeAim::StaticClass();
}

void UTakeAimAbility::Setup(AUnitBase* Owner)
{
	Super::Setup(Owner);
}

void UTakeAimAbility::OnEvent(const FAbilityEventContext& Ctx)
{
	Super::OnEvent(Ctx);
}
