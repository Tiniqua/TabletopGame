
#include "UnitBase.h"

AUnitBase::AUnitBase()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AUnitBase::BeginPlay()
{
	Super::BeginPlay();
}

void AUnitBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

