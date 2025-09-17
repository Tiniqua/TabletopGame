#pragma once

#include "CoreMinimal.h"
#include "Tabletop/Actors/UnitAbility.h"
#include "MedpackAbility.generated.h"

class AUnitBase;
struct FAbilityEventContext;

/** Grants the Medpack action (self-heal D3; 3/game, 1/turn; Move or Shoot). */
UCLASS()
class TABLETOP_API UMedpackAbility : public UUnitAbility
{
	GENERATED_BODY()
public:
	UMedpackAbility();

	virtual void Setup(AUnitBase* Owner) override;
	virtual void OnEvent(const FAbilityEventContext& Ctx) override;
};
