#include "Player/SPCharacterAnimInstance.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	void ConfigureBoolBlend(
		FAnimNode_TwoWayBlend& BlendNode,
		const float BlendInTime,
		const float BlendOutTime)
	{
		BlendNode.AlphaInputType = EAnimAlphaInputType::Bool;
		BlendNode.bAlphaBoolEnabled = false;
		BlendNode.AlphaBoolBlend.BlendInTime = BlendInTime;
		BlendNode.AlphaBoolBlend.BlendOutTime = BlendOutTime;
		BlendNode.AlphaBoolBlend.BlendOption =
			EAlphaBlendOption::HermiteCubic;
	}
}

class FSPCharacterAnimInstanceProxy final : public FAnimInstanceProxy
{
public:
	explicit FSPCharacterAnimInstanceProxy(
		USPCharacterAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
		, CharacterAnimInstance(InAnimInstance)
	{
	}

	virtual void Initialize(UAnimInstance* InAnimInstance) override
	{
		CharacterAnimInstance->LinkPresentationGraph();
		FAnimInstanceProxy::Initialize(InAnimInstance);
	}

	virtual FAnimNode_Base* GetCustomRootNode() override
	{
		return &CharacterAnimInstance->LandingBlendNode;
	}

	virtual void GetCustomNodes(
		TArray<FAnimNode_Base*>& OutNodes) override
	{
		OutNodes.Add(&CharacterAnimInstance->LocomotionNode);
		OutNodes.Add(&CharacterAnimInstance->CrouchNode);
		OutNodes.Add(&CharacterAnimInstance->JumpStartNode);
		OutNodes.Add(&CharacterAnimInstance->FallingNode);
		OutNodes.Add(&CharacterAnimInstance->LandingNode);
		OutNodes.Add(&CharacterAnimInstance->GroundCrouchBlendNode);
		OutNodes.Add(&CharacterAnimInstance->AirPhaseBlendNode);
		OutNodes.Add(&CharacterAnimInstance->GroundAirBlendNode);
		OutNodes.Add(&CharacterAnimInstance->LandingBlendNode);
	}

private:
	USPCharacterAnimInstance* CharacterAnimInstance = nullptr;
};

USPCharacterAnimInstance::USPCharacterAnimInstance()
{
	LocomotionNode.SetLoop(true);
	LocomotionNode.SetPlayRate(1.0f);
	CrouchNode.SetLoop(true);
	CrouchNode.SetPlayRate(1.0f);

	JumpStartNode.SetLoopAnimation(false);
	FallingNode.SetLoopAnimation(true);
	LandingNode.SetLoopAnimation(false);

	ConfigureBoolBlend(GroundCrouchBlendNode, 0.16f, 0.12f);
	ConfigureBoolBlend(AirPhaseBlendNode, 0.04f, 0.02f);
	ConfigureBoolBlend(GroundAirBlendNode, 0.08f, 0.16f);
	ConfigureBoolBlend(LandingBlendNode, 0.05f, 0.12f);
}

void USPCharacterAnimInstance::ConfigurePresentation(
	UBlendSpace* InLocomotionBlendSpace,
	UBlendSpace* InCrouchBlendSpace,
	UAnimSequence* InJumpStartAnimation,
	UAnimSequence* InFallingAnimation,
	UAnimSequence* InLandingAnimation)
{
	LocomotionNode.SetBlendSpace(InLocomotionBlendSpace);
	CrouchNode.SetBlendSpace(InCrouchBlendSpace);
	JumpStartNode.SetSequence(InJumpStartAnimation);
	FallingNode.SetSequence(InFallingAnimation);
	LandingNode.SetSequence(InLandingAnimation);

	bPresentationConfigured =
		InLocomotionBlendSpace
		&& InCrouchBlendSpace
		&& InJumpStartAnimation
		&& InFallingAnimation
		&& InLandingAnimation;
	ResetAirAnimationTimes();
}

FVector USPCharacterAnimInstance::ResolveActorLocalVelocity(
	const FVector& WorldVelocity,
	const FRotator& ActorRotation)
{
	const FQuat ActorYaw = FRotator(
		0.0f,
		ActorRotation.Yaw,
		0.0f).Quaternion();
	const FVector LocalVelocity = ActorYaw.UnrotateVector(WorldVelocity);
	return FVector(LocalVelocity.X, LocalVelocity.Y, 0.0f);
}

FVector USPCharacterAnimInstance::ResolveDiamondBlendInput(
	const FVector& ActorLocalVelocity)
{
	const FVector PlanarVelocity(
		ActorLocalVelocity.X,
		ActorLocalVelocity.Y,
		0.0f);
	const float ComponentSum =
		FMath::Abs(PlanarVelocity.X) + FMath::Abs(PlanarVelocity.Y);
	if (ComponentSum <= UE_KINDA_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}

	const float PlanarSpeed = PlanarVelocity.Size2D();
	return PlanarVelocity * (PlanarSpeed / ComponentSum);
}

void USPCharacterAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	LocomotionBlendInput = FVector::ZeroVector;
	LandingTimeRemaining = 0.0f;
	FallingTime = 0.0f;
	FastestDownwardSpeed = 0.0f;
	bWasFalling = false;
}

