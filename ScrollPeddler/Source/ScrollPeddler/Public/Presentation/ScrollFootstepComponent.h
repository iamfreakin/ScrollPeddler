#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Sound/SoundAttenuation.h"
#include "ScrollFootstepComponent.generated.h"

class ACharacter;
class USoundBase;

/** Sounds for one surface. One is picked at random per step, never the same one twice in a row. */
USTRUCT(BlueprintType)
struct FScrollFootstepSounds
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footsteps")
	TArray<TObjectPtr<USoundBase>> Walk;

	/** Falls back to Walk when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footsteps")
	TArray<TObjectPtr<USoundBase>> Run;

	/** Pitch for this surface. Also lets one placeholder set sound different per surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footsteps", meta = (ClampMin = "0.1", ClampMax = "4"))
	float Pitch = 1.f;

	bool IsEmpty() const { return Walk.IsEmpty() && Run.IsEmpty(); }
};

/**
 * Footsteps for any character, heard in 3D by everyone nearby.
 * Every machine (except a dedicated server) counts steps from the character's replicated movement,
 * so footsteps need no network messages and each step plays exactly once per machine.
 * Steps come from distance travelled; GetStepCycles() also drives the head bob rhythm.
 */
UCLASS(ClassGroup = (Scroll), meta = (BlueprintSpawnableComponent))
class SCROLLPEDDLER_API UScrollFootstepComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UScrollFootstepComponent();

	/** Steps taken so far as a continuous count; each whole number crossed is one footfall. */
	float GetStepCycles() const { return StepCycles; }

	/** 0 at walk speed, 1 at sprint speed (can go outside 0..1). */
	static float GetRunAlpha(const ACharacter& Character);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UFUNCTION()
	void HandleMovementModeChanged(ACharacter* Character, EMovementMode PrevMovementMode, uint8 PreviousCustomMode);

	void PlayFootstep(bool bRun);
	const FScrollFootstepSounds& GetSoundsFor(EPhysicalSurface Surface) const;

	/** Per physical surface: SurfaceType1 Wood, SurfaceType2 Stone, SurfaceType3 Water, SurfaceType4 Paper. */
	UPROPERTY(EditAnywhere, Category = "Footsteps")
	TMap<TEnumAsByte<EPhysicalSurface>, FScrollFootstepSounds> SurfaceSounds;

	/** For surfaces without an entry above, and ground without a physical material. */
	UPROPERTY(EditAnywhere, Category = "Footsteps")
	FScrollFootstepSounds DefaultSounds;

	/** Distance per step at walk speed. 150 cm at 300 cm/s is 2 steps per second. */
	UPROPERTY(EditAnywhere, Category = "Footsteps|Rhythm", meta = (ClampMin = "1", Units = "cm"))
	float WalkStepLength = 150.f;

	/** Distance per step at sprint speed. 192 cm at 500 cm/s is about 2.6 steps per second. */
	UPROPERTY(EditAnywhere, Category = "Footsteps|Rhythm", meta = (ClampMin = "1", Units = "cm"))
	float SprintStepLength = 192.f;

	/** Where between walk (0) and sprint (1) speed the Run sounds take over. */
	UPROPERTY(EditAnywhere, Category = "Footsteps|Rhythm", meta = (ClampMin = "0", ClampMax = "1"))
	float RunThreshold = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Footsteps|Playback", meta = (ClampMin = "0"))
	float WalkVolume = 0.6f;

	UPROPERTY(EditAnywhere, Category = "Footsteps|Playback", meta = (ClampMin = "0"))
	float RunVolume = 1.f;

	/** Random pitch spread per step, e.g. 0.05 = up to 5% higher or lower. */
	UPROPERTY(EditAnywhere, Category = "Footsteps|Playback", meta = (ClampMin = "0", ClampMax = "0.5"))
	float PitchVariation = 0.05f;

	/** How far below the capsule bottom to look for ground. */
	UPROPERTY(EditAnywhere, Category = "Footsteps|Playback", meta = (ClampMin = "0", Units = "cm"))
	float GroundTraceExtra = 50.f;

	/** 3D falloff: full volume within the inner radius, fading out over the falloff distance. */
	UPROPERTY(EditAnywhere, Category = "Footsteps|Playback")
	FSoundAttenuationSettings Attenuation;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> RuntimeAttenuation;

	float StepCycles = 0.f;
	FVector LastLocation = FVector::ZeroVector;
	bool bHasLastLocation = false;
	int32 LastWalkIndex = INDEX_NONE;
	int32 LastRunIndex = INDEX_NONE;
};
