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


    /** Axis-aligned box in this actor's local space; rotation/scaling supported via actor transform */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Deployment")
    UBoxComponent* Zone;

    /** Which player(s) may deploy here */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deployment")
    EDeployOwner CurrentOwner = EDeployOwner::Team1;

    /** Treat check as 2D (ignore Z) — usually desirable for top-down/tabletop */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Deployment")
    bool bUse2DCheck = true;

    /** Optional toggle if you want to disable a zone at runtime and replicate that */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category="Deployment")
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
};
