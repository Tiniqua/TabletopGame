
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TabletopCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;


UCLASS()
class TABLETOP_API ATabletopCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ATabletopCharacter();

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:
	virtual void BeginPlay() override;

	// Camera boom + camera
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	USpringArmComponent* CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	UCameraComponent* Camera;

	// Optional visual (static mesh if you’re not using a skeletal mesh)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	UStaticMeshComponent* Visual;

	// Input tuning
	UPROPERTY(EditAnywhere, Category="Movement")
	float WalkSpeed = 600.f;

	UPROPERTY(EditAnywhere, Category="Movement")
	float FlySpeed = 1200.f;

	UPROPERTY(EditAnywhere, Category="Input")
	float BaseTurnRate = 1.0f;

	UPROPERTY(EditAnywhere, Category="Input")
	float BaseLookUpRate = 1.0f;

	// Input handlers
	void MoveForward(float Value);
	void MoveRight(float Value);
	void MoveUp(float Value);
	void Turn(float Value);
	void LookUp(float Value);

};
