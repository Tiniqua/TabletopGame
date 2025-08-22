#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LibraryHelpers.generated.h"

class APlayerController;

UCLASS()
class TABLETOP_API ULibraryHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** True if WorldLocation is inside PC's allowed deployment zone(s). */
	UFUNCTION(BlueprintCallable, Category="Deployment", meta=(WorldContext="WorldContextObject"))
	static bool IsDeployLocationValid(UObject* WorldContextObject, APlayerController* PC, const FVector& WorldLocation);

	/**
	 * Do a cursor trace and also return validity.
	 * Returns true if we hit something; bIsValid says if that point is inside the zone.
	 */
	UFUNCTION(BlueprintCallable, Category="Deployment", meta=(WorldContext="WorldContextObject"))
	static bool GetDeployHitAndValidity(
		UObject* WorldContextObject,
		APlayerController* PC,
		TEnumAsByte<ETraceTypeQuery> TraceType,
		FHitResult& OutHit,
		bool& bIsValid);
};
