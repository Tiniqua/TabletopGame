
#include "TabletopPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PawnMovementComponent.h"

ATabletopPawn::ATabletopPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	
	
	// --- Root / Collision (must exist and be movable)
	USphereComponent* Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(34.f);
	Collision->SetCollisionProfileName(TEXT("Pawn"));
	Collision->SetCanEverAffectNavigation(false);
	SetRootComponent(Collision);
	
	// --- Visual (owner hidden, others see it)
	ThirdPersonVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ThirdPersonVisual"));
	ThirdPersonVisual->SetupAttachment(RootComponent);
	ThirdPersonVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ThirdPersonVisual->SetGenerateOverlapEvents(false);
	ThirdPersonVisual->bOwnerNoSee   = true;
	ThirdPersonVisual->bOnlyOwnerSee = false;
	ThirdPersonVisual->bCastHiddenShadow = true;

	// --- Camera
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(RootComponent);
	Camera->bUsePawnControlRotation = true;
	Camera->SetRelativeLocation(FVector(0.f, 0.f, 60.f)); // small lift so you’re not inside the plane

	// --- Movement
	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->MaxSpeed     = 2400.f;
	Movement->Acceleration = 8000.f;
	Movement->Deceleration = 8000.f;
	Movement->UpdatedComponent = RootComponent; // CRUCIAL

	// Controller drives camera rotation
	bUseControllerRotationPitch = true;
	bUseControllerRotationYaw   = true;
	bUseControllerRotationRoll  = false;

	// Sensitivity
	BaseTurnRate   = 1.f;
	BaseLookUpRate = 1.f;

	bReplicates = true;
	SetReplicateMovement(true);

}

void ATabletopPawn::BeginPlay()
{
	Super::BeginPlay();
	
	SetReplicateMovement(true);
	
	UPawnMovementComponent* MovementComponent = GetMovementComponent();
	if (MovementComponent)
	{
		MovementComponent->UpdateComponentVelocity();
	}
}

void ATabletopPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void ATabletopPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &ATabletopPawn::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"),   this, &ATabletopPawn::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("MoveUp"),      this, &ATabletopPawn::MoveUp);

	PlayerInputComponent->BindAxis(TEXT("Turn"),        this, &ATabletopPawn::Turn);
	PlayerInputComponent->BindAxis(TEXT("LookUp"),      this, &ATabletopPawn::LookUp);
}

void ATabletopPawn::MoveForward(float Value)
{
	if (Value == 0.f) return;
	const FVector Dir = GetActorForwardVector();

	// Client-side for responsiveness
	AddMovementInput(Dir, Value);

	// Mirror input to server (only if we’re not already the authority)
	if (!HasAuthority())
	{
		Server_AddMovementInput(Dir, Value);
	}
}

void ATabletopPawn::MoveRight(float Value)
{
	if (Value == 0.f) return;
	const FVector Dir = GetActorRightVector();
	AddMovementInput(Dir, Value);
	if (!HasAuthority())
	{
		Server_AddMovementInput(Dir, Value);
	}
}

void ATabletopPawn::MoveUp(float Value)
{
	if (Value == 0.f) return;
	const FVector Dir = FVector::UpVector;
	AddMovementInput(Dir, Value);
	if (!HasAuthority())
	{
		Server_AddMovementInput(Dir, Value);
	}
}

void ATabletopPawn::Turn(float Value)
{
	if (Value == 0.f) return;
	AddControllerYawInput(Value * BaseTurnRate);

	// (Optional) replicate yaw so others see you rotate instantly; otherwise rotation updates via movement replication
	if (!HasAuthority())
	{
		Server_SetActorYaw(GetActorRotation().Yaw + Value * BaseTurnRate);
	}
}

void ATabletopPawn::LookUp(float Value)
{
	if (Value == 0.f) return;
	AddControllerPitchInput(Value * BaseLookUpRate);
}

void ATabletopPawn::Server_AddMovementInput_Implementation(FVector WorldDirection, float Scale)
{
	// Apply on the authority; replicated movement will update everyone (including host)
	AddMovementInput(WorldDirection, Scale);
}

void ATabletopPawn::Server_SetActorYaw_Implementation(float Yaw)
{
	FRotator R = GetActorRotation();
	R.Yaw = Yaw;
	SetActorRotation(R);
}