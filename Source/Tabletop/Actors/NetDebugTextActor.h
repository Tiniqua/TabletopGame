

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NetDebugTextActor.generated.h"

class UTextRenderComponent;
UCLASS()
class TABLETOP_API ANetDebugTextActor : public AActor
{
	GENERATED_BODY()
public:
	ANetDebugTextActor();

	UFUNCTION(BlueprintCallable)
	void Init(const FString& InText, const FColor& InColor, float InWorldSize, float InLifetime);

	// If true, rotates every tick to face the local camera on each client
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DebugText")
	bool bFaceCamera = true;

protected:
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere)
	USceneComponent* Root;

	UPROPERTY(VisibleAnywhere)
	UTextRenderComponent* Text;

};
