#pragma once

#include "CoreMinimal.h"
#include "SPThreatDirectorTypes.generated.h"

/** Tunable, deterministic budget and fear-event pacing policy. */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPThreatDirectorConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float BaseBudgetGainPerSecond = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float NoiseBudgetGainPerSecond = 0.40f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float AdditionalPlayerMultiplier = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float MaximumBudget = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float FearEventCost = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float FearEventCooldownSeconds = 30.0f;

	bool IsValid() const;
};

/** Serializable/pure director state. No world or timer ownership is hidden here. */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPThreatDirectorState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float AvailableBudget = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	double LastServerTime = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	double NextFearEventAllowedServerTime = 0.0;

	bool IsValid() const;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPThreatDirectorInputs
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	double CurrentServerTime = 0.0;

	/** Recent normalized gameplay-noise pressure. Values are clamped to [0, 1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NoisePressure = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", ClampMax = "4"))
	int32 ActivePlayerCount = 1;

	bool IsValid() const;
};

/** Pure time/noise/player-count budget advance. Backward time is ignored. */
SCROLLPEDDLER_API FSPThreatDirectorState SPAdvanceThreatDirectorState(
	const FSPThreatDirectorState& CurrentState,
	const FSPThreatDirectorConfig& Config,
	const FSPThreatDirectorInputs& Inputs);

SCROLLPEDDLER_API bool SPCanSpendThreatBudget(
	const FSPThreatDirectorState& State,
	float Cost);

/** Pure spend transform; returns the input unchanged when the cost is invalid. */
SCROLLPEDDLER_API FSPThreatDirectorState SPSpendThreatBudget(
	const FSPThreatDirectorState& State,
	float Cost);

SCROLLPEDDLER_API bool SPCanTriggerFearEvent(
	const FSPThreatDirectorState& State,
	const FSPThreatDirectorConfig& Config,
	double CurrentServerTime);

/** Pure fear-event commit: spends cost and advances cooldown when allowed. */
SCROLLPEDDLER_API FSPThreatDirectorState SPCommitFearEvent(
	const FSPThreatDirectorState& State,
	const FSPThreatDirectorConfig& Config,
	double CurrentServerTime);
