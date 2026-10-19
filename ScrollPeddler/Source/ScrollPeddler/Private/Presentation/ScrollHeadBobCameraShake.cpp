#include "Presentation/ScrollHeadBobCameraShake.h"

FScrollHeadBobProfile FScrollHeadBobProfile::Lerp(const FScrollHeadBobProfile& A, const FScrollHeadBobProfile& B, float Alpha)
{
	FScrollHeadBobProfile Result;
	Result.PitchAmplitude = FMath::Lerp(A.PitchAmplitude, B.PitchAmplitude, Alpha);
	Result.PitchFrequency = FMath::Lerp(A.PitchFrequency, B.PitchFrequency, Alpha);
	Result.YawAmplitude = FMath::Lerp(A.YawAmplitude, B.YawAmplitude, Alpha);
	Result.YawFrequency = FMath::Lerp(A.YawFrequency, B.YawFrequency, Alpha);
	Result.RollAmplitude = FMath::Lerp(A.RollAmplitude, B.RollAmplitude, Alpha);
	Result.RollFrequency = FMath::Lerp(A.RollFrequency, B.RollFrequency, Alpha);
	return Result;
}

FScrollHeadBobProfile FScrollHeadBobProfile::InterpTo(const FScrollHeadBobProfile& Target, float DeltaTime, float InterpSpeed) const
{
	FScrollHeadBobProfile Result;
	Result.PitchAmplitude = FMath::FInterpTo(PitchAmplitude, Target.PitchAmplitude, DeltaTime, InterpSpeed);
	Result.PitchFrequency = FMath::FInterpTo(PitchFrequency, Target.PitchFrequency, DeltaTime, InterpSpeed);
	Result.YawAmplitude = FMath::FInterpTo(YawAmplitude, Target.YawAmplitude, DeltaTime, InterpSpeed);
	Result.YawFrequency = FMath::FInterpTo(YawFrequency, Target.YawFrequency, DeltaTime, InterpSpeed);
	Result.RollAmplitude = FMath::FInterpTo(RollAmplitude, Target.RollAmplitude, DeltaTime, InterpSpeed);
	Result.RollFrequency = FMath::FInterpTo(RollFrequency, Target.RollFrequency, DeltaTime, InterpSpeed);
	return Result;
}

FScrollHeadBobProfile FScrollHeadBobProfile::WithAmplitudeScale(float Scale) const
{
	FScrollHeadBobProfile Result = *this;
	Result.PitchAmplitude *= Scale;
	Result.YawAmplitude *= Scale;
	Result.RollAmplitude *= Scale;
	return Result;
}

void UScrollHeadBobShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	OutInfo.Duration = FCameraShakeDuration::Infinite();
}

void UScrollHeadBobShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams& Params)
{
	PitchPhase = 0.f;
	YawPhase = 0.f;
	RollPhase = 0.f;
	bStopped = false;
}

void UScrollHeadBobShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	auto Advance = [Step = UE_TWO_PI * Params.DeltaTime](float& Phase, float Frequency)
	{
		Phase = FMath::Fmod(Phase + Step * Frequency, UE_TWO_PI);
		return FMath::Sin(Phase);
	};

	OutResult.Rotation = FRotator(
		Profile.PitchAmplitude * Advance(PitchPhase, Profile.PitchFrequency),
		Profile.YawAmplitude * Advance(YawPhase, Profile.YawFrequency),
		Profile.RollAmplitude * Advance(RollPhase, Profile.RollFrequency));
}

UScrollHeadBobCameraShake::UScrollHeadBobCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSingleInstance = true;
	SetRootShakePattern(CreateDefaultSubobject<UScrollHeadBobShakePattern>(TEXT("HeadBobPattern")));
}
