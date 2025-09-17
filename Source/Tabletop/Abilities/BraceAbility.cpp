#include "BraceAbility.h"
#include "Tabletop/Actors/UnitAction.h"

UBraceAbility::UBraceAbility()
{
	AbilityId    = TEXT("Brace");
	DisplayName  = NSLOCTEXT("Abilities", "Brace", "Brace");
	GrantsAction = UAction_Brace::StaticClass();
}

void UBraceAbility::Setup(AUnitBase* Owner)
{
	Super::Setup(Owner);
}

void UBraceAbility::OnEvent(const FAbilityEventContext& Ctx)
{
	Super::OnEvent(Ctx);
}
