#pragma once

#include "CoreMinimal.h"
#include "Core/SPPartyGovernanceTypes.h"
#include "Core/SPTypes.h"
#include "GameFramework/PlayerController.h"
#include "Online/SPOnlineSessionSubsystem.h"
#include "SPPlayerController.generated.h"

UCLASS()
class SCROLLPEDDLER_API ASPPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** Starts a raw listen server on the spike map. No online service is involved. */
	UFUNCTION(Exec)
	void SPHost(int32 InExpectedPlayers = 2);

	/** Travels directly to an IP or Unreal travel URL. */
	UFUNCTION(Exec)
	void SPJoin(const FString& Address);

	UFUNCTION(Exec)
	void SPCreateLobby(int32 MaxPlayers = 4);

	UFUNCTION(Exec)
	void SPFindLobbies();

	UFUNCTION(Exec)
	void SPQuickPlay();

	UFUNCTION(Exec)
	void SPJoinLobby(int32 SearchResultIndex);

	UFUNCTION(Exec)
	void SPReady(bool bReady = true);

	UFUNCTION(Exec)
	void SPChat(const FString& Message);

	UFUNCTION(Exec)
	void SPKick(int32 TargetPlayerId);

	UFUNCTION(Exec)
	void SPVoteKick(int32 TargetPlayerId);

	UFUNCTION(Exec)
	void SPVote(const FString& VoteId, bool bApprove = true);

	UFUNCTION(Exec)
	void SPMutePlayer(int32 TargetPlayerId, bool bMute = true);

	UFUNCTION(Client, Reliable)
	void ClientCommitSessionResult(const FSPSessionResult& Result);

	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeSessionResult(FGuid SessionId, const FString& ResultHash, bool bSaved);

	UFUNCTION(Server, Reliable)
	void ServerSetPartyReady(
		bool bReady,
		const FSPPartyActionRequest& Request);

	UFUNCTION(Server, Reliable)
	void ServerSubmitPartyChat(
		const FString& Message,
		const FSPPartyActionRequest& Request);

	UFUNCTION(Server, Reliable)
	void ServerRequestHostKick(
		int32 TargetPlayerId,
		const FSPPartyActionRequest& Request);

	UFUNCTION(Server, Reliable)
	void ServerRequestKickVote(
		int32 TargetPlayerId,
		FGuid VoteId,
		const FSPPartyActionRequest& Request);

	UFUNCTION(Server, Reliable)
	void ServerCastPartyVote(
		FGuid VoteId,
		ESPPartyVoteChoice Choice,
		const FSPPartyActionRequest& Request);

	UFUNCTION(Client, Reliable)
	void ClientNotifyPartyAction(ESPPartyActionResult Result);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;

private:
	enum class EAutoSpikeStep : uint8
	{
		WaitingForPawn,
		WaitingForPickup,
		WaitingForInventory,
		WaitingForConsumption,
		WaitingForSettlement,
		Finished
	};

	void StartAutoSpikeIfRequested();
	void RunAutoSpikeStep();
	void ScheduleAutoQuitIfRequested();
	void RequestAutoQuit();
	class ASPPartyState* FindPartyState() const;
	FSPPartyActionRequest MakePartyActionRequest() const;

	UFUNCTION()
	void HandleOnlineOperationComplete(
		ESPOnlineSessionOperation Operation,
		ESPOnlineSessionResult Result);

	UFUNCTION()
	void HandleOnlineSearchResults(
		const TArray<FSPOnlineLobbySummary>& Results);

	UFUNCTION()
	void HandleOnlineConnectString(const FString& ConnectString);

	FTimerHandle AutoSpikeTimerHandle;
	FTimerHandle AutoQuitTimerHandle;
	EAutoSpikeStep AutoSpikeStep = EAutoSpikeStep::WaitingForPawn;
	double AutoStepStartedAtSeconds = 0.0;
	double AutoContestedBarrierStartedAtSeconds = 0.0;
	bool bAutoContestedAttemptMade = false;
	int32 PendingOnlineHostPlayers = 0;
};
