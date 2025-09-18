// UnitSelectionWidget.cpp

#include "UnitSelectionWidget.h"
#include "NameUtils.h"
#include "UnitRowWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Controllers/SetupPlayerController.h"
#include "Gamemodes/SetupGamemode.h"

ASetupGameState* UUnitSelectionWidget::GS() const { return GetWorld()? GetWorld()->GetGameState<ASetupGameState>() : nullptr; }
ASetupPlayerController* UUnitSelectionWidget::PC() const { return GetOwningPlayer<ASetupPlayerController>(); }

void UUnitSelectionWidget::NativeConstruct()
{
	Super::NativeConstruct();
	
	UE_LOG(LogTemp, Warning, TEXT("[USel] Construct %p Owner=%s Parent=%s InViewport=%d"),
		this, *GetNameSafe(GetOwningPlayer()), *GetNameSafe(GetParent()), IsInViewport());

	if (ASetupGameState* S = GS())
	{
		S->OnPhaseChanged       .AddDynamic(this, &UUnitSelectionWidget::OnPhaseChanged);
		S->OnPlayerReadyUp      .AddDynamic(this, &UUnitSelectionWidget::OnReadyUpChanged);
		S->OnRosterChanged      .AddDynamic(this, &UUnitSelectionWidget::OnRosterChanged);
		S->OnArmySelectionChanged.AddDynamic(this, &UUnitSelectionWidget::OnArmySelectionChanged);
	}

	if (P1ReadyBtn) P1ReadyBtn->OnClicked.AddDynamic(this, &UUnitSelectionWidget::OnP1ReadyClicked);
	if (P2ReadyBtn) P2ReadyBtn->OnClicked.AddDynamic(this, &UUnitSelectionWidget::OnP2ReadyClicked);
	if (BothReady)  BothReady ->OnClicked.AddDynamic(this, &UUnitSelectionWidget::OnBothReadyClicked);

	RefreshFromState();
	MaybeBuildRows();
}

void UUnitSelectionWidget::NativeDestruct()
{
	UE_LOG(LogTemp, Warning, TEXT("[USel] Destruct %p"), this);

	if (ASetupGameState* S = GS())
	{
		S->OnPhaseChanged        .RemoveDynamic(this, &UUnitSelectionWidget::OnPhaseChanged);
		S->OnPlayerReadyUp       .RemoveDynamic(this, &UUnitSelectionWidget::OnReadyUpChanged);
		S->OnRosterChanged       .RemoveDynamic(this, &UUnitSelectionWidget::OnRosterChanged);
		S->OnArmySelectionChanged.RemoveDynamic(this, &UUnitSelectionWidget::OnArmySelectionChanged); // fixed target
	}
	Super::NativeDestruct();
}

void UUnitSelectionWidget::OnPhaseChanged()      { MaybeBuildRows(); RefreshFromState(); }
void UUnitSelectionWidget::OnArmySelectionChanged(){ MaybeBuildRows(); RefreshFromState(); }
void UUnitSelectionWidget::OnReadyUpChanged()    { RefreshFromState(); }
void UUnitSelectionWidget::OnRosterChanged()     { RefreshFromState(); }

void UUnitSelectionWidget::RefreshFromState()
{
	ASetupGameState* S = GS();
	ASetupPlayerController* LPC = PC();
	if (!S || !LPC) return;

	if (P1Name)      P1Name     ->SetText(FText::FromString(UNameUtils::GetShortPlayerName(S->Player1)));
	if (P2Name)      P2Name     ->SetText(FText::FromString(UNameUtils::GetShortPlayerName(S->Player2)));
	if (P1PointsText)P1PointsText->SetText(FText::AsNumber(S->P1Points));
	if (P2PointsText)P2PointsText->SetText(FText::AsNumber(S->P2Points));

	const bool bLocalIsP1 = (LPC->PlayerState && LPC->PlayerState == S->Player1);
	const bool bLocalIsP2 = (LPC->PlayerState && LPC->PlayerState == S->Player2);

	const int32 Cap = 2000;
	if (P1ReadyBtn) P1ReadyBtn->SetIsEnabled(bLocalIsP1 && S->P1Points <= Cap);
	if (P2ReadyBtn) P2ReadyBtn->SetIsEnabled(bLocalIsP2 && S->P2Points <= Cap);

	if (BothReady)
	{
		const bool bHostSeat  = bLocalIsP1;
		const bool bBothReady = S->bP1Ready && S->bP2Ready;
		const bool bUnderCap  = (S->P1Points <= Cap) && (S->P2Points <= Cap);
		BothReady->SetIsEnabled(bHostSeat && bBothReady && bUnderCap && S->Phase == ESetupPhase::UnitSelection);
	}
}

