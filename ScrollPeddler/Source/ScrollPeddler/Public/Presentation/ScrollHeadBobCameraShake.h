#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "ScrollHeadBobCameraShake.generated.h"

/**
 * Infinite pattern that applies the view rotation offset UScrollHeadBobComponent computes each frame.
 * The component ticks before the camera updates, so the offset is never a frame behind.
 */
UCLASS()
class SCROLLPEDDLER_API UScrollHeadBobShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

public:
	FRotator Offset = FRotator::ZeroRotator;

private:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override { bStopped = false; }
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual bool IsFinishedImpl() const override { return bStopped; }
	virtual void StopShakePatternImpl(const FCameraShakePatternStopParams& Params) override { bStopped = true; }

	bool bStopped = false;
};

/** Camera shake that only carries UScrollHeadBobShakePattern. Started and fed by UScrollHeadBobComponent. */
UCLASS()
class SCROLLPEDDLER_API UScrollHeadBobCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UScrollHeadBobCameraShake(const FObjectInitializer& ObjectInitializer);
};
