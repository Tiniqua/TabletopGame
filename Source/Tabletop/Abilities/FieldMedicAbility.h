#pragma once

#include "CoreMinimal.h"
#include "Tabletop/Actors/UnitAbility.h"
#include "FieldMedicAbility.generated.h"

class AUnitBase;
struct FAbilityEventContext;

/** Grants the Field Medic action (heal closest ally within 12" for D6; 2/game, 1/turn; Move or Shoot). */
UCLASS()
class TABLETOP_API UFieldMedicAbility : public UUnitAbility
{
	GENERATED_BODY()
public:
	UFieldMedicAbility();

	virtual void Setup(AUnitBase* Owner) override;
	virtual void OnEvent(const FAbilityEventContext& Ctx) override;
};
