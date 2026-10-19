#include "Presentation/ScrollHeadBobComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Framework/ScrollCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"

namespace
{
	FScrollHeadBobProfile MakeProfile(float PitchAmplitude, float PitchFrequency, float YawAmplitude, float YawFrequency, float RollAmplitude, float RollFrequency)
	{
		FScrollHeadBobProfile Profile;
		Profile.PitchAmplitude = PitchAmplitude;
		Profile.PitchFrequency = PitchFrequency;
		Profile.YawAmplitude = YawAmplitude;
		Profile.YawFrequency = YawFrequency;
		Profile.RollAmplitude = RollAmplitude;
		Profile.RollFrequency = RollFrequency;
		return Profile;
	}
}

UScrollHeadBobComponent::UScrollHeadBobComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Comfort-first starting values. Idle is off because the camera already rides the head bone's idle motion.
	// Pitch runs at roughly step rate and yaw at half of it; roll stays at zero because it is the strongest motion-sickness trigger.
	Idle = MakeProfile(0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
	Walk = MakeProfile(0.3f, 2.0f, 0.2f, 1.0f, 0.f, 0.f);
	Sprint = MakeProfile(0.6f, 2.6f, 0.35f, 1.3f, 0.f, 0.f);
}

void UScrollHeadBobComponent::SetIntensity(float NewIntensity)
{
	Intensity = FMath::Clamp(NewIntensity, 0.f, 1.f);
}

void UScrollHeadBobComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const APawn* Pawn = GetOwner<APawn>();
	const APlayerController* PlayerController = Pawn ? Pawn->GetController<APlayerController>() : nullptr;
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

	Current = Current.InterpTo(GetTargetProfile(), DeltaTime, BlendSpeed);
	if (UScrollHeadBobShakePattern* Pattern = Shake ? Cast<UScrollHeadBobShakePattern>(Shake->GetRootShakePattern()) : nullptr)
	{
		Pattern->Profile = Current.WithAmplitudeScale(Intensity);
	}
}

void UScrollHeadBobComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopShake();
	Super::EndPlay(EndPlayReason);
}

FScrollHeadBobProfile UScrollHeadBobComponent::GetTargetProfile() const
{
	const ACharacter* Character = GetOwner<ACharacter>();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement || !Movement->IsMovingOnGround())
	{
		return FScrollHeadBobProfile();
	}

	const float Speed = Movement->Velocity.Size2D();
	if (Speed <= StandingSpeed)
	{
		return Idle;
	}

	// Thresholds follow the movement component, so changing walk/sprint speed never misclassifies walking as running.
	float WalkSpeed = Movement->MaxWalkSpeed;
	float SprintSpeed = WalkSpeed;
	if (const UScrollCharacterMovementComponent* ScrollMovement = Cast<UScrollCharacterMovementComponent>(Movement))
	{
		WalkSpeed = ScrollMovement->GetWalkSpeed();
		SprintSpeed = ScrollMovement->GetSprintSpeed();
	}

	if (Speed <= WalkSpeed || SprintSpeed <= WalkSpeed)
	{
		return FScrollHeadBobProfile::Lerp(Idle, Walk, FMath::Clamp(Speed / FMath::Max(WalkSpeed, 1.f), 0.f, 1.f));
	}
	return FScrollHeadBobProfile::Lerp(Walk, Sprint, FMath::Clamp((Speed - WalkSpeed) / (SprintSpeed - WalkSpeed), 0.f, 1.f));
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
	Current = FScrollHeadBobProfile();
}
