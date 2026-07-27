#include "Game/SPScrollUseResolver.h"

#include "Misc/Crc.h"

namespace
{
bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X)
		&& FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}

ESPScrollMalfunctionOutcome ResolveMalfunctionType(
	const ESPMisfireType Misfire)
{
	switch (Misfire)
	{
	case ESPMisfireType::Delay:
		return ESPScrollMalfunctionOutcome::Delay;
	case ESPMisfireType::DirectionShift:
		return ESPScrollMalfunctionOutcome::DirectionShift;
	case ESPMisfireType::ExtraNoise:
		return ESPScrollMalfunctionOutcome::ExtraNoise;
	case ESPMisfireType::None:
	default:
		return ESPScrollMalfunctionOutcome::Fizzle;
	}
}

FSPResolvedScrollEffect ResolveEffect(
	const FSPScrollEffectSpec& Spec,
	const float QualityMultiplier,
	const FSPScrollEngravingUseTuning& Engraving,
	const FVector& Direction)
{
	FSPResolvedScrollEffect Effect;
	Effect.Kind = Spec.Kind;
	Effect.Target = Spec.Target;
	Effect.Magnitude =
		Spec.Magnitude * QualityMultiplier * Engraving.MagnitudeMultiplier;
	Effect.Radius = Spec.Radius * Engraving.RadiusMultiplier;
	Effect.DurationSeconds =
		Spec.DurationSeconds * Engraving.DurationMultiplier;
	Effect.Direction = Direction;
	return Effect;
}

void AddNoiseEffect(
	TArray<FSPResolvedScrollEffect>& Effects,
	const float Magnitude,
	const FVector& Direction,
	const bool bGeneratedByMalfunction)
{
	if (Magnitude <= 0.0f)
	{
		return;
	}

	FSPResolvedScrollEffect Noise;
	Noise.Kind = ESPResolvedScrollEffectKind::Noise;
	Noise.Target = ESPScrollEffectTarget::Area;
	Noise.Magnitude = Magnitude;
	Noise.Radius = 1000.0f * Magnitude;
	Noise.Direction = Direction;
	Noise.bGeneratedByMalfunction = bGeneratedByMalfunction;
	Effects.Add(Noise);
}
}

