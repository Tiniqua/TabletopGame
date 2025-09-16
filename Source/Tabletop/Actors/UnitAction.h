#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "UnitAction.generated.h"

class AMatchPlayerController;
UENUM(BlueprintType)
enum class ETurnPhase : uint8
{
	Move,
	Shoot
};

class AUnitBase;
struct FAbilityEventContext;

USTRUCT(BlueprintType)
struct FActionDescriptor
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ActionId = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Cost = 1;         // AP cost
	UPROPERTY(EditAnywhere, BlueprintReadOnly) ETurnPhase Phase = ETurnPhase::Move;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 UsesPerTurn = 0;  // 0 = unlimited
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 UsesPerPhase = 0; // 0 = unlimited

	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bRequiresGroundClick = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bRequiresEnemyTarget = false;
};

USTRUCT(BlueprintType)
struct FActionRuntimeArgs
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector TargetLocation = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) AUnitBase* TargetUnit = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) uint8 Aux = 0; // small extra param

	UPROPERTY(EditAnywhere, BlueprintReadWrite) AMatchPlayerController* InstigatorPC = nullptr;
};

UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced)
class TABLETOP_API UUnitAction : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Action") FActionDescriptor Desc;

	UFUNCTION(BlueprintNativeEvent) bool CanExecute(AUnitBase* Unit, const FActionRuntimeArgs& Args) const;
	virtual bool CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const;

	UFUNCTION(BlueprintNativeEvent) void Execute(AUnitBase* Unit, const FActionRuntimeArgs& Args);
	virtual void Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args);

	// Optional: allow actions to bind to global events (e.g., reset usage caps)
	virtual void Setup(AUnitBase* Unit) {}

protected:
	bool PayAP(AUnitBase* Unit) const;
};


// DEFAULT IMPLEMENTATIONS FOR BASIC ACTIONS - HERE BECAUSE NO POINT MAKING MORE FILES
// -----------------------------------------------------------------------------------
UCLASS()
class TABLETOP_API UAction_Move : public UUnitAction
{
	GENERATED_BODY()
public:
	UAction_Move()
	{
		Desc.ActionId = TEXT("Move");
		Desc.DisplayName = FText::FromString(TEXT("Move"));
		Desc.Cost = 2; Desc.Phase = ETurnPhase::Move;
		Desc.bRequiresGroundClick = true;
		//Desc.UsesPerTurn = 1; - Non functional yet
	}
	virtual bool CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const override;
	virtual void Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) override;
};

UCLASS()
class TABLETOP_API UAction_Advance : public UUnitAction
{
	GENERATED_BODY()
public:
	UAction_Advance()
	{
		Desc.ActionId = TEXT("Advance");
		Desc.DisplayName = FText::FromString(TEXT("Advance"));
		Desc.Cost = 1; Desc.Phase = ETurnPhase::Move;
	}
	virtual bool CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const override;
	virtual void Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) override;
};

UCLASS()
class TABLETOP_API UAction_Shoot : public UUnitAction
{
	GENERATED_BODY()
public:
	UAction_Shoot()
	{
		Desc.ActionId = TEXT("Shoot");
		Desc.DisplayName = FText::FromString(TEXT("Shoot"));
		Desc.Cost = 2; Desc.Phase = ETurnPhase::Shoot;
		Desc.bRequiresEnemyTarget = true;
	}
	virtual bool CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const override;
	virtual void Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) override;
};