void UUnitSelectionWidget::MaybeBuildRows()
{
	ASetupGameState* S = GS();
	ASetupPlayerController* LPC = PC();
	if (!S || !LPC || !LPC->PlayerState) return;
	if (S->Phase != ESetupPhase::UnitSelection) return;

	const bool bLocalIsP1 = (LPC->PlayerState == S->Player1);
	const EFaction LocalFaction = bLocalIsP1 ? S->P1Faction : S->P2Faction;
	if (LocalFaction == EFaction::None) return;

	const bool bAlready =
		(CachedLocalFaction == LocalFaction) &&
		UnitsList && UnitsList->GetChildrenCount() > 0;
	if (bAlready) return;

	BuildUnitRows();
	CachedLocalFaction = LocalFaction;
}

void UUnitSelectionWidget::OnP1ReadyClicked()
{
	if (ASetupGameState* S = GS())
	if (ASetupPlayerController* LPC = PC())
	if (LPC->PlayerState == S->Player1 && S->P1Points <= 2000)
		LPC->Server_SetReady(!S->bP1Ready);
}
void UUnitSelectionWidget::OnP2ReadyClicked()
{
	if (ASetupGameState* S = GS())
	if (ASetupPlayerController* LPC = PC())
	if (LPC->PlayerState == S->Player2 && S->P2Points <= 2000)
		LPC->Server_SetReady(!S->bP2Ready);
}
void UUnitSelectionWidget::OnBothReadyClicked()
{
	if (ASetupGameState* S = GS())
	if (ASetupPlayerController* LPC = PC())
	if (LPC->PlayerState == S->Player1 && S->bP1Ready && S->bP2Ready &&
		S->P1Points <= 2000 && S->P2Points <= 2000)
	{
		LPC->Server_SnapshotSetupToPS();
		LPC->Server_AdvanceFromUnits();
	}
}

void UUnitSelectionWidget::BuildUnitRows()
{
	if (!UnitsList || !UnitRowEntryClass) return;

	UnitsList->ClearChildren();

	ASetupGameState* S = GS();
	if (!S || !S->FactionsTable) return;

	ASetupPlayerController* LPC = PC();
	if (!LPC || !LPC->PlayerState) return;

	const bool bLocalIsP1 = (LPC->PlayerState == S->Player1);
	const EFaction Faction = bLocalIsP1 ? S->P1Faction : S->P2Faction;

	UDataTable* UnitsTable = nullptr;
	for (const auto& Pair : S->FactionsTable->GetRowMap())
		if (const FFactionRow* Row = reinterpret_cast<const FFactionRow*>(Pair.Value))
			if (Row->Faction == Faction) { UnitsTable = Row->UnitsTable; break; }

	if (!UnitsTable) return;

	for (const auto& Pair : UnitsTable->GetRowMap())
	{
		const FName UnitId = Pair.Key;
		if (const FUnitRow* U = reinterpret_cast<const FUnitRow*>(Pair.Value))
		{
			if (UUserWidget* RawW = CreateWidget<UUserWidget>(GetOwningPlayer(), UnitRowEntryClass))
				if (UUnitRowWidget* RowW = Cast<UUnitRowWidget>(RawW))
				{
					RowW->InitFull(UnitId, *U);
					UnitsList->AddChild(RawW);
				}
		}
	}
}
