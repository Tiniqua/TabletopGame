

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoverVolume.generated.h"

class UBoxComponent;
UENUM(BlueprintType)
enum class ECoverType : uint8
{
	None  UMETA(DisplayName="None"),
	Low   UMETA(DisplayName="Low"),   // +1 to save
	High  UMETA(DisplayName="High"),  // +1 to save, -1 to hit
};


UCLASS()
class TABLETOP_API ACoverVolume : public AActor
{
	GENERATED_BODY()
public:
	ACoverVolume();

	// Collision box you scale in the map
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cover")
	UBoxComponent* Box = nullptr;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cover")
	UStaticMeshComponent* Visual = nullptr;

	// Set per-instance in the editor
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover")
	ECoverType CoverType = ECoverType::Low;
};
