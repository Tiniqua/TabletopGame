#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DeploymentZone.generated.h"

class UBoxComponent;

UENUM(BlueprintType)
enum class EDeployOwner : uint8
{
    Team1,
    Team2,
    Either
};

UCLASS(Blueprintable)
class TABLETOP_API ADeploymentZone : public AActor
{
    GENERATED_BODY()

public:
    ADeploymentZone();

    // New: team-based query
    static bool IsLocationAllowedForTeam(const UWorld* World, int32 TeamNum, const FVector& WorldLocation);
    
    UPROPERTY(EditDefaultsOnly, Category="Visual") class UMaterialInterface* DecalMaterial = nullptr;
    UPROPERTY(EditDefaultsOnly, Category="Visual") FLinearColor Team1Color = FLinearColor(0.10f,0.55f,1.f,0.35f);
    UPROPERTY(EditDefaultsOnly, Category="Visual") FLinearColor Team2Color = FLinearColor(1.f,0.25f,0.25f,0.35f);
    UPROPERTY(EditDefaultsOnly, Category="Visual") FLinearColor EitherColor = FLinearColor(0.4f,0.4f,0.4f,0.25f);
    UPROPERTY(EditDefaultsOnly, Category="Visual") float LabelHeight = 120.f;
    
    UFUNCTION()
    void OnRep_Enabled()
    {
        RefreshVisuals();
    }
    
    UFUNCTION()
    void OnRep_CurrentOwner()
    {
        RefreshVisuals();
    }
    
    
    /** Axis-aligned box in this actor's local space; rotation/scaling supported via actor transform */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Deployment")
    UBoxComponent* Zone;

    /** Which player(s) may deploy here */
    UPROPERTY(ReplicatedUsing=OnRep_CurrentOwner, EditAnywhere, BlueprintReadWrite, Category="Deploy")
    EDeployOwner CurrentOwner = EDeployOwner::Either;

    /** Treat check as 2D (ignore Z) — usually desirable for top-down/tabletop */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Deployment")
    bool bUse2DCheck = true;

    /** Optional toggle if you want to disable a zone at runtime and replicate that */
    UPROPERTY(ReplicatedUsing=OnRep_Enabled, EditAnywhere, BlueprintReadWrite, Category="Deploy")
    bool bEnabled = true;

    /** Does this world-space location lie in the zone (respecting bUse2DCheck)? */
    UFUNCTION(BlueprintCallable, Category="Deployment")
    bool ContainsLocation(const FVector& WorldLocation) const;

    /** Convenience: get a random point inside (projected to box if bUse2DCheck) */
    UFUNCTION(BlueprintCallable, Category="Deployment")
    FVector GetRandomPointInside() const;

    /** Static helper: is location valid for a specific player's slot across any zones in the world? */
    static bool IsLocationAllowedForPlayer(const UWorld* World, int32 PlayerSlot /*1 or 2*/, const FVector& WorldLocation);

    /** Map your PlayerController to slot (1 or 2) however you track it */
    static int32 ResolvePlayerSlot(const APlayerController* PC);

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;

    // build/update visuals locally
    void RefreshVisuals();
    
    UFUNCTION()
    void OnMatchSignalChanged();
    
    // helpers to fetch nice player/army names
    FString OwnerDisplayText() const;
    FLinearColor OwnerColor() const;

    UFUNCTION() void UpdateOwnerText();
    UFUNCTION() void OnMatchChanged();

    UPROPERTY(VisibleAnywhere, Category="Deploy|Viz")
    class UTextRenderComponent* OwnerText = nullptr;

    UPROPERTY(EditAnywhere, Category="Deploy|Viz")
    bool bShowOwnerText = true;

    UPROPERTY(EditAnywhere, Category="Deploy|Viz", meta=(ClampMin="8.0"))
    float OwnerTextWorldSize = 48.f;

    UPROPERTY(EditAnywhere, Category="Deploy|Viz")
    float OwnerTextZOffset = 20.f; // extra lift above box center

private:
    // spawned client-side
    UPROPERTY(Transient) class UDecalComponent*   ZoneDecal   = nullptr;
    UPROPERTY(Transient) class UTextRenderComponent* Label3D  = nullptr;

    TWeakObjectPtr<class AMatchGameState> BoundGS;
};
