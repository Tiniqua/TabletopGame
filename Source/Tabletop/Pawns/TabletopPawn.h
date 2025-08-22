
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "TabletopPawn.generated.h"

class UFloatingPawnMovement;
class UCameraComponent;
UCLASS()
class TABLETOP_API ATabletopPawn : public APawn
{
	GENERATED_BODY()

public:
	ATabletopPawn();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Visual")
	UStaticMeshComponent* ThirdPersonVisual;

	// Camera for local view
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	UCameraComponent* Camera;

	// Movement (free-fly)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	UFloatingPawnMovement* Movement;

	// Input rates (if you want to scale mouse/controller)
	UPROPERTY(EditAnywhere, Category="Input")
	float BaseTurnRate = 1.0f;   // mouse delta is already “per frame”; usually 1.0
	UPROPERTY(EditAnywhere, Category="Input")
	float BaseLookUpRate = 1.0f;

public:	
	virtual void Tick(float DeltaTime) override;

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	void MoveForward(float Value);
	void MoveRight(float Value);
	void MoveUp(float Value);
	void Turn(float Value);
	void LookUp(float Value);

private:
	/** Generic movement RPC so we don’t need one per axis */
	UFUNCTION(Server, Unreliable)
	void Server_AddMovementInput(FVector WorldDirection, float Scale);

	/** (Optional) replicate facing; if you want other clients to see rotation changes immediately */
	UFUNCTION(Server, Unreliable)
	void Server_SetActorYaw(float Yaw);
};
