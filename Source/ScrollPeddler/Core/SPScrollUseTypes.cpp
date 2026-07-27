#include "Core/SPScrollUseTypes.h"

namespace
{
bool IsFiniteNonNegative(const float Value)
{
	return FMath::IsFinite(Value) && Value >= 0.0f;
}

bool IsFiniteProbability(const float Value)
{
	return FMath::IsFinite(Value) && Value >= 0.0f && Value <= 1.0f;
}
}

bool FSPScrollEffectSpec::IsValid() const
{
	if (!StaticEnum<ESPResolvedScrollEffectKind>()->IsValidEnumValue(
			static_cast<int64>(Kind))
		|| !StaticEnum<ESPScrollEffectTarget>()->IsValidEnumValue(
			static_cast<int64>(Target))
		|| !FMath::IsFinite(Magnitude)
		|| !IsFiniteNonNegative(Radius)
		|| !IsFiniteNonNegative(DurationSeconds))
	{
		return false;
	}

	return Kind == ESPResolvedScrollEffectKind::Noise
		? !FMath::IsNearlyZero(Magnitude)
		: Magnitude > 0.0f;
}

bool FSPScrollFamilyTuning::IsValid() const
{
	return BaseDefinitionId.IsValid()
		&& !StableId.IsNone()
		&& StaticEnum<ESPScrollBaseFamily>()->IsValidEnumValue(
			static_cast<int64>(Family))
		&& !BaseEffects.IsEmpty()
		&& !BaseEffects.ContainsByPredicate(
			[](const FSPScrollEffectSpec& Effect)
			{
				return !Effect.IsValid();
			})
		&& IsFiniteProbability(BaseMalfunctionChance)
		&& IsFiniteProbability(ContaminationChanceAtMaximum)
		&& IsFiniteNonNegative(MalfunctionDelaySeconds)
		&& FMath::IsFinite(DirectionShiftDegrees)
		&& DirectionShiftDegrees >= 0.0f
		&& DirectionShiftDegrees <= 180.0f
		&& IsFiniteNonNegative(MalfunctionNoiseMagnitude);
}

bool FSPScrollEngravingUseTuning::IsValid() const
{
	return EngravingDefinitionId.IsValid()
		&& !StableId.IsNone()
		&& FMath::IsFinite(MagnitudeMultiplier)
		&& MagnitudeMultiplier > 0.0f
		&& FMath::IsFinite(RadiusMultiplier)
		&& RadiusMultiplier > 0.0f
		&& FMath::IsFinite(DurationMultiplier)
		&& DurationMultiplier > 0.0f
		&& FMath::IsFinite(MalfunctionChanceDelta)
		&& MalfunctionChanceDelta >= -1.0f
		&& MalfunctionChanceDelta <= 1.0f
		&& IsFiniteNonNegative(AddedNoiseMagnitude);
}
