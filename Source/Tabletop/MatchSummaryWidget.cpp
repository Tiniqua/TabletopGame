// MatchSummaryWidget.cpp
#include "MatchSummaryWidget.h"

#include "OnlineSubsystem.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Gamemodes/MatchGameMode.h"
#include "Interfaces/OnlineSessionDelegates.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Kismet/GameplayStatics.h"

void UMatchSummaryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	
	if (BtnReturnToMenu && !BtnReturnToMenu->OnClicked.IsBound())
	{
		BtnReturnToMenu->OnClicked.AddDynamic(this, &UMatchSummaryWidget::OnReturnToMenuClicked);
	}
}

void UMatchSummaryWidget::OnReturnToMenuClicked()
{
	if (BtnReturnToMenu) { BtnReturnToMenu->SetIsEnabled(false); }
	LeaveOrDestroySessionThenReturn();
}

void UMatchSummaryWidget::LeaveOrDestroySessionThenReturn()
{
	IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
	if (OSS)
	{
		IOnlineSessionPtr Sessions = OSS->GetSessionInterface();
		if (Sessions.IsValid())
		{
			// Destroy current session (works for both host & clients)
			FOnDestroySessionCompleteDelegate Delegate =
				FOnDestroySessionCompleteDelegate::CreateUObject(this, &UMatchSummaryWidget::OnDestroySessionComplete);
			Sessions->AddOnDestroySessionCompleteDelegate_Handle(Delegate);

			if (Sessions->GetNamedSession(NAME_GameSession))
			{
				Sessions->DestroySession(NAME_GameSession);
				return;
			}
		}
	}

	// Fallback (no OSS / no session)
	ReturnToMainMenu();
}

FString UMatchSummaryWidget::ResolveReturnMapPath() const
{
	// 1) If a specific Level was provided (PIE convenience), use its owning world's package
	if (MainMenuLevelOverride)
	{
		if (const UWorld* OwningWorld = MainMenuLevelOverride->GetTypedOuter<UWorld>())
		{
			if (const UPackage* Pkg = OwningWorld->GetOutermost())
			{
				return Pkg->GetName(); // e.g. "/Game/Maps/MainMenu"
			}
		}
	}

	// 2) If an asset soft reference is set, use its long package name
	if (!MainMenuMapAsset.IsNull())
	{
		return MainMenuMapAsset.ToSoftObjectPath().GetLongPackageName();
	}

	// 3) Fallback to string path (if provided)
	if (!MainMenuMapPath.IsEmpty())
	{
		return MainMenuMapPath;
	}

	// 4) Hard default
	return TEXT("/Game/Maps/MainMenu");
}

void UMatchSummaryWidget::ReturnToMainMenu()
{
	UWorld* World = GetWorld();
	if (!World) return;

	const FString MapPath = ResolveReturnMapPath();
	if (MapPath.IsEmpty())
	{
		// As a last resort, just remove the widget so user isn't stuck
		RemoveFromParent();
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	const bool bIsServer  = PC ? PC->HasAuthority() : (World->GetNetMode() != NM_Client);

	if (bIsServer)
	{
		UGameplayStatics::OpenLevel(World, FName(*MapPath), true);
	}
	else if (PC)
	{
		PC->ClientTravel(MapPath, TRAVEL_Absolute);
	}

	RemoveFromParent();
}

void UMatchSummaryWidget::OnDestroySessionComplete(FName /*SessionName*/, bool /*bWasSuccessful*/)
{
	ReturnToMainMenu();
}

static FString PadInt(int32 V, int32 Width)
{
	FString S = FString::FromInt(V);
	while (S.Len() < Width) S = TEXT(" ") + S;
	return S;
}

FString UMatchSummaryWidget::MakeSurvivorLines(const FMatchSummary& S, int32 TeamNum)
{
	TArray<FString> Lines;
	Lines.Reserve(S.Survivors.Num());

	// Optional: header
	Lines.Add(TEXT("Unit                               (Models)"));

	for (const FSurvivorEntry& E : S.Survivors)
	{
		if (E.TeamNum != TeamNum) continue;

		const FString Name = E.UnitName.ToString();
		// Basic alignment: name padded to ~32 chars (rough)
		FString NamePad = Name;
		if (NamePad.Len() < 32) NamePad += FString::ChrN(32 - NamePad.Len(), ' ');

		const FString Count = FString::Printf(TEXT("(%s/%s)"),
			*PadInt(E.ModelsCurrent, 2),
			*PadInt(E.ModelsMax, 2));

		Lines.Add(NamePad + TEXT("  ") + Count);
	}

	if (Lines.Num() == 1) // only header present → no survivors
	{
		Lines.Add(TEXT("— none —"));
	}

	return FString::Join(Lines, TEXT("\n"));
}

void UMatchSummaryWidget::RefreshFromState(AMatchGameState* GS)
{
	if (!GS) return;

	const FMatchSummary& S = GS->FinalSummary;

	if (P1ScoreText) P1ScoreText->SetText(FText::AsNumber(S.ScoreP1));
	if (P2ScoreText) P2ScoreText->SetText(FText::AsNumber(S.ScoreP2));
	if (RoundsText)
	{
		RoundsText->SetText(FText::FromString(
			FString::Printf(TEXT("Rounds: %d / %d"), (int)S.RoundsPlayed, (int)GS->MaxRounds)));
	}

	if (P1SurvivorsText)
	{
		P1SurvivorsText->SetText(FText::FromString(MakeSurvivorLines(S, 1)));
	}
	if (P2SurvivorsText)
	{
		P2SurvivorsText->SetText(FText::FromString(MakeSurvivorLines(S, 2)));
	}
}
