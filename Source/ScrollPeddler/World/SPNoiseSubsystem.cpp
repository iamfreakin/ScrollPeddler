#include "World/SPNoiseSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogSPNoiseSubsystem, Log, All);

bool FSPNoiseEvent::IsPayloadValid() const
{
	return !Location.ContainsNaN()
		&& FMath::IsFinite(Loudness)
		&& FMath::IsFinite(Radius)
		&& Loudness > 0.0f
		&& Radius > 0.0f;
}

bool FSPNoiseEvent::IsValid() const
{
	return IsPayloadValid()
		&& FMath::IsFinite(ServerTimeSeconds)
		&& ServerTimeSeconds >= 0.0;
}

bool FSPNoiseEvent::IsExpired(
	const double CurrentServerTime,
	const double LifetimeSeconds) const
{
	if (!IsValid()
		|| !FMath::IsFinite(CurrentServerTime)
		|| !FMath::IsFinite(LifetimeSeconds)
		|| LifetimeSeconds <= 0.0)
	{
		return true;
	}

	return CurrentServerTime >= ServerTimeSeconds
		&& CurrentServerTime - ServerTimeSeconds >= LifetimeSeconds;
}

bool USPNoiseSubsystem::PublishNoiseEvent(const FSPNoiseEvent& NoiseEvent)
{
	UWorld* World = GetWorld();
	if (!IsAuthorityWorld() || !NoiseEvent.IsPayloadValid()
		|| (NoiseEvent.SourceActor.IsValid()
			&& NoiseEvent.SourceActor->GetWorld() != World))
	{
		UE_LOG(LogSPNoiseSubsystem, Warning,
			TEXT("SP_NOISE_REJECTED authority=%d payload_valid=%d source=%s"),
			IsAuthorityWorld() ? 1 : 0,
			NoiseEvent.IsPayloadValid() ? 1 : 0,
			*GetNameSafe(NoiseEvent.SourceActor.Get()));
		return false;
	}

	const double CurrentServerTime = ResolveServerTime();
	if (!FMath::IsFinite(CurrentServerTime) || CurrentServerTime < 0.0)
	{
		return false;
	}

	PruneExpiredEvents(CurrentServerTime);

	FSPNoiseEvent PublishedEvent = NoiseEvent;
	PublishedEvent.ServerTimeSeconds = CurrentServerTime;
	RecentEvents.Add(PublishedEvent);
	if (RecentEvents.Num() > MaxRecentEventCount)
	{
		RecentEvents.RemoveAt(
			0,
			RecentEvents.Num() - MaxRecentEventCount,
			EAllowShrinking::No);
	}

	NoisePublishedNative.Broadcast(PublishedEvent);
	OnNoisePublished.Broadcast(PublishedEvent);

	UE_LOG(LogSPNoiseSubsystem, Verbose,
		TEXT("SP_NOISE_PUBLISHED location=%s loudness=%.3f radius=%.1f tag=%s source=%s recent=%d"),
		*PublishedEvent.Location.ToCompactString(),
		PublishedEvent.Loudness,
		PublishedEvent.Radius,
		*PublishedEvent.NoiseTag.ToString(),
		*GetNameSafe(PublishedEvent.SourceActor.Get()),
		RecentEvents.Num());
	return true;
}

bool USPNoiseSubsystem::ReportNoise(
	const FVector Location,
	const float Loudness,
	const float Radius,
	const FGameplayTag NoiseTag,
	AActor* SourceActor)
{
	FSPNoiseEvent NoiseEvent;
	NoiseEvent.Location = Location;
	NoiseEvent.Loudness = Loudness;
	NoiseEvent.Radius = Radius;
	NoiseEvent.NoiseTag = NoiseTag;
	NoiseEvent.SourceActor = SourceActor;
	return PublishNoiseEvent(NoiseEvent);
}

TArray<FSPNoiseEvent> USPNoiseSubsystem::GetRecentNoiseEvents(
	const double MaxAgeSeconds)
{
	const double CurrentServerTime = ResolveServerTime();
	PruneExpiredEvents(CurrentServerTime);

	if (MaxAgeSeconds < 0.0
		|| MaxAgeSeconds >= RecentEventLifetimeSeconds)
	{
		return RecentEvents;
	}

	TArray<FSPNoiseEvent> Result;
	if (!FMath::IsFinite(MaxAgeSeconds))
	{
		return Result;
	}

	Result.Reserve(RecentEvents.Num());
	for (const FSPNoiseEvent& NoiseEvent : RecentEvents)
	{
		if (!NoiseEvent.IsExpired(CurrentServerTime, MaxAgeSeconds))
		{
			Result.Add(NoiseEvent);
		}
	}
	return Result;
}

int32 USPNoiseSubsystem::GetRecentNoiseEventCount()
{
	PruneExpiredEvents();
	return RecentEvents.Num();
}

void USPNoiseSubsystem::PruneExpiredEvents(const double CurrentServerTime)
{
	const double ServerTime = ResolveServerTime(CurrentServerTime);
	RecentEvents.RemoveAll(
		[ServerTime](const FSPNoiseEvent& NoiseEvent)
		{
			return NoiseEvent.IsExpired(ServerTime, RecentEventLifetimeSeconds);
		});
}

bool USPNoiseSubsystem::IsAuthorityWorld() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

double USPNoiseSubsystem::ResolveServerTime(
	const double RequestedServerTime) const
{
	if (RequestedServerTime >= 0.0)
	{
		return RequestedServerTime;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}

	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}

	return static_cast<double>(World->GetTimeSeconds());
}
