#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WorldspaceUnitStatusWidget.generated.h"

class AUnitBase;
class UTextBlock;
enum class ECoverType : uint8;

/** Lightweight widget that mirrors the selected/target unit's status above it in worldspace. */
UCLASS()
class TABLETOP_API UWorldspaceUnitStatusWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    /** Apply the latest state for the observed unit. */
    UFUNCTION(BlueprintCallable, Category="Status")
    void ApplyUnitStatus(AUnitBase* Unit, ECoverType CoverType, bool bHasCoverInfo);

protected:
    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* NameText = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* WoundsText = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* CoverText = nullptr;

    UPROPERTY(meta=(BindWidgetOptional))
    UTextBlock* ActiveActionsText = nullptr;

private:
    FString BuildActionsSummary(const AUnitBase* Unit) const;
};
