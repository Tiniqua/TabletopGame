// CoverPresetData.h
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ArmyData.h"            // for EFaction
#include "CoverPresetData.generated.h"

USTRUCT()
struct FCoverPresetRep
{
	GENERATED_BODY()

	UPROPERTY() UStaticMesh* High = nullptr;
	UPROPERTY() UStaticMesh* Low  = nullptr;
	UPROPERTY() UStaticMesh* None = nullptr;

	// Threshold + starting health percent are part of the preset decision:
	UPROPERTY() float HighToLowPct   = 0.65f;
	UPROPERTY() float StartHealthPct = 1.0f;

	// Optional: if you still want “prefer low” knobs,
	// keep them inside the struct so they are applied once.
	UPROPERTY() bool bPreferLowCover = false;

	// Simple validity check
	bool IsValid() const { return High || Low || None; }
};