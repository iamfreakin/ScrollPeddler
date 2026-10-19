#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ScrollHeadBobComponent.generated.h"

class APlayerCameraManager;
class UCameraShakeBase;
class UScrollFootstepComponent;

/** Peak view rotation per axis, in degrees. */
USTRUCT(BlueprintType)
struct FScrollHeadBobAmplitude
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Bob", meta = (ClampMin = "0", Units = "deg"))
	float Pitch = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Bob", meta = (ClampMin = "0", Units = "deg"))
	float Yaw = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Head Bob", meta = (ClampMin = "0", Units = "deg"))
	float Roll = 0.f;

	static FScrollHeadBobAmplitude Lerp(const FScrollHeadBobAmplitude& A, const FScrollHeadBobAmplitude& B, float Alpha);
	FScrollHeadBobAmplitude InterpTo(const FScrollHeadBobAmplitude& Target, float DeltaTime, float InterpSpeed) const;
	FScrollHeadBobAmplitude operator*(float Scale) const { return { Pitch * Scale, Yaw * Scale, Roll * Scale }; }
};

/**
 * View-only head bob for the locally controlled character. Never runs for other players' copies.
 * While walking it follows the footstep rhythm: the view dips once per step (pitch) and sways once per two steps (yaw, roll).
 * Standing still uses a separate, slow idle layer. Flat in the air.
 */
UCLASS(ClassGroup = (Scroll), meta = (BlueprintSpawnableComponent))
class SCROLLPEDDLER_API UScrollHeadBobComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UScrollHeadBobComponent();

	/** 0 turns head bob off, 1 is full strength. Meant for the "reduce/disable shake" accessibility setting. */
	UFUNCTION(BlueprintCallable, Category = "Head Bob")
	void SetIntensity(float NewIntensity);

	UFUNCTION(BlueprintPure, Category = "Head Bob")
	float GetIntensity() const { return Intensity; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void StopShake();

	/** Standing still. Off by default: the camera already rides the head bone's idle motion. */
	UPROPERTY(EditAnywhere, Category = "Head Bob")
	FScrollHeadBobAmplitude Idle;

	UPROPERTY(EditAnywhere, Category = "Head Bob", meta = (ClampMin = "0", Units = "Hz"))
	float IdleFrequency = 0.25f;

	/** At walk speed. The rhythm comes from the footsteps. */
	UPROPERTY(EditAnywhere, Category = "Head Bob")
	FScrollHeadBobAmplitude Walk;

	/** At sprint speed. */
	UPROPERTY(EditAnywhere, Category = "Head Bob")
	FScrollHeadBobAmplitude Sprint;

	/** How quickly the bob strength follows speed changes, per second. */
	UPROPERTY(EditAnywhere, Category = "Head Bob", meta = (ClampMin = "0"))
	float BlendSpeed = 6.f;

	/** At or below this ground speed the character counts as standing still. */
	UPROPERTY(EditAnywhere, Category = "Head Bob", meta = (ClampMin = "0", Units = "cm/s"))
	float StandingSpeed = 10.f;

	UPROPERTY(EditAnywhere, Category = "Head Bob", meta = (ClampMin = "0", ClampMax = "1"))
	float Intensity = 1.f;

	UPROPERTY(Transient)
	TObjectPtr<UScrollFootstepComponent> Footsteps;

	FScrollHeadBobAmplitude CurrentIdle;
	FScrollHeadBobAmplitude CurrentStep;
	float IdlePhase = 0.f;
	TWeakObjectPtr<UCameraShakeBase> ActiveShake;
	TWeakObjectPtr<APlayerCameraManager> ShakeCameraManager;
};
