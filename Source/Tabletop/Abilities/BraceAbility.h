#pragma once

#include "CoreMinimal.h"
#include "Tabletop/Actors/UnitAbility.h"
#include "BraceAbility.generated.h"

class AUnitBase;
struct FAbilityEventContext;

/** Grants the Brace action (invulnerable save easier until end of turn). */
UCLASS()
class TABLETOP_API UBraceAbility : public UUnitAbility
{
	GENERATED_BODY()
public:
	UBraceAbility();

	virtual void Setup(AUnitBase* Owner) override;
	virtual void OnEvent(const FAbilityEventContext& Ctx) override;
};
