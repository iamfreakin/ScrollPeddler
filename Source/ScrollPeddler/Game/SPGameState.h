#pragma once

#include "CoreMinimal.h"
#include "Core/SPRunTypes.h"
#include "Core/SPTypes.h"
#include "GameFramework/GameStateBase.h"
#include "TimerManager.h"
#include "SPGameState.generated.h"

UCLASS()
class SCROLLPEDDLER_API ASPGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ASPGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void AuthorityInitializeSession(const FGuid& InSessionId, int32 InExpectedPlayers);
	void AuthoritySetPhase(ESPSessionPhase InPhase);
	void AuthoritySetExtractedPlayerCount(int32 InExtractedPlayerCount);
	void AuthorityMarkSettlementCommitted();

	/** Initializes the new run model without replacing the legacy session API. */
	bool AuthorityInitializeRun(const FGuid& InRunId, int32 InRosterPlayerCount);

	/** Starts the fixed 25-minute expedition plus 2-minute collapse window. */
	bool AuthorityStartExpedition(double InStartServerTime = -1.0);

	/** Monotonic server-only phase transition; repeated transitions are idempotent. */
	bool AuthoritySetRunPhase(ESPRunPhase InPhase);

	/** Atomically updates mutually exclusive roster counters after validation. */
	bool AuthoritySetRunRosterCounts(
		int32 InActivePlayerCount,
		int32 InDisconnectedPlayerCount,
		int32 InRunExtractedPlayerCount,
		int32 InMissingPlayerCount);

	/** Applies deadline-derived Collapse or Resolution state on authority. */
	bool AuthorityRefreshRunPhase(double CurrentServerTime = -1.0);

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	FGuid GetSessionId() const { return SessionId; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	ESPSessionPhase GetSessionPhase() const { return SessionPhase; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	int32 GetExpectedPlayers() const { return ExpectedPlayers; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	int32 GetExtractedPlayerCount() const { return ExtractedPlayerCount; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	bool IsSettlementCommitted() const { return bSettlementCommitted; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	FGuid GetRunId() const { return RunId; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	ESPRunPhase GetRunPhase() const { return RunPhase; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	double GetRunStartServerTime() const { return RunStartServerTime; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	double GetExpeditionDeadlineServerTime() const { return ExpeditionDeadlineServerTime; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	double GetCollapseDeadlineServerTime() const { return CollapseDeadlineServerTime; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	int32 GetRunRosterPlayerCount() const { return RunRosterPlayerCount; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	int32 GetActiveRunPlayerCount() const { return ActiveRunPlayerCount; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	int32 GetDisconnectedRunPlayerCount() const { return DisconnectedRunPlayerCount; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	int32 GetRunExtractedPlayerCount() const { return RunExtractedPlayerCount; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	int32 GetMissingRunPlayerCount() const { return MissingRunPlayerCount; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	int32 GetUnresolvedRunPlayerCount() const;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	double GetSecondsUntilCollapse() const;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	double GetSecondsUntilResolution() const;

private:
	UFUNCTION()
	void OnRep_SessionMetadata();

	UFUNCTION()
	void OnRep_SessionPhase();

	UFUNCTION()
	void OnRep_ExtractionProgress();

	UFUNCTION()
	void OnRep_RunMetadata();

	UFUNCTION()
	void OnRep_RunPhase();

	UFUNCTION()
	void OnRep_RunRoster();

	void ScheduleRunDeadlineTimers();
	void ClearRunDeadlineTimers();
	void HandleExpeditionDeadline();
	void HandleCollapseDeadline();
	double ResolveServerTime(double RequestedServerTime) const;

	UPROPERTY(ReplicatedUsing = OnRep_SessionMetadata)
	FGuid SessionId;

	UPROPERTY(ReplicatedUsing = OnRep_SessionMetadata)
	int32 ExpectedPlayers = 1;

	UPROPERTY(ReplicatedUsing = OnRep_SessionPhase)
	ESPSessionPhase SessionPhase = ESPSessionPhase::LobbyCreated;

	UPROPERTY(ReplicatedUsing = OnRep_ExtractionProgress)
	int32 ExtractedPlayerCount = 0;

	UPROPERTY(ReplicatedUsing = OnRep_SessionPhase)
	bool bSettlementCommitted = false;

	UPROPERTY(ReplicatedUsing = OnRep_RunMetadata)
	FGuid RunId;

	UPROPERTY(ReplicatedUsing = OnRep_RunPhase)
	ESPRunPhase RunPhase = ESPRunPhase::Preparing;

	UPROPERTY(ReplicatedUsing = OnRep_RunMetadata)
	double RunStartServerTime = 0.0;

	UPROPERTY(ReplicatedUsing = OnRep_RunMetadata)
	double ExpeditionDeadlineServerTime = 0.0;

	UPROPERTY(ReplicatedUsing = OnRep_RunMetadata)
	double CollapseDeadlineServerTime = 0.0;

	UPROPERTY(ReplicatedUsing = OnRep_RunRoster)
	int32 RunRosterPlayerCount = 0;

	UPROPERTY(ReplicatedUsing = OnRep_RunRoster)
	int32 ActiveRunPlayerCount = 0;

	UPROPERTY(ReplicatedUsing = OnRep_RunRoster)
	int32 DisconnectedRunPlayerCount = 0;

	UPROPERTY(ReplicatedUsing = OnRep_RunRoster)
	int32 RunExtractedPlayerCount = 0;

	UPROPERTY(ReplicatedUsing = OnRep_RunRoster)
	int32 MissingRunPlayerCount = 0;

	FTimerHandle ExpeditionDeadlineTimerHandle;
	FTimerHandle CollapseDeadlineTimerHandle;
};
