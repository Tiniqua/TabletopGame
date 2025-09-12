#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameFramework/PlayerState.h"
#include "NameUtils.generated.h"

UCLASS()
class TABLETOP_API UNameUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category="UI|Names")
	static FString GetShortPlayerName(const APlayerState* PS, int32 MaxChars = 16);

	static FString CleanClamp(const FString& In, int32 MaxChars);
	// If you keep custom DisplayName on your derived PS, we’ll prefer that.
	UFUNCTION(BlueprintPure, Category="UI|Names")
	static FString GetBestDisplayName(const APlayerState* PS); // unclamped, for debugging
};