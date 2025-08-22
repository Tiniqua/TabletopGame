
#include "TabletopPlayerState.h"

#include "Tabletop/Gamemodes/MatchGameMode.h"

static void BroadcastDeploymentChanged(UWorld* World)
{
	if (!World) return;
	if (AMatchGameState* GS = World->GetGameState<AMatchGameState>())
	{
		GS->OnDeploymentChanged.Broadcast(); // local broadcast on this client/server instance
	}
}

void ATabletopPlayerState::OnRep_PlayerIdentity()
{
	BroadcastDeploymentChanged(GetWorld());
}

void ATabletopPlayerState::OnRep_TeamNum()
{
	BroadcastDeploymentChanged(GetWorld());
}

void ATabletopPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATabletopPlayerState, DisplayName);
	DOREPLIFETIME(ATabletopPlayerState, SelectedFaction);
	DOREPLIFETIME(ATabletopPlayerState, Roster);
	DOREPLIFETIME(ATabletopPlayerState, TeamNum);
}

void ATabletopPlayerState::CopyProperties(APlayerState* PS)
{
	Super::CopyProperties(PS);
	if (auto* TPS = Cast<ATabletopPlayerState>(PS))
	{
		TPS->DisplayName     = DisplayName;
		TPS->SelectedFaction = SelectedFaction;
		TPS->Roster          = Roster;
	}

	BroadcastDeploymentChanged(GetWorld());
}

void ATabletopPlayerState::OverrideWith(APlayerState* PS)
{
	Super::OverrideWith(PS);
	if (auto* TPS = Cast<ATabletopPlayerState>(PS))
	{
		DisplayName     = TPS->DisplayName;
		SelectedFaction = TPS->SelectedFaction;
		Roster          = TPS->Roster;
	}
	BroadcastDeploymentChanged(GetWorld());
}