#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UnitAction.generated.h"

// Forward declarations only (avoid heavy includes here)
class AUnitBase;
class AMatchPlayerController;
class AMatchGameMode;
class AMatchGameState;
class UUnitActionResourceComponent;
class UAbilityEventSubsystem;
struct FAbilityEventContext;



UENUM(BlueprintType)
enum class ETurnPhase : uint8
{
	Move,
	Shoot
};

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
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 UsesPerMatch = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bRequiresGroundClick = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bRequiresEnemyTarget = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)bool bRequiresFriendlyTarget = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)bool bAllowSelfTarget = false; 

	// If this action applies a debt to the next phase's AP
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 NextPhaseAPCost = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) bool bPassive = false;                // no button, no AP
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) bool bShowInPassiveList = true;      // for UI chips
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(MultiLine="true"))
	FText Tooltip;
};

USTRUCT(BlueprintType)
struct FActionRuntimeArgs
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector     TargetLocation = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) AUnitBase* TargetUnit     = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) uint8      Aux            = 0; // small extra param

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

	// Optional live preview hooks for UI highlight, ghost placement, etc.
	UFUNCTION(BlueprintNativeEvent) void BeginPreview(AUnitBase* Unit);
	virtual void BeginPreview_Implementation(AUnitBase* Unit);

	UFUNCTION(BlueprintNativeEvent) void EndPreview(AUnitBase* Unit);
	virtual void EndPreview_Implementation(AUnitBase* Unit);

	// Optional: allow actions to bind to global events (e.g., reset usage caps)
	virtual void Setup(AUnitBase* Unit);

	virtual bool LeavesLingeringState() const { return false; }

	UFUNCTION(BlueprintNativeEvent, BlueprintPure)
	bool IsPassive() const;      // default false
	virtual bool IsPassive_Implementation() const { return Desc.bPassive; }

	UFUNCTION(BlueprintNativeEvent, BlueprintPure)
	FText GetTooltipText() const; // default to Desc.Tooltip
	virtual FText GetTooltipText_Implementation() const { return Desc.Tooltip; }

	UPROPERTY()
	AUnitBase* OwnerUnit = nullptr;

protected:
	bool PayAP(AUnitBase* Unit) const;
};


// =================== DEFAULT / BUILT-IN ACTIONS ===================

UCLASS()
class TABLETOP_API UAction_Move : public UUnitAction
{
	GENERATED_BODY()
public:
	UAction_Move();

	virtual bool CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const override;
	virtual void Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) override;
};

UCLASS()
class TABLETOP_API UAction_Advance : public UUnitAction
{
	GENERATED_BODY()
public:
	UAction_Advance();

	virtual bool CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const override;
	virtual void Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) override;
};

UCLASS()
class TABLETOP_API UAction_Shoot : public UUnitAction
{
	GENERATED_BODY()
public:
	UAction_Shoot();

	virtual bool CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const override;
	virtual void Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) override;
};

UCLASS()
class TABLETOP_API UAction_Overwatch : public UUnitAction
{
	GENERATED_BODY()
public:
	UAction_Overwatch();

	virtual void Setup(AUnitBase* Unit) override;
	virtual bool CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const override;
	virtual void Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) override;

	bool LeavesLingeringState() const override { return true; }

private:
	UFUNCTION() void OnAnyEvent(const struct FAbilityEventContext& Ctx);
};

// +1 to hit for next shot
UCLASS()
class TABLETOP_API UAction_TakeAim : public UUnitAction
{
	GENERATED_BODY()
public:
	UAction_TakeAim();

	virtual void Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) override;
};

// FNP easier until end of turn (defense buff)
UCLASS()
class TABLETOP_API UAction_Hunker : public UUnitAction
{
	GENERATED_BODY()
public:
	UAction_Hunker();

	virtual bool CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const override;
	virtual void Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) override;
};

// Invulnerable save easier until end of turn
UCLASS()
class TABLETOP_API UAction_Brace : public UUnitAction
{
	GENERATED_BODY()
public:
	UAction_Brace();

	virtual void Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) override;
};

// Self-heal D3 (3 uses per match, 1/turn). Usable in Move or Shoot.
UCLASS()
class TABLETOP_API UAction_Medpack : public UUnitAction
{
	GENERATED_BODY()
public:
	UAction_Medpack();

	virtual bool CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const override;
	virtual void Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) override;
};

// Radial heal (target closest friendly within 12") for D6. 2 uses/match, 1/turn. Usable in Move or Shoot.
// Also previews the target via BeginPreview/EndPreview.
UCLASS()
class TABLETOP_API UAction_FieldMedic : public UUnitAction
{
	GENERATED_BODY()
public:
	UAction_FieldMedic();

	virtual bool CanExecute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) const override;
	virtual void Execute_Implementation(AUnitBase* Unit, const FActionRuntimeArgs& Args) override;

	virtual void BeginPreview_Implementation(AUnitBase* Unit) override;
	virtual void EndPreview_Implementation(AUnitBase* Unit) override;

private:
	TWeakObjectPtr<AUnitBase> CachedPreviewTarget;

	AUnitBase* FindClosestAllyWithin12(AUnitBase* U) const;
};
