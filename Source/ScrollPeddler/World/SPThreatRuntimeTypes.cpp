#include "World/SPThreatRuntimeTypes.h"

#include "GameFramework/Actor.h"

bool FSPThreatAttackIntent::IsValid() const
{
	return IntentId.IsValid()
		&& ::IsValid(ThreatActor.Get())
		&& ::IsValid(TargetActor.Get())
		&& (ResultCondition == ESPPlayerCondition::Injured
			|| ResultCondition == ESPPlayerCondition::Down)
		&& !ImpactLocation.ContainsNaN()
		&& FMath::IsFinite(ServerTimeSeconds)
		&& ServerTimeSeconds >= 0.0;
}

bool FSPPaperCorruptionIntent::IsValid() const
{
	return RequestId.IsValid()
		&& ::IsValid(ThreatActor.Get())
		&& ::IsValid(TargetActor.Get())
		&& FMath::IsFinite(ContaminationDelta)
		&& ContaminationDelta >= 0.0f
		&& (ContaminationDelta > 0.0f || bRequestItemDamage)
		&& FMath::IsFinite(ServerTimeSeconds)
		&& ServerTimeSeconds >= 0.0;
}

bool SPShouldInvestigateNoise(
	const FVector& ThreatLocation,
	const FVector& NoiseLocation,
	const float Loudness,
	const float NoiseRadius,
	const float MinimumLoudness,
	const float MaximumHearingRadius,
	const float HearingRadiusScale)
{
	if (ThreatLocation.ContainsNaN() || NoiseLocation.ContainsNaN()
		|| !FMath::IsFinite(Loudness)
		|| !FMath::IsFinite(NoiseRadius)
		|| !FMath::IsFinite(MinimumLoudness)
		|| !FMath::IsFinite(MaximumHearingRadius)
		|| !FMath::IsFinite(HearingRadiusScale)
		|| Loudness < MinimumLoudness
		|| Loudness <= 0.0f
		|| NoiseRadius <= 0.0f
		|| MinimumLoudness < 0.0f
		|| MaximumHearingRadius <= 0.0f
		|| HearingRadiusScale <= 0.0f)
	{
		return false;
	}

	const double EffectiveRadius = FMath::Min(
		static_cast<double>(MaximumHearingRadius),
		static_cast<double>(NoiseRadius)
			* static_cast<double>(HearingRadiusScale)
			* static_cast<double>(Loudness));
	return FVector::DistSquared(ThreatLocation, NoiseLocation)
		<= FMath::Square(EffectiveRadius);
}

double SPCalculatePaperTargetPriority(
	const ESPThreatItemTargetKind TargetKind,
	const double DistanceSquared)
{
	if (!FMath::IsFinite(DistanceSquared) || DistanceSquared < 0.0)
	{
		return -1.0;
	}

	const double KindPriority =
		TargetKind == ESPThreatItemTargetKind::Scroll ? 2.0 : 1.0;
	return KindPriority * 1.0e15 - DistanceSquared;
}
