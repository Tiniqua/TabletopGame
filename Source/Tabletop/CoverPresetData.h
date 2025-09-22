// CoverPresetData.h
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ArmyData.h"            // for EFaction
#include "CoverPresetData.generated.h"

USTRUCT(BlueprintType)
struct FCoverPresetRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EFaction     Faction = EFaction::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) UStaticMesh* HighCoverMesh = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) UStaticMesh* LowCoverMesh  = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) UStaticMesh* NoCoverMesh   = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float        HighToLowPct   = 0.65f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float        StartHealthPct = 1.0f;
};
