#include "Framework/ScrollCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "Framework/ScrollPlayerController.h"
#include "Framework/ScrollPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputActionValue.h"
#include "ScrollLog.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float CapsuleRadius = 40.f;
	constexpr float CapsuleHalfHeight = 90.f;
	constexpr float EyeHeight = 64.f;
	// Engine basic shapes are 100 units across.
	constexpr float BasicShapeSize = 100.f;
}

AScrollCharacter::AScrollCharacter()
{
	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);

	// First person: the body turns with the view; moving does not rotate it.
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	GetCharacterMovement()->bOrientRotationToMovement = false;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, EyeHeight));
	FirstPersonCamera->bUsePawnControlRotation = true;

	// When a real body mesh is assigned it is for other players only.
	GetMesh()->SetOwnerNoSee(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(GetCapsuleComponent());
	PlaceholderBody->SetStaticMesh(CylinderMesh.Object);
	PlaceholderBody->SetRelativeScale3D(FVector(CapsuleRadius * 2.f, CapsuleRadius * 2.f, CapsuleHalfHeight * 2.f) / BasicShapeSize);
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetOwnerNoSee(true);

	PlaceholderVisor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderVisor"));
	PlaceholderVisor->SetupAttachment(GetCapsuleComponent());
	PlaceholderVisor->SetStaticMesh(CubeMesh.Object);
	PlaceholderVisor->SetRelativeLocation(FVector(CapsuleRadius, 0.f, EyeHeight));
	PlaceholderVisor->SetRelativeScale3D(FVector(15.f, 50.f, 15.f) / BasicShapeSize);
	PlaceholderVisor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderVisor->SetOwnerNoSee(true);
}

void AScrollCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Evidence for the two-player check: which copy is this on which machine.
	const AScrollPlayerState* ScrollPlayerState = GetPlayerState<AScrollPlayerState>();
	UE_LOG(LogScroll, Log, TEXT("%s BeginPlay: Participant=%d LocalRole=%s RemoteRole=%s LocallyControlled=%d"),
		*GetName(),
		ScrollPlayerState ? ScrollPlayerState->GetParticipantSessionId() : 0,
		*UEnum::GetValueAsString(GetLocalRole()),
		*UEnum::GetValueAsString(GetRemoteRole()),
		IsLocallyControlled() ? 1 : 0);
}

void AScrollCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	const AScrollPlayerController* ScrollController = GetController<AScrollPlayerController>();
	if (!EnhancedInput || !ScrollController || !ScrollController->GetInputSet().IsComplete())
	{
		UE_LOG(LogScroll, Error, TEXT("%s: needs EnhancedInputComponent and an AScrollPlayerController with a complete input set."), *GetName());
		return;
	}

	const FScrollInputSet& Input = ScrollController->GetInputSet();
	EnhancedInput->BindAction(Input.Jump, ETriggerEvent::Started, this, &ACharacter::Jump);
	EnhancedInput->BindAction(Input.Jump, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	EnhancedInput->BindAction(Input.Move, ETriggerEvent::Triggered, this, &AScrollCharacter::Move);
	EnhancedInput->BindAction(Input.Look, ETriggerEvent::Triggered, this, &AScrollCharacter::Look);
}

void AScrollCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddMovementInput(GetActorForwardVector(), Axis.Y);
	AddMovementInput(GetActorRightVector(), Axis.X);
}

void AScrollCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}
