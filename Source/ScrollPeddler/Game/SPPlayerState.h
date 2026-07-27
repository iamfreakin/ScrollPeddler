#pragma once

#include "CoreMinimal.h"
#include "Core/SPRunTypes.h"
#include "Core/SPTypes.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"
#include "SPPlayerState.generated.h"

/** Authority-owned reconnect snapshot. Economic state remains in the host campaign. */
USTRUCT()
struct SCROLLPEDDLER_API FSPPlayerRunSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	int32 PickedUpCount = 0;

	UPROPERTY()
	int32 ConsumedScrollCount = 0;

	UPROPERTY()
	int32 ExtractedScrollCount = 0;

	UPROPERTY()
	int32 CarriedDeliveryValue = 0;

	UPROPERTY()
	int32 GoldDelta = 0;

	UPROPERTY()
	bool bExtracted = false;

	UPROPERTY()
	ESPPlayerCondition PlayerCondition = ESPPlayerCondition::Normal;

	UPROPERTY()
	ESPParticipationState ParticipationState = ESPParticipationState::Hub;

	UPROPERTY()
	int32 DownCount = 0;

	UPROPERTY()
	double BleedoutDeadlineServerTime = 0.0;

	UPROPERTY()
	TArray<FGuid> PickedUpInstanceIds;

	UPROPERTY()
	TArray<FGuid> ConsumedInstanceIds;

	UPROPERTY()
	TArray<FGuid> CarriedInstanceIds;

	UPROPERTY()
	TMap<FGuid, int32> PickedUpDeliveryValues;

	bool IsStructurallyValid() const;
	bool IsBleedoutExpired(double CurrentServerTime) const;
};

UCLASS()
class SCROLLPEDDLER_API ASPPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ASPPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server-only accounting. Returns true only when this instance is newly recorded. */
	bool RecordScrollPickedUp(const FSPScrollInstance& Item);

	/** Server-only accounting. Returns true only when this known carried instance is newly consumed. */
	bool RecordScrollConsumed(const FSPScrollInstance& Item, int32 DeliveryValue);

	/** Server-only compensation used if a reserved world pickup cannot finish its claim. */
	bool RollbackScrollPickedUp(const FSPScrollInstance& Item);

	/** Clears the short-lived reacquire rollback marker after the world claim commits. */
	bool ConfirmScrollPickedUp(const FSPScrollInstance& Item);

	/** Server-only hand/bag drop accounting. The lifetime pickup count is preserved. */
	bool RecordScrollDropped(const FSPScrollInstance& Item);

	/** Server-only and idempotent. Freezes this player's extraction totals. */
	void MarkExtracted();

	/**
	 * Server-only condition transition. Repeating the current state is an
	 * idempotent success; terminal Missing state cannot be reversed.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scroll Peddler|Run")
	bool AuthorityTransitionCondition(ESPPlayerCondition InCondition);

	/**
	 * Server-only participation transition. Repeating the current state is an
	 * idempotent success; terminal spectator state cannot rejoin the run.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scroll Peddler|Run")
	bool AuthorityTransitionParticipation(ESPParticipationState InParticipationState);

	/** Server-only and idempotent. Freezes legacy extraction totals once. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scroll Peddler|Run")
	bool AuthorityMarkExtracted();

	/** Server-only and idempotent. Missing is terminal and enters spectating. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scroll Peddler|Run")
	bool AuthorityMarkMissing();

	/** Captures the authoritative transient run state for a bounded reconnect window. */
	bool AuthorityBuildReconnectSnapshot(FSPPlayerRunSnapshot& OutSnapshot) const;

	/** Restores a non-terminal snapshot and resumes participation as Active. */
	bool AuthorityRestoreReconnectSnapshot(const FSPPlayerRunSnapshot& Snapshot);

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	bool IsExtracted() const { return bExtracted; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	ESPPlayerCondition GetPlayerCondition() const { return PlayerCondition; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	ESPParticipationState GetParticipationState() const { return ParticipationState; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	int32 GetDownCount() const { return DownCount; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	double GetBleedoutEndServerTime() const { return BleedoutEndServerTime; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	float GetBleedoutDurationForCurrentDown() const;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	double GetRemainingBleedoutSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	bool IsRunTerminal() const;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	int32 GetPickedUpCount() const { return PickedUpCount; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	int32 GetConsumedScrollCount() const { return ConsumedScrollCount; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	int32 GetExtractedScrollCount() const { return ExtractedScrollCount; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	int32 GetCarriedDeliveryValue() const { return CarriedDeliveryValue; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	int32 GetGoldDelta() const { return GoldDelta; }

private:
	UFUNCTION()
	void OnRep_Extracted();

	UFUNCTION()
	void OnRep_RunState();

	void ScheduleBleedout(float DurationSeconds);
	void ScheduleBleedoutUntil(double DeadlineServerTime);
	void ClearBleedout();
	void HandleBleedoutExpired();
	double GetAuthoritativeServerTimeSeconds() const;

	UPROPERTY(Replicated)
	int32 PickedUpCount = 0;

	UPROPERTY(Replicated)
	int32 ConsumedScrollCount = 0;

	UPROPERTY(Replicated)
	int32 ExtractedScrollCount = 0;

	UPROPERTY(Replicated)
	int32 CarriedDeliveryValue = 0;

	UPROPERTY(Replicated)
	int32 GoldDelta = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Extracted)
	bool bExtracted = false;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	ESPPlayerCondition PlayerCondition = ESPPlayerCondition::Normal;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	ESPParticipationState ParticipationState = ESPParticipationState::Hub;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	int32 DownCount = 0;

	UPROPERTY(ReplicatedUsing = OnRep_RunState)
	double BleedoutEndServerTime = 0.0;

	/** Authority-only guards against duplicated RPCs and overlap callbacks. */
	TSet<FGuid> PickedUpInstanceIds;
	TSet<FGuid> ConsumedInstanceIds;
	TSet<FGuid> CarriedInstanceIds;
	TSet<FGuid> RecentlyReacquiredInstanceIds;
	TMap<FGuid, int32> PickedUpDeliveryValues;

	FTimerHandle BleedoutTimerHandle;
};
