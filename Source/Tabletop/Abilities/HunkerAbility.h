#pragma once
#include "Tabletop/Actors/UnitAbility.h"
#include "Tabletop/Actors/UnitAction.h"
#include "HunkerAbility.generated.h"

UCLASS()
class TABLETOP_API UHunkerAbility : public UUnitAbility
{
	GENERATED_BODY()
public:
	UHunkerAbility()
	{
		AbilityId   = TEXT("Hunker");
		DisplayName = NSLOCTEXT("Abilities", "Hunker", "Hunker Down");
		GrantsAction = UAction_Hunker::StaticClass();
	}
};