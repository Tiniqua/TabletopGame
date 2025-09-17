#include "MedpackAbility.h"
#include "Tabletop/Actors/UnitAction.h"

UMedpackAbility::UMedpackAbility()
{
	AbilityId    = TEXT("Medpack");
	DisplayName  = NSLOCTEXT("Abilities", "Medpack", "Medpack");
	GrantsAction = UAction_Medpack::StaticClass();
}

void UMedpackAbility::Setup(AUnitBase* Owner)
{
	Super::Setup(Owner);
}

void UMedpackAbility::OnEvent(const FAbilityEventContext& Ctx)
{
	Super::OnEvent(Ctx);
}
