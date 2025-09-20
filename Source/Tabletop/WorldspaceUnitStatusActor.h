#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldspaceUnitStatusActor.generated.h"

class AUnitBase;
class AMatchPlayerController;
class UWidgetComponent;
class UWorldspaceUnitStatusWidget;
enum class ECoverType : uint8;

UCLASS()
class TABLETOP_API AWorldspaceUnitStatusActor : public AActor
{
    GENERATED_BODY()
public:
    AWorldspaceUnitStatusActor();

    void InitializeIndicator(AMatchPlayerController* InOwner, TSubclassOf<UWorldspaceUnitStatusWidget> WidgetClass);

    void SetObservedUnit(AUnitBase* Unit);
    void SetCoverInfo(ECoverType CoverType, bool bHasCoverInfo);

    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
    UPROPERTY(VisibleAnywhere, Category="Status")
    TObjectPtr<USceneComponent> Root = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Status")
    TObjectPtr<UWidgetComponent> WidgetComponent = nullptr;

    UPROPERTY(EditAnywhere, Category="Status")
    FVector WorldOffset = FVector(0.f, 0.f, 220.f);

private:
    UFUNCTION()
    void HandleObservedUnitStatusChanged();

    void RefreshWidget();
    void UpdateTransform() const;

    TWeakObjectPtr<AUnitBase> ObservedUnit;
    TWeakObjectPtr<AMatchPlayerController> OwnerPC;

    bool bCoverIsKnown = false;
    ECoverType CachedCover;
    bool bPendingRefresh = false;
};
