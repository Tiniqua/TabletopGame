#pragma once

#include "CoreMinimal.h"
#include "Tabletop/Actors/UnitAbility.h"
#include "TakeAimAbility.generated.h"

class AUnitBase;
struct FAbilityEventContext;

/** Grants the Take Aim action (+1 to hit for next shot). */
UCLASS()
class TABLETOP_API UTakeAimAbility : public UUnitAbility
{
	GENERATED_BODY()
public:
	UTakeAimAbility();

	virtual void Setup(AUnitBase* Owner) override;
	virtual void OnEvent(const FAbilityEventContext& Ctx) override;
};
