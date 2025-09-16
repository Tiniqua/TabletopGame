#pragma once
#include "CoreMinimal.h"
#include "Tabletop/Actors/UnitAbility.h"
#include "OverwatchAbility.generated.h"

class AUnitBase;
class UUnitAction;
struct FAbilityEventContext;

UCLASS()
class TABLETOP_API UOverwatchAbility : public UUnitAbility
{
    GENERATED_BODY()
public:
    UOverwatchAbility();
    
    virtual void Setup(AUnitBase* Owner) override;
    virtual void OnEvent(const FAbilityEventContext& Ctx) override;
};

