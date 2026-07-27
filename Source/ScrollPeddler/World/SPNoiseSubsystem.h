#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "SPNoiseSubsystem.generated.h"

class AActor;

/** One authority-stamped gameplay sound that AI or directors may consume. */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPNoiseEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Peddler|Noise")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Peddler|Noise", meta = (ClampMin = "0.0"))
	float Loudness = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Peddler|Noise", meta = (ClampMin = "0.0"))
	float Radius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Peddler|Noise")
	FGameplayTag NoiseTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Peddler|Noise")
	TWeakObjectPtr<AActor> SourceActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scroll Peddler|Noise")
	double ServerTimeSeconds = 0.0;

	/** Caller-controlled fields are finite and describe an audible event. */
	bool IsPayloadValid() const;

	/** Payload plus the authority timestamp are valid. */
	bool IsValid() const;

	bool IsExpired(double CurrentServerTime, double LifetimeSeconds) const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FSPNoisePublishedSignature,
	const FSPNoiseEvent&,
	NoiseEvent);

DECLARE_MULTICAST_DELEGATE_OneParam(
	FSPNoisePublishedNativeSignature,
	const FSPNoiseEvent&);

/**
 * Server-side gameplay-noise bus. Recent events are intentionally short-lived
 * and bounded; this is not a replicated event ledger.
 */
UCLASS()
class SCROLLPEDDLER_API USPNoiseSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxRecentEventCount = 64;
	static constexpr double RecentEventLifetimeSeconds = 5.0;

	/** Validates and authority-stamps a caller-provided payload. */
	bool PublishNoiseEvent(const FSPNoiseEvent& NoiseEvent);

	/** Convenience entry point for C++ and Blueprint gameplay emitters. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scroll Peddler|Noise")
	bool ReportNoise(
		FVector Location,
		float Loudness,
		float Radius,
		FGameplayTag NoiseTag,
		AActor* SourceActor);

	/** Returns non-expired events, optionally filtered to a shorter maximum age. */
	TArray<FSPNoiseEvent> GetRecentNoiseEvents(double MaxAgeSeconds = -1.0);

	int32 GetRecentNoiseEventCount();

	void PruneExpiredEvents(double CurrentServerTime = -1.0);

	bool IsAuthorityWorld() const;

	FSPNoisePublishedNativeSignature& OnNoisePublishedNative()
	{
		return NoisePublishedNative;
	}

	UPROPERTY(BlueprintAssignable, Category = "Scroll Peddler|Noise")
	FSPNoisePublishedSignature OnNoisePublished;

private:
	double ResolveServerTime(double RequestedServerTime = -1.0) const;

	TArray<FSPNoiseEvent> RecentEvents;
	FSPNoisePublishedNativeSignature NoisePublishedNative;
};
