

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ObjectiveMarker.generated.h"

class USphereComponent;
USTRUCT(BlueprintType)
struct FObjectiveContestant
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	APlayerState* PlayerState = nullptr;

	UPROPERTY(BlueprintReadOnly)
	int32 ObjectiveControl = 0;
};

UCLASS()
class AObjectiveMarker : public AActor
{
	GENERATED_BODY()
public:
	AObjectiveMarker();

	// ---- Design data ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Objective")
	FName ObjectiveId = "OBJ_A";

	// In centimeters (UE units). 6" ~ 152.4 cm.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Objective", meta=(ClampMin="1.0"))
	float RadiusCm = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Objective")
	int32 PointsPerRound = 5;

	UPROPERTY(EditAnywhere, Category="Objective|Debug")
	bool bDrawDebug = true;


	/** Null if no controller or contested tie for max OC. */
	UPROPERTY(ReplicatedUsing=OnRep_ControlPS)
	APlayerState* ControllingPS = nullptr;
	
	// ---- Queries / API ----

	/** Server: recompute contestants and controlling PS from current unit positions. */
	UFUNCTION(BlueprintCallable, Category="Objective")
	void RecalculateControl();

	/** Is world location inside the scoring radius (cheap distance check). */
	UFUNCTION(BlueprintPure, Category="Objective")
	bool IsInside(const FVector& WorldLoc) const
	{
		return FVector::DistSquared(WorldLoc, GetActorLocation()) <= RadiusSq;
	}

	/** Replicated winner (nullptr if contested or empty). */
	UFUNCTION(BlueprintPure, Category="Objective")
	APlayerState* GetControllingPlayerState() const { return ControllingPS; }

	/** Replicated per-PS OC snapshot for UI (server fills, clients read). */
	UFUNCTION(BlueprintPure, Category="Objective")
	const TArray<FObjectiveContestant>& GetContestants() const { return Contestants; }

protected:
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& E) override;
#endif

	// Replication
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// Visual only; no overlaps/ticks
	UPROPERTY()
	USphereComponent* Sphere = nullptr;

	float RadiusSq = 0.f;
	void RecalcRadius(); // update Sphere radius & RadiusSq

	// ---- Replicated control state ----

	/** Sorted descending by OC. Server writes, clients read. */
	UPROPERTY(ReplicatedUsing=OnRep_Contestants)
	TArray<FObjectiveContestant> Contestants;

	

	UFUNCTION()
	void OnRep_Contestants() {}

	UFUNCTION()
	void OnRep_ControlPS() {}
};