void USPCharacterAnimInstance::NativeUpdateAnimation(
	const float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	if (!Character)
	{
		return;
	}

	const FVector ActorLocalVelocity = ResolveActorLocalVelocity(
		Character->GetVelocity(),
		Character->GetActorRotation());
	LocomotionBlendInput =
		ResolveDiamondBlendInput(ActorLocalVelocity);
	LocomotionNode.SetPosition(LocomotionBlendInput);
	CrouchNode.SetPosition(FVector(
		ActorLocalVelocity.Size2D(),
		0.0f,
		0.0f));

	const UCharacterMovementComponent* Movement =
		Character->GetCharacterMovement();
	const bool bIsFalling = Movement && Movement->IsFalling();
	const bool bJustStartedFalling = bIsFalling && !bWasFalling;
	const bool bJustLanded = !bIsFalling && bWasFalling;

	if (bJustStartedFalling)
	{
		ResetAirAnimationTimes();
		LandingTimeRemaining = 0.0f;
		FallingTime = 0.0f;
		FastestDownwardSpeed = Character->GetVelocity().Z;
	}

	if (bIsFalling)
	{
		FallingTime += DeltaSeconds;
		FastestDownwardSpeed = FMath::Min(
			FastestDownwardSpeed,
			Character->GetVelocity().Z);
	}
	else if (bJustLanded)
	{
		const bool bWasMeaningfulFall =
			FallingTime >= MinimumLandingAirTime
			&& FastestDownwardSpeed <= -MinimumLandingDownwardSpeed;
		if (bWasMeaningfulFall)
		{
			LandingNode.SetAccumulatedTime(0.0f);
			LandingTimeRemaining = LandingPresentationDuration;
		}
		else
		{
			LandingTimeRemaining = 0.0f;
		}
		FallingTime = 0.0f;
		FastestDownwardSpeed = 0.0f;
	}
	else if (!bIsFalling)
	{
		LandingTimeRemaining = FMath::Max(
			0.0f,
			LandingTimeRemaining - DeltaSeconds);
	}

	GroundCrouchBlendNode.bAlphaBoolEnabled = Character->bIsCrouched;
	AirPhaseBlendNode.bAlphaBoolEnabled =
		bIsFalling && Character->GetVelocity().Z <= 10.0f;
	GroundAirBlendNode.bAlphaBoolEnabled = bIsFalling;
	LandingBlendNode.bAlphaBoolEnabled =
		LandingTimeRemaining > 0.0f;
	bWasFalling = bIsFalling;
}

FAnimInstanceProxy*
USPCharacterAnimInstance::CreateAnimInstanceProxy()
{
	return new FSPCharacterAnimInstanceProxy(this);
}

void USPCharacterAnimInstance::LinkPresentationGraph()
{
	GroundCrouchBlendNode.A.SetLinkNode(&LocomotionNode);
	GroundCrouchBlendNode.B.SetLinkNode(&CrouchNode);

	AirPhaseBlendNode.A.SetLinkNode(&JumpStartNode);
	AirPhaseBlendNode.B.SetLinkNode(&FallingNode);

	GroundAirBlendNode.A.SetLinkNode(&GroundCrouchBlendNode);
	GroundAirBlendNode.B.SetLinkNode(&AirPhaseBlendNode);

	LandingBlendNode.A.SetLinkNode(&GroundAirBlendNode);
	LandingBlendNode.B.SetLinkNode(&LandingNode);
}

void USPCharacterAnimInstance::ResetAirAnimationTimes()
{
	JumpStartNode.SetAccumulatedTime(0.0f);
	FallingNode.SetAccumulatedTime(0.0f);
}
