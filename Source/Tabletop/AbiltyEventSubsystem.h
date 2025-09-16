
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CombatEffects.h"
#include "AbiltyEventSubsystem.generated.h"


class AUnitBase;
struct FGameplayTagContainer;

USTRUCT(BlueprintType)
struct FAbilityEventContext
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) ECombatEvent Event = ECombatEvent::Game_Begin;
	UPROPERTY(BlueprintReadOnly) class AMatchGameMode* GM = nullptr;
	UPROPERTY(BlueprintReadOnly) class AMatchGameState* GS = nullptr;
	UPROPERTY(BlueprintReadOnly) AUnitBase* Source = nullptr;
	UPROPERTY(BlueprintReadOnly) AUnitBase* Target = nullptr;
	UPROPERTY(BlueprintReadOnly) FVector     WorldPos = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) float       Radius   = 0.f;
	UPROPERTY(BlueprintReadOnly) FGameplayTagContainer Tags;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnEventSimple, const FAbilityEventContext&);            // just context
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnEventUnit, const FAbilityEventContext&, AUnitBase*); // source focus
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnEventUnitVsUnit, const FAbilityEventContext&, AUnitBase*, AUnitBase*);

UCLASS()
class TABLETOP_API UAbilityEventSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	FOnEventSimple         OnAny;         // catch-all
	FOnEventUnit           OnUnit;
	FOnEventUnitVsUnit     OnUnitVsUnit;

	UFUNCTION(BlueprintCallable)
	void Broadcast(const FAbilityEventContext& Ctx);
};