FSPScrollUseResult FSPScrollUseResolver::Resolve(
	const FSPScrollUseRequest& Request,
	const FSPAuthoritativeScrollUseState& AuthorityState,
	const FSPScrollFamilyTuning& Family,
	const FSPScrollEngravingUseTuning& Engraving)
{
	FSPScrollUseResult Result;
	Result.RequestId = Request.RequestId;
	Result.ScrollInstanceId = Request.ScrollInstanceId;

	if (!AuthorityState.bServerAuthority)
	{
		Result.Code = ESPScrollUseResultCode::NotAuthority;
		return Result;
	}
	if (!Request.RequestId.IsValid()
		|| !IsFiniteVector(Request.AimDirection)
		|| Request.AimDirection.IsNearlyZero())
	{
		Result.Code = ESPScrollUseResultCode::InvalidRequest;
		return Result;
	}
	if (!Request.ScrollInstanceId.IsValid()
		|| !AuthorityState.Scroll.IsValid()
		|| AuthorityState.Scroll.InstanceId != Request.ScrollInstanceId
		|| !FMath::IsFinite(AuthorityState.Scroll.Contamination)
		|| AuthorityState.Scroll.Contamination < 0.0f
		|| AuthorityState.Scroll.Contamination > 100.0f
		|| !StaticEnum<ESPScrollQuality>()->IsValidEnumValue(
			static_cast<int64>(AuthorityState.Scroll.Quality))
		|| !StaticEnum<ESPMisfireType>()->IsValidEnumValue(
			static_cast<int64>(AuthorityState.Scroll.Misfire)))
	{
		Result.Code = ESPScrollUseResultCode::InvalidScroll;
		return Result;
	}
	if (!Family.IsValid() || !Engraving.IsValid())
	{
		Result.Code = ESPScrollUseResultCode::InvalidDefinition;
		return Result;
	}
	if (AuthorityState.Scroll.BaseDefinitionId != Family.BaseDefinitionId)
	{
		Result.Code = ESPScrollUseResultCode::DefinitionMismatch;
		return Result;
	}
	if (AuthorityState.Scroll.EngravingDefinitionId
		!= Engraving.EngravingDefinitionId)
	{
		Result.Code = ESPScrollUseResultCode::EngravingMismatch;
		return Result;
	}

	const FVector NormalizedAim = Request.AimDirection.GetSafeNormal();
	const float ContaminationRatio =
		AuthorityState.Scroll.Contamination / 100.0f;
	Result.MalfunctionChance = FMath::Clamp(
		Family.BaseMalfunctionChance
			+ Family.ContaminationChanceAtMaximum * ContaminationRatio
			+ Engraving.MalfunctionChanceDelta,
		0.0f,
		1.0f);
	Result.MalfunctionRoll = static_cast<float>(
		static_cast<double>(BuildDeterministicHash(
			AuthorityState,
			TEXT("Roll")))
		/ 4294967296.0);

	FVector ResolvedDirection = NormalizedAim;
	const bool bMalfunction =
		Result.MalfunctionRoll < Result.MalfunctionChance;
	if (bMalfunction)
	{
		Result.Malfunction = ResolveMalfunctionType(
			AuthorityState.Scroll.Misfire);
		if (Result.Malfunction == ESPScrollMalfunctionOutcome::DirectionShift)
		{
			const bool bRotateClockwise =
				(BuildDeterministicHash(
					AuthorityState,
					TEXT("Direction")) & 1U) != 0U;
			const float Angle = Family.DirectionShiftDegrees
				* (bRotateClockwise ? 1.0f : -1.0f);
			ResolvedDirection = NormalizedAim
				.RotateAngleAxis(Angle, FVector::UpVector)
				.GetSafeNormal();
		}
	}

	if (Result.Malfunction != ESPScrollMalfunctionOutcome::Fizzle)
	{
		const float QualityMultiplier =
			SPGetQualityMultiplier(AuthorityState.Scroll.Quality);
		Result.Effects.Reserve(Family.BaseEffects.Num() + 2);
		for (const FSPScrollEffectSpec& Spec : Family.BaseEffects)
		{
			FSPResolvedScrollEffect Effect = ResolveEffect(
				Spec,
				QualityMultiplier,
				Engraving,
				ResolvedDirection);
			if (!FMath::IsFinite(Effect.Magnitude)
				|| !FMath::IsFinite(Effect.Radius)
				|| !FMath::IsFinite(Effect.DurationSeconds))
			{
				Result.Code = ESPScrollUseResultCode::InvalidDefinition;
				Result.Effects.Reset();
				return Result;
			}
			Result.Effects.Add(Effect);
		}
		AddNoiseEffect(
			Result.Effects,
			Engraving.AddedNoiseMagnitude,
			ResolvedDirection,
			false);
	}

	switch (Result.Malfunction)
	{
	case ESPScrollMalfunctionOutcome::Delay:
		Result.ApplicationDelaySeconds = Family.MalfunctionDelaySeconds;
		break;
	case ESPScrollMalfunctionOutcome::ExtraNoise:
		AddNoiseEffect(
			Result.Effects,
			Family.MalfunctionNoiseMagnitude,
			ResolvedDirection,
			true);
		break;
	default:
		break;
	}

	Result.Code = ESPScrollUseResultCode::Accepted;
	Result.bShouldConsumeScroll = true;
	return Result;
}

uint32 FSPScrollUseResolver::BuildDeterministicHash(
	const FSPAuthoritativeScrollUseState& AuthorityState,
	const TCHAR* Salt)
{
	const FString Canonical = FString::Printf(
		TEXT("%d|%s|%s|%s"),
		AuthorityState.RunSeed,
		*AuthorityState.Scroll.InstanceId.ToString(EGuidFormats::Digits),
		*AuthorityState.Scroll.BaseDefinitionId.ToString(),
		Salt);
	return FCrc::StrCrc32(*Canonical);
}
