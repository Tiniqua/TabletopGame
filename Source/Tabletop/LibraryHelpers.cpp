#include "LibraryHelpers.h"

#include "EngineUtils.h"
#include "Actors/DeploymentZone.h"
#include "PlayerStates/TabletopPlayerState.h"

bool ULibraryHelpers::IsDeployLocationValid(UObject* WorldContextObject, APlayerController* PC, const FVector& WorldLocation)
{
	if (!WorldContextObject || !PC) return false;
	UWorld* World = WorldContextObject->GetWorld();
	if (!World) return false;

	const ATabletopPlayerState* TPS = PC->GetPlayerState<ATabletopPlayerState>();
	const int32 Team = TPS ? TPS->TeamNum : 0;
	if (Team <= 0) return false;

	return ADeploymentZone::IsLocationAllowedForTeam(World, Team, WorldLocation);
}

bool ULibraryHelpers::GetDeployHitAndValidity(
	UObject* WorldContextObject,
	APlayerController* PC,
	TEnumAsByte<ETraceTypeQuery> TraceType,
	FHitResult& OutHit,
	bool& bIsValid)
{
	bIsValid = false;
	if (!PC) return false;

	const bool bHit = PC->GetHitResultUnderCursorByChannel(TraceType, /*bTraceComplex*/ false, OutHit);
	if (!bHit) return false;

	bIsValid = IsDeployLocationValid(WorldContextObject, PC, OutHit.ImpactPoint);
	return true;
}