#pragma once

#include "CoreMinimal.h"
#include "Actors/CoverVolume.h"
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

namespace CombatMath
{
	inline float ProbAtLeast(int32 Need) {
		if (Need <= 1) return 1.f;
		if (Need >= 7) return 0.f;
		return float(7 - Need) / 6.f;
	}
	inline int32 ToWoundTarget(int32 S, int32 T) {
		if (S >= 2*T) return 2; if (S > T) return 3; if (S == T) return 4;
		if (2*S <= T) return 6; return 5;
	}
	inline int32 ModifiedSaveNeed(int32 BaseSave, int32 AP) {
		return FMath::Clamp(BaseSave + FMath::Max(0, AP), 2, 7);
	}
	inline const TCHAR* CoverTypeToText(ECoverType C) {
		switch (C) { case ECoverType::Low: return TEXT("in cover (low)");
		case ECoverType::High:return TEXT("in cover (high)");
		default:               return TEXT("no cover"); }
	}
}
