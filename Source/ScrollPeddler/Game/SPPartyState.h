#pragma once

#include "CoreMinimal.h"
#include "Core/SPPartyGovernanceTypes.h"
#include "GameFramework/Info.h"
#include "SPPartyState.generated.h"

/**
 * Always-relevant replicated view of server-owned party governance.
 *
 * PlayerController RPCs must derive RequesterMemberId from their authenticated
 * PlayerState and may only forward intent into these authority methods.
 */
UCLASS()
class SCROLLPEDDLER_API ASPPartyState : public AInfo
{
	GENERATED_BODY()

public:
	ASPPartyState();

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool AuthorityInitializeParty(
		const FString& HostMemberId,
		const FString& HostDisplayName);

	ESPPartyActionResult AuthorityRegisterMember(
		const FString& MemberId,
		const FString& DisplayName,
		const FSPPartyActionRequest& Request);

	ESPPartyActionResult AuthoritySetMemberConnected(
		const FString& MemberId,
		bool bConnected,
		const FSPPartyActionRequest& Request);

	ESPPartyActionResult AuthoritySetReady(
		const FString& RequesterMemberId,
		bool bReady,
		const FSPPartyActionRequest& Request);

	ESPPartyActionResult AuthoritySetRunPhase(
		ESPRunPhase RequestedPhase,
		const FSPPartyActionRequest& Request);

	ESPPartyActionResult AuthorityHostKickImmediately(
		const FString& RequesterMemberId,
		const FString& TargetMemberId,
		const FSPPartyActionRequest& Request);

	ESPPartyActionResult AuthorityStartKickVote(
		const FString& ProposerMemberId,
		const FString& TargetMemberId,
		const FGuid& VoteId,
		const FSPPartyActionRequest& Request,
		double OverrideServerTime = -1.0);

	ESPPartyActionResult AuthorityStartImportantCraftVote(
		const FString& ProposerMemberId,
		const FPrimaryAssetId& ProposalId,
		const FGuid& VoteId,
		const FSPPartyActionRequest& Request,
		double OverrideServerTime = -1.0);

	ESPPartyActionResult AuthorityCastVote(
		const FString& VoterMemberId,
		const FGuid& VoteId,
		ESPPartyVoteChoice Choice,
		const FSPPartyActionRequest& Request,
		double OverrideServerTime = -1.0);

	ESPPartyActionResult AuthoritySubmitChat(
		const FString& SenderMemberId,
		const FString& Message,
		const FSPPartyActionRequest& Request,
		double OverrideServerTime = -1.0);

	bool AuthorityRefreshExpiredVotes(
		double OverrideServerTime = -1.0);

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Party")
	const FSPPartyGovernanceState& GetGovernanceState() const
	{
		return GovernanceState;
	}

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Party")
	bool AreAllPresentMembersReady() const
	{
		return GovernanceState.AreAllPresentMembersReady();
	}

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Party")
	bool IsLateJoinAllowed() const
	{
		return GovernanceState.IsLateJoinAllowed();
	}

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Party")
	bool AreInvitesAllowed() const
	{
		return GovernanceState.AreInvitesAllowed();
	}

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Party")
	bool HasPassedImportantCraftVote(
		const FPrimaryAssetId& ProposalId) const
	{
		return GovernanceState.HasPassedImportantCraftVote(ProposalId);
	}

private:
	UFUNCTION()
	void OnRep_GovernanceState();

	double ResolveServerTime(double OverrideServerTime) const;
	void NotifyIfMutated(int64 PreviousRevision);

	UPROPERTY(ReplicatedUsing = OnRep_GovernanceState)
	FSPPartyGovernanceState GovernanceState;
};
