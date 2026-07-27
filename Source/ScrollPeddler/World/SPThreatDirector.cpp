#include "World/SPThreatDirector.h"

#include "Game/SPGameState.h"
#include "GameplayTagContainer.h"
#include "Net/UnrealNetwork.h"
#include "World/SPNoiseSubsystem.h"

ASPThreatDirector::ASPThreatDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.0f;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
}

void ASPThreatDirector::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	FVector FocusLocation = FVector::ZeroVector;
	FSPThreatDirectorInputs Inputs;
	Inputs.CurrentServerTime = GetWorld()->GetTimeSeconds();
	Inputs.NoisePressure = CalculateNoisePressure(FocusLocation);
	Inputs.ActivePlayerCount = ResolveActivePlayerCount();
	DirectorState = SPAdvanceThreatDirectorState(
		DirectorState,
		DirectorConfig,
		Inputs);
	TryTriggerFearEvent(Inputs.CurrentServerTime, FocusLocation);
	TryRequestThreatSpawn(Inputs.CurrentServerTime);
	ForceNetUpdate();
}

void ASPThreatDirector::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASPThreatDirector, DirectorState);
	DOREPLIFETIME(ASPThreatDirector, LastFearEvent);
}

float ASPThreatDirector::CalculateNoisePressure(
	FVector& OutFocusLocation) const
{
	USPNoiseSubsystem* NoiseSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<USPNoiseSubsystem>()
		: nullptr;
	if (!NoiseSubsystem)
	{
		return 0.0f;
	}

	const TArray<FSPNoiseEvent> Events =
		NoiseSubsystem->GetRecentNoiseEvents(2.0);
	float LoudnessSum = 0.0f;
	float Loudest = -1.0f;
	for (const FSPNoiseEvent& Event : Events)
	{
		LoudnessSum += FMath::Max(0.0f, Event.Loudness);
		if (Event.Loudness > Loudest)
		{
			Loudest = Event.Loudness;
			OutFocusLocation = Event.Location;
		}
	}
	return FMath::Clamp(LoudnessSum / 3.0f, 0.0f, 1.0f);
}

int32 ASPThreatDirector::ResolveActivePlayerCount() const
{
	const ASPGameState* ScrollGameState = GetWorld()
		? GetWorld()->GetGameState<ASPGameState>()
		: nullptr;
	return ScrollGameState
		? FMath::Clamp(
			ScrollGameState->GetActiveRunPlayerCount(),
			0,
			4)
		: 0;
}

void ASPThreatDirector::TryTriggerFearEvent(
	const double ServerTime,
	const FVector& FocusLocation)
{
	if (!SPCanTriggerFearEvent(
		DirectorState,
		DirectorConfig,
		ServerTime))
	{
		return;
	}

	DirectorState = SPCommitFearEvent(
		DirectorState,
		DirectorConfig,
		ServerTime);
	++LastFearEvent.Sequence;
	LastFearEvent.Kind = static_cast<ESPFearEventKind>(
		(LastFearEvent.Sequence - 1) % 5);
	LastFearEvent.TriggeredAtServerTime = ServerTime;
	LastFearEvent.FocusLocation = FocusLocation;
	OnFearEventTriggered.Broadcast(LastFearEvent);

	const bool bMakesNoise =
		LastFearEvent.Kind == ESPFearEventKind::EquipmentAlarm
		|| LastFearEvent.Kind == ESPFearEventKind::ScrollSneeze;
	if (bMakesNoise)
	{
		if (USPNoiseSubsystem* NoiseSubsystem =
			GetWorld()->GetSubsystem<USPNoiseSubsystem>())
		{
			NoiseSubsystem->ReportNoise(
				FocusLocation,
				0.8f,
				1100.0f,
				FGameplayTag::RequestGameplayTag(
					TEXT("Noise.Threat.FearEvent"),
					false),
				this);
		}
	}
}

void ASPThreatDirector::TryRequestThreatSpawn(const double ServerTime)
{
	if (ServerTime < NextThreatSpawnRequestServerTime
		|| !SPCanSpendThreatBudget(DirectorState, ThreatSpawnCost))
	{
		return;
	}

	DirectorState = SPSpendThreatBudget(
		DirectorState,
		ThreatSpawnCost);
	const ESPThreatArchetype RequestedArchetype =
		(SpawnRequestSequence++ % 2) == 0
		? ESPThreatArchetype::EchoHunter
		: ESPThreatArchetype::PaperEater;
	NextThreatSpawnRequestServerTime =
		ServerTime + MinimumSpawnRequestIntervalSeconds;
	ThreatSpawnRequested.Broadcast(RequestedArchetype);
}

void ASPThreatDirector::OnRep_LastFearEvent()
{
	if (LastFearEvent.Sequence > 0)
	{
		OnFearEventTriggered.Broadcast(LastFearEvent);
	}
}
