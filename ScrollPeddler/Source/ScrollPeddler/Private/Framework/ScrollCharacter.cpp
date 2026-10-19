#include "Framework/ScrollCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/CollisionProfile.h"
#include "Framework/ScrollCharacterMovementComponent.h"
#include "Framework/ScrollPlayerController.h"
#include "Framework/ScrollPlayerState.h"
#include "InputActionValue.h"
#include "Presentation/ScrollHeadBobComponent.h"
#include "ScrollLog.h"

namespace
{
	// Mannequin proportions, same as the First Person template.
	constexpr float CapsuleRadius = 34.f;
	constexpr float CapsuleHalfHeight = 96.f;
}

AScrollCharacter::AScrollCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UScrollCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);

	// First person: the body turns with the view; moving does not rotate it.
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	// Air feel from the First Person template.
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.f;
	GetCharacterMovement()->AirControl = 0.5f;

	// Full body for other players. Also casts this player's own shadow while hidden from them.
	GetMesh()->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	GetMesh()->SetOwnerNoSee(true);
	// Mannequin meshes have their root at the feet and face +Y.
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -CapsuleHalfHeight), FRotator(0.f, -90.f, 0.f));
	// The owner never renders the body, but the arms copy its pose, so its bones must keep updating.
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonMesh"));
	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	// Camera on the arms mesh's head bone, offsets from the First Person template.
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(FirstPersonMesh, TEXT("head"));
	FirstPersonCamera->SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.f), FRotator(0.f, 90.f, -90.f));
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetEnableFirstPersonFieldOfView(true);
	FirstPersonCamera->SetFirstPersonFieldOfView(70.f);
	FirstPersonCamera->SetEnableFirstPersonScale(true);
	FirstPersonCamera->SetFirstPersonScale(0.6f);

	HeadBob = CreateDefaultSubobject<UScrollHeadBobComponent>(TEXT("HeadBob"));
}

UScrollCharacterMovementComponent* AScrollCharacter::GetScrollMovement() const
{
	return CastChecked<UScrollCharacterMovementComponent>(GetCharacterMovement());
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

	if (!GetMesh()->GetSkeletalMeshAsset() || !FirstPersonMesh->GetSkeletalMeshAsset())
	{
		UE_LOG(LogScroll, Warning, TEXT("%s has no body or arms mesh. Use BP_SpikeCharacter (or another Blueprint subclass that assigns them)."), *GetName());
	}
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
	EnhancedInput->BindAction(Input.Sprint, ETriggerEvent::Started, this, &AScrollCharacter::StartSprint);
	EnhancedInput->BindAction(Input.Sprint, ETriggerEvent::Completed, this, &AScrollCharacter::StopSprint);
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

void AScrollCharacter::StartSprint()
{
	GetScrollMovement()->SetWantsToSprint(true);
}

void AScrollCharacter::StopSprint()
{
	GetScrollMovement()->SetWantsToSprint(false);
}
