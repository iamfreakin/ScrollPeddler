#pragma once

#include "CoreMinimal.h"
#include "SPRunTypes.generated.h"

/** Authoritative lifecycle of one field run. */
UENUM(BlueprintType)
enum class ESPRunPhase : uint8
{
	Preparing,
	Expedition,
	Collapse,
	Resolution,
	Settlement
};

/** Physical condition. Participation is tracked separately. */
UENUM(BlueprintType)
enum class ESPPlayerCondition : uint8
{
	Normal,
	Injured,
	Down,
	Missing
};

/** Where a player currently participates in the host's run. */
UENUM(BlueprintType)
enum class ESPParticipationState : uint8
{
	Hub,
	Active,
	Disconnected,
	Extracted,
	Spectating
};

/** Fixed expedition window before the collapse grace period begins. */
SCROLLPEDDLER_API double SPGetExpeditionDurationSeconds();

/** Fixed grace period between collapse and forced run resolution. */
SCROLLPEDDLER_API double SPGetCollapseDurationSeconds();

/** First Down is 45 seconds, second is 30 seconds, subsequent Downs are 15 seconds. */
SCROLLPEDDLER_API float SPGetBleedoutDurationSeconds(int32 DownCount);

/** Returns whether a condition transition is legal. Repeating a state is idempotent. */
SCROLLPEDDLER_API bool SPCanTransitionPlayerCondition(
	ESPPlayerCondition Current,
	ESPPlayerCondition Requested);

/** Returns whether a participation transition is legal. Repeating a state is idempotent. */
SCROLLPEDDLER_API bool SPCanTransitionParticipationState(
	ESPParticipationState Current,
	ESPParticipationState Requested);

/** Run phases are monotonic; abort paths may skip forward but never rewind. */
SCROLLPEDDLER_API bool SPCanAdvanceRunPhase(ESPRunPhase Current, ESPRunPhase Requested);
