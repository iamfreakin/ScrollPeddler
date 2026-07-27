#pragma once

#include "CoreMinimal.h"
#include "Core/SPItemTypes.h"
#include "Core/SPPartyGovernanceTypes.h"
#include "Data/SPVerticalSliceDefinitions.h"
#include "Game/SPPlayerState.h"
#include "GameFramework/GameModeBase.h"
#include "World/SPThreatRuntimeTypes.h"
#include "SPGameMode.generated.h"

class ASPCharacter;
class ASPDungeonLayoutActor;
class ASPExtractionZone;
class ASPGameState;
class ASPPlayerController;
class ASPPlayerState;
class ASPPartyState;
class ASPThreatDirector;
class USPScrollDefinition;
class USPScrollEngravingDefinition;
class USPGrayboxThreatDefinition;
class USPDungeonSeedDefinition;
class USPContractDefinition;

struct FSPDisconnectedRunRecord
{
	FSPPlayerRunSnapshot PlayerSnapshot;
	FSPInventoryState InventorySnapshot;
	double ExpiresAtServerTime = 0.0;
};

struct FSPRunOutcomeRecord
{
	FString PlayerId;
	bool bExtracted = false;
	int32 PickedUpCount = 0;
	int32 ConsumedScrollCount = 0;
	int32 ExtractedScrollCount = 0;
	int32 GoldDelta = 0;
	TArray<FSPItemInstance> ExtractedItems;
};

UCLASS()
class SCROLLPEDDLER_API ASPGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASPGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	/** Routes an authority request through the extraction zone's distance validation. */
	bool TryExtractCharacter(ASPCharacter* Character);

	/** Called only by ASPExtractionZone after its authority and distance checks pass. */
	void HandlePlayerReachedExtraction(ASPCharacter* Character);

	/** Completes settlement only after each owning client verifies its local SaveGame write. */
	void HandleSettlementAck(
		ASPPlayerController* PlayerController,
		const FGuid& AckSessionId,
		const FString& ResultHash,
		bool bSaved);

	ASPExtractionZone* GetExtractionZone() const { return ExtractionZone; }
	int32 GetExpectedPlayers() const { return ExpectedPlayers; }
	ASPPartyState* GetPartyState() const { return PartyState; }

	ESPPartyActionResult HandlePartyReadyRequest(
		ASPPlayerController* Requester,
		bool bReady,
		const FSPPartyActionRequest& Request);
	ESPPartyActionResult HandlePartyChatRequest(
		ASPPlayerController* Requester,
		const FString& Message,
		const FSPPartyActionRequest& Request);
	ESPPartyActionResult HandleHostKickRequest(
		ASPPlayerController* Requester,
		int32 TargetPlayerId,
		const FSPPartyActionRequest& Request);
	ESPPartyActionResult HandleStartKickVoteRequest(
		ASPPlayerController* Requester,
		int32 TargetPlayerId,
		const FGuid& VoteId,
		const FSPPartyActionRequest& Request);
	ESPPartyActionResult HandleCastPartyVoteRequest(
		ASPPlayerController* Requester,
		const FGuid& VoteId,
		ESPPartyVoteChoice Choice,
		const FSPPartyActionRequest& Request);

