#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UnitAbility.generated.h"

class AUnitBase;
class UUnitAction;
struct FAbilityEventContext;

UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced)
class TABLETOP_API UUnitAbility : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ability")
	FName AbilityId = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ability")
	FText DisplayName;

	// If this ability wants to add an active action to the unit, provide a class here
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ability")
	TSubclassOf<UUnitAction> GrantsAction;

	// called once after spawn (subscribe to events, grant actions, etc.)
	virtual void Setup(AUnitBase* Owner);

	// optional: handle event bus callbacks
	virtual void OnEvent(const FAbilityEventContext& Ctx) {}

protected:
	UPROPERTY()
	AUnitBase* OwnerUnit;
};
