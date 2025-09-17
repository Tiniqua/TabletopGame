#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/UnrealNetwork.h"
#include "UnitActionResourceComponent.generated.h"

UENUM(BlueprintType)
enum class EActionPoolScope : uint8 { PerPhase, PerTurn };

UCLASS(ClassGroup=(Tabletop), meta=(BlueprintSpawnableComponent))
class TABLETOP_API UUnitActionResourceComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category="AP")
	EActionPoolScope Scope = EActionPoolScope::PerPhase;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AP")
	int32 DefaultMaxAP = 2;

	UPROPERTY(ReplicatedUsing=OnRep_AP, BlueprintReadOnly, Category="AP")
	int32 CurrentAP = 0;
	
	UPROPERTY(Replicated, BlueprintReadOnly, Category="AP")
	int32 MaxAP = 4;

	UUnitActionResourceComponent();
	
	UFUNCTION()
	void OnRep_AP();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	

	UFUNCTION(BlueprintCallable) void ResetForPhase();
	
	UFUNCTION(BlueprintCallable) void ResetForTurn();
	
	UFUNCTION(BlueprintCallable) bool CanPay(int32 Cost) const;
	UFUNCTION(BlueprintCallable) bool Pay(int32 Cost);
	
	UFUNCTION(BlueprintCallable) void Refund(int32 Amount);
	UFUNCTION(BlueprintCallable) void Grant(int32 Amount, int32 NewCap = -1);
	
};
