#include "Presentation/ScrollHeadBobCameraShake.h"

void UScrollHeadBobShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	OutInfo.Duration = FCameraShakeDuration::Infinite();
}

void UScrollHeadBobShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	OutResult.Rotation = Offset;
}

UScrollHeadBobCameraShake::UScrollHeadBobCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSingleInstance = true;
	SetRootShakePattern(CreateDefaultSubobject<UScrollHeadBobShakePattern>(TEXT("HeadBobPattern")));
}
