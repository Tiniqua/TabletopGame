#include "NameUtils.h"
#include "PlayerStates/TabletopPlayerState.h"

FString UNameUtils::CleanClamp(const FString& In, int32 MaxChars)
{
	FString S = In;
	S.TrimStartAndEndInline();
	S.ReplaceInline(TEXT("\r"), TEXT(" "));
	S.ReplaceInline(TEXT("\n"), TEXT(" "));
	S.ReplaceInline(TEXT("\t"), TEXT(" "));
	if (S.IsEmpty())
		S = TEXT("Player");
	if (MaxChars > 0 && S.Len() > MaxChars)
		S = S.Left(MaxChars);
	return S;
}

FString UNameUtils::GetBestDisplayName(const APlayerState* PS)
{
	if (!PS) return FString();
	if (const ATabletopPlayerState* TPS = Cast<ATabletopPlayerState>(PS))
	{
		if (!TPS->DisplayName.IsEmpty())
			return TPS->DisplayName;
		if (!TPS->ShortDisplayName.IsEmpty())
			return TPS->ShortDisplayName; // if you add it (see step 2)
	}
	return PS->GetPlayerName();
}

FString UNameUtils::GetShortPlayerName(const APlayerState* PS, int32 MaxChars)
{
	return CleanClamp(GetBestDisplayName(PS), MaxChars);
}
