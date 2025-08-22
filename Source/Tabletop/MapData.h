// MapData.h

#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MapData.generated.h"

USTRUCT(BlueprintType)
struct FMapRow : public FTableRowBase
{
	GENERATED_BODY()

	// Shown in dropdown
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText DisplayName;

	// Level to travel to (e.g. /Game/Maps/TT_Map01 or simple level name if using ServerTravel with short name)
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName LevelName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UWorld> Level;

	// Preview icon (UMG Image)
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UTexture2D* Preview = nullptr;
};