private:
	void SpawnSpikeWorld();
	void SpawnPlayerStarts();
	void SpawnGrayboxLighting();
	void SpawnGrayboxBlocks();
	void SpawnDungeonLayout();
	void SpawnSpikePickups();
	void SpawnExtractionZone();
	void SpawnThreats();
	void RefreshSessionPhase();
	void RefreshRunRosterAndResolution();
	void ExpireDisconnectedPlayers(double ServerTime);
	void ApplyPendingReconnectInventories();
	void ForceResolveOutstandingPlayers();
	void CaptureRunOutcome(
		const FString& RosterKey,
		const ASPPlayerState* PlayerState,
		const ASPCharacter* Character,
		bool bExtracted);
	void CaptureMissingOutcome(
		const FString& RosterKey,
		const FSPPlayerRunSnapshot* Snapshot = nullptr);
	FString ResolveRosterKey(const AController* Controller) const;
	FString ResolveHostCampaignOwnerId() const;
	bool TryRestoreDisconnectedPlayer(
		APlayerController* NewPlayer,
		const FString& RosterKey);
	bool EnsureHostCampaignReady();
	bool CommitHostCampaignSettlement();
	void RegisterPartyMember(APlayerController* NewPlayer, const FString& RosterKey);
	void UpdatePartyRunPhase(ESPRunPhase RunPhase);
	void LockOnlineLobbyForExpedition();
	bool IsPartyMemberKicked(const FString& RosterKey) const;
	bool IsRunReconnectAllowed(const FString& RosterKey) const;
	void ApplyKickedMemberRunPolicy(
		const FString& RosterKey,
		ESPRunPhase RunPhase,
		ASPPlayerController* TargetController);
	ASPPlayerController* FindControllerByPlayerId(int32 PlayerId) const;
	void ApplyPassedKickVote(const FGuid& VoteId);
	void TryCommitSettlement();
	void CompleteSettlementAfterAckWindow();
	FString BuildLocalPlayerId(const ASPPlayerState* PlayerState) const;

	UFUNCTION()
	void HandleThreatAttackIntent(const FSPThreatAttackIntent& Intent);
	void HandleThreatSpawnRequested(ESPThreatArchetype Archetype);

	FGuid SessionId;
	int32 ExpectedPlayers = 2;
	int32 CampaignSlotIndex = 0;
	int32 DungeonSeed = 1729;
	bool bSettlementStarted = false;
	bool bHostCampaignCommitted = false;
	bool bSpikeWorldSpawned = false;
	bool bOnlineLobbyLocked = false;
	double NextRunRefreshServerTime = 0.0;
	static constexpr double ReconnectGraceSeconds = 120.0;
	static constexpr float SettlementAckWindowSeconds = 10.0f;
	TMap<TWeakObjectPtr<ASPPlayerController>, FString> PendingSettlementHashes;
	TSet<TWeakObjectPtr<ASPPlayerController>> SuccessfulSettlementAcks;
	TMap<TWeakObjectPtr<AController>, FString> ControllerRosterKeys;
	TSet<FString> RunRosterKeys;
	TMap<FString, FSPDisconnectedRunRecord> DisconnectedRunPlayers;
	TMap<FString, FSPInventoryState> PendingReconnectInventories;
	TMap<FString, FSPRunOutcomeRecord> RunOutcomes;
	FTimerHandle SettlementAckTimerHandle;

	UPROPERTY(Transient)
	TObjectPtr<ASPExtractionZone> ExtractionZone;

	UPROPERTY()
	TObjectPtr<USPScrollDefinition> SpikeScrollDefinition;

	UPROPERTY()
	TObjectPtr<USPScrollEngravingDefinition> AmplifiedEngravingDefinition;

	UPROPERTY()
	TObjectPtr<USPScrollEngravingDefinition> StableEngravingDefinition;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USPGrayboxThreatDefinition>> RuntimeThreatDefinitions;

	UPROPERTY(Transient)
	TObjectPtr<ASPThreatDirector> ThreatDirector;

	UPROPERTY(Transient)
	TObjectPtr<ASPPartyState> PartyState;

	UPROPERTY(Transient)
	TObjectPtr<ASPDungeonLayoutActor> DungeonLayoutActor;

	UPROPERTY(Transient)
	TObjectPtr<USPDungeonSeedDefinition> RuntimeDungeonSeedDefinition;

	UPROPERTY(Transient)
	TObjectPtr<USPContractDefinition> RuntimeContractDefinition;

	TSet<FGuid> ProcessedThreatAttackIntents;
	TSet<FGuid> AppliedKickVoteIds;

#if WITH_DEV_AUTOMATION_TESTS
	friend class FSPGameModeKickAuthorityCleanupTest;
#endif
};
