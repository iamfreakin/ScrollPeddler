#pragma once

#include "CoreMinimal.h"
#include "Core/SPThreatDirectorTypes.h"
#include "Data/SPVerticalSliceDefinitions.h"
#include "GameFramework/Info.h"
#include "SPThreatDirector.generated.h"

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPReplicatedFearEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 Sequence = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPFearEventKind Kind = ESPFearEventKind::Whispers;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	double TriggeredAtServerTime = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector FocusLocation = FVector::ZeroVector;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FSPFearEventTriggeredSignature,
	const FSPReplicatedFearEvent&,
	Event);

DECLARE_MULTICAST_DELEGATE_OneParam(
	FSPThreatSpawnRequestedSignature,
	ESPThreatArchetype);

/** Authority-only pacing actor backed by the deterministic director state. */
UCLASS()
class SCROLLPEDDLER_API ASPThreatDirector : public AInfo
{
	GENERATED_BODY()

public:
	ASPThreatDirector();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Threat")
	const FSPThreatDirectorState& GetDirectorState() const
	{
		return DirectorState;
	}

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Threat")
	const FSPReplicatedFearEvent& GetLastFearEvent() const
	{
		return LastFearEvent;
	}

	FSPThreatSpawnRequestedSignature& OnThreatSpawnRequested()
	{
		return ThreatSpawnRequested;
	}

	UPROPERTY(BlueprintAssignable, Category = "Scroll Peddler|Threat")
	FSPFearEventTriggeredSignature OnFearEventTriggered;

private:
	UFUNCTION()
	void OnRep_LastFearEvent();

	float CalculateNoisePressure(FVector& OutFocusLocation) const;
	int32 ResolveActivePlayerCount() const;
	void TryTriggerFearEvent(double ServerTime, const FVector& FocusLocation);
	void TryRequestThreatSpawn(double ServerTime);

	UPROPERTY(EditDefaultsOnly, Category = "Threat")
	FSPThreatDirectorConfig DirectorConfig;

	UPROPERTY(Replicated)
	FSPThreatDirectorState DirectorState;

	UPROPERTY(ReplicatedUsing = OnRep_LastFearEvent)
	FSPReplicatedFearEvent LastFearEvent;

	FSPThreatSpawnRequestedSignature ThreatSpawnRequested;
	double NextThreatSpawnRequestServerTime = 0.0;
	int32 SpawnRequestSequence = 0;

	static constexpr float ThreatSpawnCost = 20.0f;
	static constexpr double MinimumSpawnRequestIntervalSeconds = 30.0;
};
