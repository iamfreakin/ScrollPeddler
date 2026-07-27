#pragma once

#include "CoreMinimal.h"
#include "Core/SPRunTypes.h"
#include "SPThreatRuntimeTypes.generated.h"

class AActor;

UENUM(BlueprintType)
enum class ESPThreatBehaviorState : uint8
{
	Idle,
	Investigating,
	Pursuing,
	Telegraphing,
	Recovering,
	Staggered,
	Retreating
};

UENUM(BlueprintType)
enum class ESPThreatItemTargetKind : uint8
{
	WorldItem,
	Scroll
};

/** Fixed server attack outcome; inventory drop is an explicit separate intent. */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPThreatAttackIntent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGuid IntentId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<AActor> ThreatActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<AActor> TargetActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPPlayerCondition ResultCondition = ESPPlayerCondition::Injured;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bRequestHandItemDrop = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector ImpactLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	double ServerTimeSeconds = 0.0;

	bool IsValid() const;
};

/** Paper Eater asks the authoritative item owner to apply this mutation. */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPPaperCorruptionIntent
{
	GENERATED_BODY()

	/** Stable replay key for authoritative idempotency at the item owner. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGuid RequestId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<AActor> ThreatActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<AActor> TargetActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPThreatItemTargetKind TargetKind = ESPThreatItemTargetKind::WorldItem;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float ContaminationDelta = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bRequestItemDamage = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	double ServerTimeSeconds = 0.0;

	bool IsValid() const;
};

/** Pure Echo Hunter hearing policy used by runtime and automation tests. */
SCROLLPEDDLER_API bool SPShouldInvestigateNoise(
	const FVector& ThreatLocation,
	const FVector& NoiseLocation,
	float Loudness,
	float NoiseRadius,
	float MinimumLoudness,
	float MaximumHearingRadius,
	float HearingRadiusScale);

/** Higher values are preferred; every Scroll outranks every ordinary item. */
SCROLLPEDDLER_API double SPCalculatePaperTargetPriority(
	ESPThreatItemTargetKind TargetKind,
	double DistanceSquared);
