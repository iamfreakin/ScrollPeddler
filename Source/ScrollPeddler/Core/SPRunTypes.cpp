#include "Core/SPRunTypes.h"

namespace
{
constexpr double ExpeditionDurationSeconds = 25.0 * 60.0;
constexpr double CollapseDurationSeconds = 2.0 * 60.0;
}

double SPGetExpeditionDurationSeconds()
{
	return ExpeditionDurationSeconds;
}

double SPGetCollapseDurationSeconds()
{
	return CollapseDurationSeconds;
}

float SPGetBleedoutDurationSeconds(const int32 DownCount)
{
	if (DownCount <= 1)
	{
		return 45.0f;
	}

	if (DownCount == 2)
	{
		return 30.0f;
	}

	return 15.0f;
}

bool SPCanTransitionPlayerCondition(
	const ESPPlayerCondition Current,
	const ESPPlayerCondition Requested)
{
	if (Current == Requested)
	{
		return true;
	}

	switch (Current)
	{
	case ESPPlayerCondition::Normal:
		return Requested == ESPPlayerCondition::Injured
			|| Requested == ESPPlayerCondition::Down
			|| Requested == ESPPlayerCondition::Missing;
	case ESPPlayerCondition::Injured:
		return Requested == ESPPlayerCondition::Normal
			|| Requested == ESPPlayerCondition::Down
			|| Requested == ESPPlayerCondition::Missing;
	case ESPPlayerCondition::Down:
		return Requested == ESPPlayerCondition::Injured
			|| Requested == ESPPlayerCondition::Missing;
	case ESPPlayerCondition::Missing:
	default:
		return false;
	}
}

bool SPCanTransitionParticipationState(
	const ESPParticipationState Current,
	const ESPParticipationState Requested)
{
	if (Current == Requested)
	{
		return true;
	}

	switch (Current)
	{
	case ESPParticipationState::Hub:
		return Requested == ESPParticipationState::Active
			|| Requested == ESPParticipationState::Disconnected
			|| Requested == ESPParticipationState::Extracted
			|| Requested == ESPParticipationState::Spectating;
	case ESPParticipationState::Active:
		return Requested == ESPParticipationState::Disconnected
			|| Requested == ESPParticipationState::Extracted
			|| Requested == ESPParticipationState::Spectating;
	case ESPParticipationState::Disconnected:
		return Requested == ESPParticipationState::Active
			|| Requested == ESPParticipationState::Extracted
			|| Requested == ESPParticipationState::Spectating;
	case ESPParticipationState::Extracted:
		return Requested == ESPParticipationState::Spectating;
	case ESPParticipationState::Spectating:
	default:
		return false;
	}
}

bool SPCanAdvanceRunPhase(const ESPRunPhase Current, const ESPRunPhase Requested)
{
	return static_cast<uint8>(Requested) >= static_cast<uint8>(Current);
}

bool SPAllowsFieldGameplayAction(const ESPRunPhase Phase)
{
	return Phase == ESPRunPhase::Expedition
		|| Phase == ESPRunPhase::Collapse;
}

bool SPAllowsPlayerFieldGameplayAction(
	const ESPRunPhase Phase,
	const ESPParticipationState ParticipationState,
	const ESPPlayerCondition PlayerCondition)
{
	return SPAllowsFieldGameplayAction(Phase)
		&& ParticipationState == ESPParticipationState::Active
		&& PlayerCondition != ESPPlayerCondition::Missing;
}
