#include "Core/SPThreatDirectorTypes.h"

bool FSPThreatDirectorConfig::IsValid() const
{
	return FMath::IsFinite(BaseBudgetGainPerSecond)
		&& BaseBudgetGainPerSecond >= 0.0f
		&& FMath::IsFinite(NoiseBudgetGainPerSecond)
		&& NoiseBudgetGainPerSecond >= 0.0f
		&& FMath::IsFinite(AdditionalPlayerMultiplier)
		&& AdditionalPlayerMultiplier >= 0.0f
		&& FMath::IsFinite(MaximumBudget)
		&& MaximumBudget >= 0.0f
		&& FMath::IsFinite(FearEventCost)
		&& FearEventCost >= 0.0f
		&& FearEventCost <= MaximumBudget
		&& FMath::IsFinite(FearEventCooldownSeconds)
		&& FearEventCooldownSeconds >= 0.0f;
}

bool FSPThreatDirectorState::IsValid() const
{
	return FMath::IsFinite(AvailableBudget)
		&& AvailableBudget >= 0.0f
		&& FMath::IsFinite(LastServerTime)
		&& LastServerTime >= 0.0
		&& FMath::IsFinite(NextFearEventAllowedServerTime)
		&& NextFearEventAllowedServerTime >= 0.0;
}

bool FSPThreatDirectorInputs::IsValid() const
{
	return FMath::IsFinite(CurrentServerTime)
		&& CurrentServerTime >= 0.0
		&& FMath::IsFinite(NoisePressure)
		&& NoisePressure >= 0.0f
		&& ActivePlayerCount >= 0
		&& ActivePlayerCount <= 4;
}

FSPThreatDirectorState SPAdvanceThreatDirectorState(
	const FSPThreatDirectorState& CurrentState,
	const FSPThreatDirectorConfig& Config,
	const FSPThreatDirectorInputs& Inputs)
{
	if (!CurrentState.IsValid() || !Config.IsValid() || !Inputs.IsValid()
		|| Inputs.CurrentServerTime < CurrentState.LastServerTime)
	{
		return CurrentState;
	}

	FSPThreatDirectorState Result = CurrentState;
	const double DeltaSeconds =
		Inputs.CurrentServerTime - CurrentState.LastServerTime;
	const float PlayerMultiplier = Inputs.ActivePlayerCount > 0
		? 1.0f
			+ (Inputs.ActivePlayerCount - 1)
				* Config.AdditionalPlayerMultiplier
		: 0.0f;
	const float NoisePressure =
		FMath::Clamp(Inputs.NoisePressure, 0.0f, 1.0f);
	const double GainPerSecond =
		(Config.BaseBudgetGainPerSecond
			+ NoisePressure * Config.NoiseBudgetGainPerSecond)
		* PlayerMultiplier;

	Result.AvailableBudget = FMath::Clamp(
		static_cast<float>(
			static_cast<double>(CurrentState.AvailableBudget)
			+ DeltaSeconds * GainPerSecond),
		0.0f,
		Config.MaximumBudget);
	Result.LastServerTime = Inputs.CurrentServerTime;
	return Result;
}

bool SPCanSpendThreatBudget(
	const FSPThreatDirectorState& State,
	const float Cost)
{
	return State.IsValid()
		&& FMath::IsFinite(Cost)
		&& Cost >= 0.0f
		&& State.AvailableBudget >= Cost;
}

FSPThreatDirectorState SPSpendThreatBudget(
	const FSPThreatDirectorState& State,
	const float Cost)
{
	if (!SPCanSpendThreatBudget(State, Cost))
	{
		return State;
	}

	FSPThreatDirectorState Result = State;
	Result.AvailableBudget =
		FMath::Max(0.0f, Result.AvailableBudget - Cost);
	return Result;
}

bool SPCanTriggerFearEvent(
	const FSPThreatDirectorState& State,
	const FSPThreatDirectorConfig& Config,
	const double CurrentServerTime)
{
	return State.IsValid()
		&& Config.IsValid()
		&& FMath::IsFinite(CurrentServerTime)
		&& CurrentServerTime >= State.LastServerTime
		&& CurrentServerTime >= State.NextFearEventAllowedServerTime
		&& SPCanSpendThreatBudget(State, Config.FearEventCost);
}

FSPThreatDirectorState SPCommitFearEvent(
	const FSPThreatDirectorState& State,
	const FSPThreatDirectorConfig& Config,
	const double CurrentServerTime)
{
	if (!SPCanTriggerFearEvent(State, Config, CurrentServerTime))
	{
		return State;
	}

	FSPThreatDirectorState Result =
		SPSpendThreatBudget(State, Config.FearEventCost);
	Result.NextFearEventAllowedServerTime =
		CurrentServerTime + Config.FearEventCooldownSeconds;
	return Result;
}
