
#include "UnitActionResourceComponent.h"

#include "Gamemodes/MatchGameMode.h"

UUnitActionResourceComponent::UUnitActionResourceComponent()
{
	SetIsReplicatedByDefault(true);
}

void UUnitActionResourceComponent::OnRep_AP()
{
	if (UWorld* W = GetWorld())
	{
		if (AMatchGameState* S = W->GetGameState<AMatchGameState>())
		{
			S->OnDeploymentChanged.Broadcast(); // kicks widgets to Refresh()
		}	
	}
}

void UUnitActionResourceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UUnitActionResourceComponent, CurrentAP);
	DOREPLIFETIME(UUnitActionResourceComponent, MaxAP);
	DOREPLIFETIME(UUnitActionResourceComponent, Scope);
}

void UUnitActionResourceComponent::ResetForPhase()
{
	MaxAP = DefaultMaxAP; CurrentAP = MaxAP;
}

void UUnitActionResourceComponent::ResetForTurn()
{
	MaxAP = DefaultMaxAP; CurrentAP = MaxAP;
}

bool UUnitActionResourceComponent::CanPay(int32 Cost) const
{
	return Cost <= CurrentAP; 
}

bool UUnitActionResourceComponent::Pay(int32 Cost)
{
	if (!CanPay(Cost)) return false;
	CurrentAP -= Cost;
	return true;
}

void UUnitActionResourceComponent::Refund(int32 Amount)
{
	CurrentAP = FMath::Clamp(CurrentAP + Amount, 0, MaxAP);
}

void UUnitActionResourceComponent::Grant(int32 Amount, int32 NewCap)
{
	if (NewCap > 0) MaxAP = NewCap;
	CurrentAP = FMath::Clamp(CurrentAP + Amount, 0, MaxAP);
}