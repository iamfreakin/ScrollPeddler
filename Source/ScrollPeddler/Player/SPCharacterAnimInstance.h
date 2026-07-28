#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "AnimNodes/AnimNode_TwoWayBlend.h"
#include "SPCharacterAnimInstance.generated.h"

class UAnimSequence;
class UBlendSpace;

/**
 * Native presentation graph for the KayKit pilot.
 *
 * Locomotion remains content-tunable in Blend Space assets while this class
 * owns the small state graph and transition timing used by every network role.
 */
UCLASS(Transient, NotBlueprintable)
class SCROLLPEDDLER_API USPCharacterAnimInstance final : public UAnimInstance
{
	GENERATED_BODY()

public:
	USPCharacterAnimInstance();

	void ConfigurePresentation(
		UBlendSpace* InLocomotionBlendSpace,
		UBlendSpace* InCrouchBlendSpace,
		UAnimSequence* InJumpStartAnimation,
		UAnimSequence* InFallingAnimation,
		UAnimSequence* InLandingAnimation);

	bool IsPresentationConfigured() const { return bPresentationConfigured; }
	FVector GetLocomotionBlendInput() const { return LocomotionBlendInput; }

	/** Converts replicated world velocity into actor-local Blend Space axes. */
	static FVector ResolveActorLocalVelocity(
		const FVector& WorldVelocity,
		const FRotator& ActorRotation);

	/**
	 * Maps a circular movement velocity onto the cardinal-sample diamond.
	 * This keeps a constant-speed diagonal on the same Blend Space ring as
	 * cardinal movement instead of incorrectly selecting the sprint edge.
	 */
	static FVector ResolveDiamondBlendInput(
		const FVector& ActorLocalVelocity);

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	friend class FSPCharacterAnimInstanceProxy;

	void LinkPresentationGraph();
	void ResetAirAnimationTimes();

	UPROPERTY(Transient)
	FAnimNode_BlendSpacePlayer_Standalone LocomotionNode;

	UPROPERTY(Transient)
	FAnimNode_BlendSpacePlayer_Standalone CrouchNode;

	UPROPERTY(Transient)
	FAnimNode_SequencePlayer_Standalone JumpStartNode;

	UPROPERTY(Transient)
	FAnimNode_SequencePlayer_Standalone FallingNode;

	UPROPERTY(Transient)
	FAnimNode_SequencePlayer_Standalone LandingNode;

	UPROPERTY(Transient)
	FAnimNode_TwoWayBlend GroundCrouchBlendNode;

	UPROPERTY(Transient)
	FAnimNode_TwoWayBlend AirPhaseBlendNode;

	UPROPERTY(Transient)
	FAnimNode_TwoWayBlend GroundAirBlendNode;

	UPROPERTY(Transient)
	FAnimNode_TwoWayBlend LandingBlendNode;

	UPROPERTY(Transient)
	FVector LocomotionBlendInput = FVector::ZeroVector;

	UPROPERTY(Transient)
	float LandingTimeRemaining = 0.0f;

	UPROPERTY(Transient)
	float FallingTime = 0.0f;

	UPROPERTY(Transient)
	float FastestDownwardSpeed = 0.0f;

	UPROPERTY(Transient)
	bool bPresentationConfigured = false;

	UPROPERTY(Transient)
	bool bWasFalling = false;

	static constexpr float LandingPresentationDuration = 0.3f;
	static constexpr float MinimumLandingAirTime = 0.18f;
	static constexpr float MinimumLandingDownwardSpeed = 180.0f;
};
