#include "FieldMedicAbility.h"
#include "Tabletop/Actors/UnitAction.h"

UFieldMedicAbility::UFieldMedicAbility()
{
	AbilityId    = TEXT("FieldMedic");
	DisplayName  = NSLOCTEXT("Abilities", "FieldMedic", "Field Medic");
	GrantsAction = UAction_FieldMedic::StaticClass();
}

void UFieldMedicAbility::Setup(AUnitBase* Owner)
{
	Super::Setup(Owner);
}

void UFieldMedicAbility::OnEvent(const FAbilityEventContext& Ctx)
{
	Super::OnEvent(Ctx);
}
