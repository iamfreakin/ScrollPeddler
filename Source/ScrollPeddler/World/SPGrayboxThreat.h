#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/SPThreatRuntimeTypes.h"
#include "SPGrayboxThreat.generated.h"

class APawn;
class ASPPlayerState;
class UPrimitiveComponent;
class USPGrayboxThreatDefinition;
class USPNoiseSubsystem;
class UStaticMeshComponent;
struct FSPNoiseEvent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FSPThreatAttackIntentSignature,
	const FSPThreatAttackIntent&,
	Intent);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FSPPaperCorruptionIntentSignature,
	const FSPPaperCorruptionIntent&,
	Intent);

/**
 * Server-driven graybox threat. Movement, perception, player condition, and
 * item-mutation requests are authoritative; clients receive presentation state.
 */
UCLASS()
class SCROLLPEDDLER_API ASPGrayboxThreat : public AActor
{
	GENERATED_BODY()

public:
	ASPGrayboxThreat();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scroll Peddler|Threat")
	bool AuthoritySetThreatDefinition(USPGrayboxThreatDefinition* InDefinition);

	/** No death state exists: disruptive effects enter Staggered then Retreating. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scroll Peddler|Threat")
	bool AuthorityStagger(FVector EffectOrigin);

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Threat")
	const USPGrayboxThreatDefinition* GetThreatDefinition() const
	{
		return ThreatDefinition;
	}

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Threat")
	ESPThreatBehaviorState GetBehaviorState() const { return BehaviorState; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Threat")
	FVector GetInvestigationLocation() const { return InvestigationLocation; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Threat")
	double GetTelegraphEndServerTime() const { return TelegraphEndServerTime; }

	bool CanInvestigateNoise(const FSPNoiseEvent& NoiseEvent) const;

	UPROPERTY(BlueprintAssignable, Category = "Scroll Peddler|Threat")
	FSPThreatAttackIntentSignature OnAttackIntent;

	/** Observation hook; the target interface remains mutation authority. */
	UPROPERTY(BlueprintAssignable, Category = "Scroll Peddler|Threat")
	FSPPaperCorruptionIntentSignature OnPaperCorruptionIntent;

private:
	void BindNoiseSubscription();
	void UnbindNoiseSubscription();
	void HandleNoisePublished(const FSPNoiseEvent& NoiseEvent);

	void TickEchoHunter(float DeltaSeconds, double ServerTime);
	void TickPaperEater(float DeltaSeconds, double ServerTime);
	void TickStaggerOrRetreat(float DeltaSeconds, double ServerTime);
	void TransitionTo(ESPThreatBehaviorState NewState, double UntilServerTime = 0.0);

	void MoveToward(const FVector& Destination, float Speed, float DeltaSeconds);
	void MoveAwayFrom(const FVector& Origin, float Speed, float DeltaSeconds);
	APawn* FindNearestVisiblePlayer() const;
	bool IsPlayerTargetEligible(const APawn* Pawn) const;
	bool HasLineOfSightTo(const AActor* TargetActor, bool bRequireViewCone) const;
	void BeginTelegraphedAttack(APawn* TargetPawn, double ServerTime);
	void ResolveTelegraphedAttack(double ServerTime);

	AActor* FindBestPaperTarget() const;
	bool IsPaperTargetAvailable(const AActor* TargetActor) const;
	void RequestPaperCorruption(AActor* TargetActor, double ServerTime);

	double GetServerTimeSeconds() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scroll Peddler|Threat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> ThreatMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Peddler|Threat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USPGrayboxThreatDefinition> ThreatDefinition;

	UPROPERTY(Replicated)
	ESPThreatBehaviorState BehaviorState = ESPThreatBehaviorState::Idle;

	UPROPERTY(Replicated)
	FVector InvestigationLocation = FVector::ZeroVector;

	UPROPERTY(Replicated)
	double TelegraphEndServerTime = 0.0;

	UPROPERTY(Replicated)
	TObjectPtr<AActor> ReplicatedTargetActor;

	TWeakObjectPtr<USPNoiseSubsystem> NoiseSubsystem;
	TWeakObjectPtr<AActor> CurrentTargetActor;
	FDelegateHandle NoiseDelegateHandle;

	double StateEndServerTime = 0.0;
	double NextPaperScanServerTime = 0.0;
	double LastInvestigatedNoiseServerTime = -1.0;
	double LastInvestigatedNoiseScore = 0.0;
	FVector RetreatOrigin = FVector::ZeroVector;
};
