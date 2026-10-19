#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "ScrollHeadBobCameraShake.generated.h"

/** One head bob setting: a sine wave per rotation axis. */
USTRUCT(BlueprintType)
struct FScrollHeadBobProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Bob", meta = (ClampMin = "0", Units = "deg"))
	float PitchAmplitude = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Bob", meta = (ClampMin = "0", Units = "Hz"))
	float PitchFrequency = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Bob", meta = (ClampMin = "0", Units = "deg"))
	float YawAmplitude = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Bob", meta = (ClampMin = "0", Units = "Hz"))
	float YawFrequency = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Bob", meta = (ClampMin = "0", Units = "deg"))
	float RollAmplitude = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Bob", meta = (ClampMin = "0", Units = "Hz"))
	float RollFrequency = 0.f;

	static FScrollHeadBobProfile Lerp(const FScrollHeadBobProfile& A, const FScrollHeadBobProfile& B, float Alpha);

	/** Moves every value toward Target at InterpSpeed (per second). */
	FScrollHeadBobProfile InterpTo(const FScrollHeadBobProfile& Target, float DeltaTime, float InterpSpeed) const;

	/** Same frequencies, amplitudes multiplied by Scale. */
	FScrollHeadBobProfile WithAmplitudeScale(float Scale) const;
};

/**
 * Infinite head bob pattern. UScrollHeadBobComponent writes Profile every frame.
 * Phases accumulate per axis, so frequency changes (walk to sprint) never make the view jump.
 */
UCLASS()
class SCROLLPEDDLER_API UScrollHeadBobShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

public:
	FScrollHeadBobProfile Profile;

private:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual bool IsFinishedImpl() const override { return bStopped; }
	virtual void StopShakePatternImpl(const FCameraShakePatternStopParams& Params) override { bStopped = true; }

	float PitchPhase = 0.f;
	float YawPhase = 0.f;
	float RollPhase = 0.f;
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
