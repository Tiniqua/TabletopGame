
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UnitBase.generated.h"

UCLASS()
class TABLETOP_API AUnitBase : public AActor
{
	GENERATED_BODY()
	
public:	
	AUnitBase();

protected:
	virtual void BeginPlay() override;

public:	
	virtual void Tick(float DeltaTime) override;

};
