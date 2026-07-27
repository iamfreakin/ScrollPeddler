#include "Game/SPPartyState.h"

#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogSPPartyState, Log, All);

ASPPartyState::ASPPartyState()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
}

void ASPPartyState::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASPPartyState, GovernanceState);
}

bool ASPPartyState::AuthorityInitializeParty(
	const FString& HostMemberId,
	const FString& HostDisplayName)
{
	if (!HasAuthority()
		|| !GovernanceState.Initialize(
			HostMemberId,
			HostDisplayName))
	{
		return false;
	}

	ForceNetUpdate();
	return true;
}

ESPPartyActionResult ASPPartyState::AuthorityRegisterMember(
	const FString& MemberId,
	const FString& DisplayName,
	const FSPPartyActionRequest& Request)
{
	if (!HasAuthority())
	{
		return ESPPartyActionResult::NotAuthority;
	}
	const int64 PreviousRevision = GovernanceState.Revision;
	const ESPPartyActionResult Result =
		GovernanceState.TryRegisterMember(
			MemberId,
			DisplayName,
			Request);
	NotifyIfMutated(PreviousRevision);
	return Result;
}

ESPPartyActionResult ASPPartyState::AuthoritySetMemberConnected(
	const FString& MemberId,
	const bool bConnected,
	const FSPPartyActionRequest& Request)
{
	if (!HasAuthority())
	{
		return ESPPartyActionResult::NotAuthority;
	}
	const int64 PreviousRevision = GovernanceState.Revision;
	const ESPPartyActionResult Result =
		GovernanceState.TrySetMemberConnected(
			MemberId,
			bConnected,
			Request);
	NotifyIfMutated(PreviousRevision);
	return Result;
}

ESPPartyActionResult ASPPartyState::AuthoritySetReady(
	const FString& RequesterMemberId,
	const bool bReady,
	const FSPPartyActionRequest& Request)
{
	if (!HasAuthority())
	{
		return ESPPartyActionResult::NotAuthority;
	}
	const int64 PreviousRevision = GovernanceState.Revision;
	const ESPPartyActionResult Result =
		GovernanceState.TrySetReady(
			RequesterMemberId,
			bReady,
			Request);
	NotifyIfMutated(PreviousRevision);
	return Result;
}

ESPPartyActionResult ASPPartyState::AuthoritySetRunPhase(
	const ESPRunPhase RequestedPhase,
	const FSPPartyActionRequest& Request)
{
	if (!HasAuthority())
	{
		return ESPPartyActionResult::NotAuthority;
	}
	const int64 PreviousRevision = GovernanceState.Revision;
	const ESPPartyActionResult Result =
		GovernanceState.TrySetRunPhase(RequestedPhase, Request);
	NotifyIfMutated(PreviousRevision);
	return Result;
}

ESPPartyActionResult ASPPartyState::AuthorityHostKickImmediately(
	const FString& RequesterMemberId,
	const FString& TargetMemberId,
	const FSPPartyActionRequest& Request)
{
	if (!HasAuthority())
	{
		return ESPPartyActionResult::NotAuthority;
	}
	const int64 PreviousRevision = GovernanceState.Revision;
	const ESPPartyActionResult Result =
		GovernanceState.TryHostKickImmediately(
			RequesterMemberId,
			TargetMemberId,
			Request);
	NotifyIfMutated(PreviousRevision);
	return Result;
}

ESPPartyActionResult ASPPartyState::AuthorityStartKickVote(
	const FString& ProposerMemberId,
	const FString& TargetMemberId,
	const FGuid& VoteId,
	const FSPPartyActionRequest& Request,
	const double OverrideServerTime)
{
	if (!HasAuthority())
	{
		return ESPPartyActionResult::NotAuthority;
	}
	const int64 PreviousRevision = GovernanceState.Revision;
	const ESPPartyActionResult Result =
		GovernanceState.TryStartKickVote(
			ProposerMemberId,
			TargetMemberId,
			VoteId,
			ResolveServerTime(OverrideServerTime),
			Request);
	NotifyIfMutated(PreviousRevision);
	return Result;
}

ESPPartyActionResult ASPPartyState::AuthorityStartImportantCraftVote(
	const FString& ProposerMemberId,
	const FPrimaryAssetId& ProposalId,
	const FGuid& VoteId,
	const FSPPartyActionRequest& Request,
	const double OverrideServerTime)
{
	if (!HasAuthority())
	{
		return ESPPartyActionResult::NotAuthority;
	}
	const int64 PreviousRevision = GovernanceState.Revision;
	const ESPPartyActionResult Result =
		GovernanceState.TryStartImportantCraftVote(
			ProposerMemberId,
			ProposalId,
			VoteId,
			ResolveServerTime(OverrideServerTime),
			Request);
	NotifyIfMutated(PreviousRevision);
	return Result;
}

ESPPartyActionResult ASPPartyState::AuthorityCastVote(
	const FString& VoterMemberId,
	const FGuid& VoteId,
	const ESPPartyVoteChoice Choice,
	const FSPPartyActionRequest& Request,
	const double OverrideServerTime)
{
	if (!HasAuthority())
	{
		return ESPPartyActionResult::NotAuthority;
	}
	const int64 PreviousRevision = GovernanceState.Revision;
	const ESPPartyActionResult Result =
		GovernanceState.TryCastVote(
			VoterMemberId,
			VoteId,
			Choice,
			ResolveServerTime(OverrideServerTime),
			Request);
	NotifyIfMutated(PreviousRevision);
	return Result;
}

ESPPartyActionResult ASPPartyState::AuthoritySubmitChat(
	const FString& SenderMemberId,
	const FString& Message,
	const FSPPartyActionRequest& Request,
	const double OverrideServerTime)
{
	if (!HasAuthority())
	{
		return ESPPartyActionResult::NotAuthority;
	}
	const int64 PreviousRevision = GovernanceState.Revision;
	const ESPPartyActionResult Result =
		GovernanceState.TrySubmitChat(
			SenderMemberId,
			Message,
			ResolveServerTime(OverrideServerTime),
			Request);
	NotifyIfMutated(PreviousRevision);
	return Result;
}

bool ASPPartyState::AuthorityRefreshExpiredVotes(
	const double OverrideServerTime)
{
	if (!HasAuthority())
	{
		return false;
	}
	const int64 PreviousRevision = GovernanceState.Revision;
	const bool bChanged = GovernanceState.RefreshExpiredVotes(
		ResolveServerTime(OverrideServerTime));
	NotifyIfMutated(PreviousRevision);
	return bChanged;
}

void ASPPartyState::OnRep_GovernanceState()
{
	UE_LOG(LogSPPartyState, Verbose,
		TEXT("SP_PARTY_STATE_REPLICATED revision=%lld phase=%d members=%d votes=%d chat=%d"),
		GovernanceState.Revision,
		static_cast<int32>(GovernanceState.RunPhase),
		GovernanceState.GetPresentMemberCount(),
		GovernanceState.Votes.Num(),
		GovernanceState.ChatMessages.Num());
}

double ASPPartyState::ResolveServerTime(
	const double OverrideServerTime) const
{
	if (OverrideServerTime >= 0.0)
	{
		return OverrideServerTime;
	}
	return GetWorld()
		? static_cast<double>(GetWorld()->GetTimeSeconds())
		: 0.0;
}

void ASPPartyState::NotifyIfMutated(const int64 PreviousRevision)
{
	if (GovernanceState.Revision != PreviousRevision)
	{
		ForceNetUpdate();
	}
}
