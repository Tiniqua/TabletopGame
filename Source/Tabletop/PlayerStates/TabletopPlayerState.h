
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Tabletop/ArmyData.h"
#include "Tabletop/Gamemodes/SetupGamemode.h"
#include "TabletopPlayerState.generated.h"

UCLASS()
class TABLETOP_API ATabletopPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	UPROPERTY(ReplicatedUsing=OnRep_PlayerIdentity)
	FString DisplayName;

	UPROPERTY(ReplicatedUsing=OnRep_PlayerIdentity)
	EFaction SelectedFaction;
	
	UPROPERTY(Replicated, BlueprintReadOnly)
	TArray<FUnitCount> Roster;

	UFUNCTION()
	void OnRep_PlayerIdentity();

	UPROPERTY(ReplicatedUsing=OnRep_TeamNum, BlueprintReadOnly, Category="Match")
	int32 TeamNum = 0;   // 0 = unset, 1 = team1, 2 = team2

	UFUNCTION()
	void OnRep_TeamNum();

	UFUNCTION() void OnRep_PlayerInfo(){}

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void CopyProperties(APlayerState* PS) override;
	virtual void OverrideWith(APlayerState* PS) override;
};
