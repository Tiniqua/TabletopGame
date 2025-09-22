#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoverVolume.generated.h"

class UBoxComponent;
class UStaticMesh;
class UStaticMeshComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogCoverNet, Log, All);

UENUM(BlueprintType)
enum class ECoverType : uint8
{
	None  UMETA(DisplayName="None"),
	Low   UMETA(DisplayName="Low"),
	High  UMETA(DisplayName="High"),
};

UCLASS()
class TABLETOP_API ACoverVolume : public AActor
{
	GENERATED_BODY()

public:
	ACoverVolume();

	// Components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cover")
	UBoxComponent* Box = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cover")
	UStaticMeshComponent* Visual = nullptr;

	UPROPERTY(Transient)
	bool bPresetInitialized = false;
	
	// --- Config / State ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover|Preset")
	bool bPreferLowCover = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Destruction", meta=(ClampMin="0.01"))
	float MaxHealth = 10.f;

	UPROPERTY(ReplicatedUsing=OnRep_Health, BlueprintReadOnly, Category="Cover|Destruction")
	float Health = 10.f;

	UPROPERTY(ReplicatedUsing=OnRep_Preset, EditAnywhere, BlueprintReadWrite, Category="Cover|Preset")
	UStaticMesh* HighMesh = nullptr;

	UPROPERTY(ReplicatedUsing=OnRep_Preset, EditAnywhere, BlueprintReadWrite, Category="Cover|Preset")
	UStaticMesh* LowMesh  = nullptr;

	UPROPERTY(ReplicatedUsing=OnRep_Preset, EditAnywhere, BlueprintReadWrite, Category="Cover|Preset")
	UStaticMesh* NoneMesh = nullptr;

	/** threshold now REPLICATED so clients resolve High/Low the same way as server */
	UPROPERTY(ReplicatedUsing=OnRep_Threshold, EditAnywhere, BlueprintReadOnly, Category="Cover|Preset", meta=(ClampMin="0.0", ClampMax="1.0"))
	float HighToLowPct = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category="Cover|Collision")
	TEnumAsByte<ECollisionChannel> CoverTraceECC = ECC_GameTraceChannel4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover|Destruction")
	bool bDestroyOnZero = true;

	// API
	UFUNCTION(BlueprintCallable, Category="Cover|Destruction")
	void SetHealthPercentImmediate(float Pct);

	UFUNCTION(BlueprintCallable, Category="Cover|Preset")
	void ApplyPresetMeshes(UStaticMesh* InHighMesh, UStaticMesh* InLowMesh, UStaticMesh* InNoneMesh);

	UFUNCTION(BlueprintCallable, Category="Cover|Destruction")
	void ApplyCoverDamage(float Incoming);

	UPROPERTY(Transient)
	ECoverType LastAppliedType = ECoverType::None;

	// True only while we're recomputing as a result of ApplyCoverDamage().
	UPROPERTY(Transient)
	bool bRecomputeAfterDamage = false;

	UFUNCTION(BlueprintPure, Category="Cover") float GetLowStateHealthFraction() const;
	UFUNCTION(BlueprintPure, Category="Cover") bool  IsHighDefault() const;
	UFUNCTION(BlueprintPure, Category="Cover") ECoverType GetCurrentCoverType() const;
	UFUNCTION(BlueprintPure, Category="Cover") bool  BlocksCoverTrace() const;

	inline static const float kHealthEpsilonPct = 0.01f;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	ECoverType ComputeTypeFromHealth() const;
	void ApplyStateVisuals(ECoverType NewType);
	void RecomputeFromHealth();
	float ClampDamageToSingleStateDrop(float Incoming) const;
	bool HaveValidComponents() const;
	bool IsGameWorld() const;

	// Rep-notifies
	UFUNCTION() void OnRep_Health();
	UFUNCTION() void OnRep_Preset();
	/** NEW: ensure clients recompute when threshold changes */
	UFUNCTION() void OnRep_Threshold();
};