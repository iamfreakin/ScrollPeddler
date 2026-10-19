#include "Presentation/ScrollHeadBobComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Presentation/ScrollFootstepComponent.h"
#include "Presentation/ScrollHeadBobCameraShake.h"

FScrollHeadBobAmplitude FScrollHeadBobAmplitude::Lerp(const FScrollHeadBobAmplitude& A, const FScrollHeadBobAmplitude& B, float Alpha)
{
	return { FMath::Lerp(A.Pitch, B.Pitch, Alpha), FMath::Lerp(A.Yaw, B.Yaw, Alpha), FMath::Lerp(A.Roll, B.Roll, Alpha) };
}

FScrollHeadBobAmplitude FScrollHeadBobAmplitude::InterpTo(const FScrollHeadBobAmplitude& Target, float DeltaTime, float InterpSpeed) const
{
	return {
		FMath::FInterpTo(Pitch, Target.Pitch, DeltaTime, InterpSpeed),
		FMath::FInterpTo(Yaw, Target.Yaw, DeltaTime, InterpSpeed),
		FMath::FInterpTo(Roll, Target.Roll, DeltaTime, InterpSpeed) };
}

UScrollHeadBobComponent::UScrollHeadBobComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Comfort-first starting values. Roll stays at zero because it is the strongest motion-sickness trigger.
	Walk = { 0.3f, 0.2f, 0.f };
	Sprint = { 0.6f, 0.35f, 0.f };
}

void UScrollHeadBobComponent::SetIntensity(float NewIntensity)
{
	Intensity = FMath::Clamp(NewIntensity, 0.f, 1.f);
}

void UScrollHeadBobComponent::BeginPlay()
{
	Super::BeginPlay();
	Footsteps = GetOwner()->FindComponentByClass<UScrollFootstepComponent>();
	if (Footsteps)
	{
		// Read this frame's step count, not last frame's.
		AddTickPrerequisiteComponent(Footsteps);
	}
}

void UScrollHeadBobComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const ACharacter* Character = GetOwner<ACharacter>();
	const APlayerController* PlayerController = Character ? Character->GetController<APlayerController>() : nullptr;
	if (!PlayerController || !PlayerController->IsLocalController() || !PlayerController->PlayerCameraManager)
	{
		StopShake();
		return;
	}

	UCameraShakeBase* Shake = ActiveShake.Get();
	if (!Shake || !Shake->IsActive() || ShakeCameraManager.Get() != PlayerController->PlayerCameraManager)
	{
		StopShake();
		Shake = PlayerController->PlayerCameraManager->StartCameraShake(UScrollHeadBobCameraShake::StaticClass());
		ActiveShake = Shake;
		ShakeCameraManager = PlayerController->PlayerCameraManager;
	}

	// Pick target strengths: idle layer while standing, step layer while moving, nothing in the air.
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const bool bOnGround = Movement && Movement->IsMovingOnGround();
	const float Speed = Movement ? Movement->Velocity.Size2D() : 0.f;
	FScrollHeadBobAmplitude IdleTarget;
	FScrollHeadBobAmplitude StepTarget;
	if (bOnGround && Speed <= StandingSpeed)
	{
		IdleTarget = Idle;
	}
	else if (bOnGround)
	{
		const float RunAlpha = FMath::Clamp(UScrollFootstepComponent::GetRunAlpha(*Character), 0.f, 1.f);
		// Below walk speed (slowing down, analog input) the bob shrinks with speed.
		const float WalkFraction = FMath::Clamp(Speed / FMath::Max(Movement->MaxWalkSpeed, 1.f), 0.f, 1.f);
		StepTarget = FScrollHeadBobAmplitude::Lerp(Walk, Sprint, RunAlpha) * WalkFraction;
	}
	CurrentIdle = CurrentIdle.InterpTo(IdleTarget, DeltaTime, BlendSpeed);
	CurrentStep = CurrentStep.InterpTo(StepTarget, DeltaTime, BlendSpeed);

	IdlePhase = FMath::Fmod(IdlePhase + UE_TWO_PI * IdleFrequency * DeltaTime, UE_TWO_PI);
	const float Steps = Footsteps ? Footsteps->GetStepCycles() : 0.f;

	// Each whole step is a footfall: the view is lowest then (pitch), and sways to alternate sides each step (yaw, roll).
	const float StepDip = -FMath::Cos(UE_TWO_PI * Steps);
	const float StepSway = FMath::Sin(UE_PI * Steps);
	const float IdleBreath = FMath::Sin(IdlePhase);
	const float IdleDrift = FMath::Sin(0.5f * IdlePhase);
	const FRotator Offset(
		CurrentStep.Pitch * StepDip + CurrentIdle.Pitch * IdleBreath,
		CurrentStep.Yaw * StepSway + CurrentIdle.Yaw * IdleDrift,
		CurrentStep.Roll * StepSway + CurrentIdle.Roll * IdleDrift);

	if (UScrollHeadBobShakePattern* Pattern = Shake ? Cast<UScrollHeadBobShakePattern>(Shake->GetRootShakePattern()) : nullptr)
	{
		Pattern->Offset = Offset * Intensity;
	}
}

void UScrollHeadBobComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopShake();
	Super::EndPlay(EndPlayReason);
}

void UScrollHeadBobComponent::StopShake()
{
	if (UCameraShakeBase* Shake = ActiveShake.Get())
	{
		if (APlayerCameraManager* CameraManager = ShakeCameraManager.Get())
		{
			CameraManager->StopCameraShake(Shake, true);
		}
	}
	ActiveShake.Reset();
	ShakeCameraManager.Reset();
	CurrentIdle = FScrollHeadBobAmplitude();
	CurrentStep = FScrollHeadBobAmplitude();
}
