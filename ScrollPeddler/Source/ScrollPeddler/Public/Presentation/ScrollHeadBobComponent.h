#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Presentation/ScrollHeadBobCameraShake.h"
#include "ScrollHeadBobComponent.generated.h"

class APlayerCameraManager;

/**
 * View-only head bob for the locally controlled character. Never runs for other players' copies.
 * Blends Idle -> Walk -> Sprint by ground speed relative to the movement component's walk and sprint speeds,
 * and goes flat in the air.
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
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FScrollHeadBobProfile GetTargetProfile() const;
	void StopShake();

	UPROPERTY(EditAnywhere, Category = "Head Bob")
	FScrollHeadBobProfile Idle;

	UPROPERTY(EditAnywhere, Category = "Head Bob")
	FScrollHeadBobProfile Walk;

	UPROPERTY(EditAnywhere, Category = "Head Bob")
	FScrollHeadBobProfile Sprint;

	/** How quickly the bob follows speed changes, per second. */
	UPROPERTY(EditAnywhere, Category = "Head Bob", meta = (ClampMin = "0"))
	float BlendSpeed = 6.f;

	/** At or below this ground speed the Idle profile is used. */
	UPROPERTY(EditAnywhere, Category = "Head Bob", meta = (ClampMin = "0", Units = "cm/s"))
	float StandingSpeed = 10.f;

	UPROPERTY(EditAnywhere, Category = "Head Bob", meta = (ClampMin = "0", ClampMax = "1"))
	float Intensity = 1.f;

	FScrollHeadBobProfile Current;
	TWeakObjectPtr<UCameraShakeBase> ActiveShake;
	TWeakObjectPtr<APlayerCameraManager> ShakeCameraManager;
};
