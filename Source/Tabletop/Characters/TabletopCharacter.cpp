
#include "TabletopCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"

ATabletopCharacter::ATabletopCharacter()
{
    PrimaryActorTick.bCanEverTick = false;

    // Capsule (comes from ACharacter), set a simple visual if you want
    Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
    Visual->SetupAttachment(GetCapsuleComponent());
    Visual->SetIsReplicated(true);

    // Camera boom + camera
    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(GetCapsuleComponent());
    CameraBoom->TargetArmLength = 0.f;      // first-person style (0) or increase for 3rd-person
    CameraBoom->bUsePawnControlRotation = true;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraBoom);
    Camera->bUsePawnControlRotation = false;

    // Character movement replication/prediction is built-in
    bReplicates = true;
    SetReplicateMovement(true);
    //SetNetUseOwnerRelevancy(true);

    // Configure movement to FLY so Up/Down works like your pawn
    UCharacterMovementComponent* Move = GetCharacterMovement();
    Move->DefaultLandMovementMode = MOVE_Flying;   // keep walking available if you ever switch
    Move->NavAgentProps.bCanCrouch = false;
    Move->bOrientRotationToMovement = false;        // we’ll use controller yaw
    bUseControllerRotationYaw = true;

    Move->SetMovementMode(MOVE_Flying);
    Move->MaxWalkSpeed = WalkSpeed;
    Move->MaxFlySpeed  = FlySpeed;
    Move->RotationRate = FRotator(0.f, 720.f, 0.f);
    
    Move->BrakingDecelerationFlying = 2000.f;   // stop quicker
    Move->bUseSeparateBrakingFriction = true;
    Move->BrakingFriction = 1.f;
    Move->BrakingFrictionFactor = 1.0f;
    Move->BrakingSubStepTime = 0.005f;

    Move->bOrientRotationToMovement = false;
    bUseControllerRotationYaw = true;
    
}

void ATabletopCharacter::BeginPlay()
{
    Super::BeginPlay();
    // Ensure we start in flying mode
    if (GetCharacterMovement()->MovementMode != MOVE_Flying)
    {
        GetCharacterMovement()->SetMovementMode(MOVE_Flying);
    }
}

void ATabletopCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    check(PlayerInputComponent);
    PlayerInputComponent->BindAxis("MoveForward", this, &ATabletopCharacter::MoveForward);
    PlayerInputComponent->BindAxis("MoveRight",   this, &ATabletopCharacter::MoveRight);
    PlayerInputComponent->BindAxis("MoveUp",      this, &ATabletopCharacter::MoveUp);
    PlayerInputComponent->BindAxis("Turn",        this, &ATabletopCharacter::Turn);
    PlayerInputComponent->BindAxis("LookUp",      this, &ATabletopCharacter::LookUp);
}

void ATabletopCharacter::MoveForward(float Value)
{
    if (Value != 0.f)
    {
        AddMovementInput(GetActorForwardVector(), Value);
    }
}

void ATabletopCharacter::MoveRight(float Value)
{
    if (Value != 0.f)
    {
        AddMovementInput(GetActorRightVector(), Value);
    }
}

void ATabletopCharacter::MoveUp(float Value)
{
    if (Value != 0.f)
    {
        AddMovementInput(FVector::UpVector, Value);
    }
}

void ATabletopCharacter::Turn(float Value)
{
    if (Value != 0.f)
    {
        AddControllerYawInput(Value * BaseTurnRate);
    }
}

void ATabletopCharacter::LookUp(float Value)
{
    if (Value != 0.f)
    {
        AddControllerPitchInput(Value * BaseLookUpRate);
    }
}
