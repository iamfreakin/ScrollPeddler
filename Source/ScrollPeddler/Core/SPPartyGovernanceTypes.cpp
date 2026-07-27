#include "Core/SPPartyGovernanceTypes.h"

namespace SPPartyGovernance
{
	bool IsKnownRunPhase(const ESPRunPhase Phase)
	{
		return StaticEnum<ESPRunPhase>()->IsValidEnumValue(
			static_cast<int64>(Phase));
	}

	bool IsKnownVoteKind(const ESPPartyVoteKind Kind)
	{
		return StaticEnum<ESPPartyVoteKind>()->IsValidEnumValue(
			static_cast<int64>(Kind));
	}

	bool IsKnownVoteChoice(const ESPPartyVoteChoice Choice)
	{
		return StaticEnum<ESPPartyVoteChoice>()->IsValidEnumValue(
			static_cast<int64>(Choice));
	}

	bool IsKnownVoteStatus(const ESPPartyVoteStatus Status)
	{
		return StaticEnum<ESPPartyVoteStatus>()->IsValidEnumValue(
			static_cast<int64>(Status));
	}

	bool IsValidMemberIdentity(
		const FString& MemberId,
		const FString& DisplayName)
	{
		return !MemberId.IsEmpty()
			&& MemberId.Len()
				<= FSPPartyGovernanceState::MaximumMemberIdCharacters
			&& !DisplayName.IsEmpty()
			&& DisplayName.Len()
				<= FSPPartyGovernanceState::MaximumDisplayNameCharacters;
	}

	bool IsFiniteNonNegativeTime(const double ServerTime)
	{
		return FMath::IsFinite(ServerTime) && ServerTime >= 0.0;
	}
}

bool FSPPartyGovernanceState::Initialize(
	const FString& HostMemberId,
	const FString& HostDisplayName)
{
	FString NormalizedHostId = HostMemberId;
	FString NormalizedDisplayName = HostDisplayName;
	NormalizedHostId.TrimStartAndEndInline();
	NormalizedDisplayName.TrimStartAndEndInline();
	if (!SPPartyGovernance::IsValidMemberIdentity(
		NormalizedHostId,
		NormalizedDisplayName))
	{
		return false;
	}

	Revision = 0;
	RunPhase = ESPRunPhase::Preparing;
	Members.Reset();
	Votes.Reset();
	ChatMessages.Reset();
	LastChatSequence = 0;
	ProcessedRequests.Reset();

	FSPPartyMemberState& Host = Members.AddDefaulted_GetRef();
	Host.MemberId = MoveTemp(NormalizedHostId);
	Host.DisplayName = MoveTemp(NormalizedDisplayName);
	Host.bIsHost = true;
	return IsStructurallyValid();
}

bool FSPPartyGovernanceState::IsStructurallyValid() const
{
	if (Revision < 0
		|| !SPPartyGovernance::IsKnownRunPhase(RunPhase)
		|| GetPresentMemberCount() < 1
		|| GetPresentMemberCount() > MaximumPartyMembers
		|| ChatMessages.Num() > ChatRingCapacity
		|| LastChatSequence < 0)
	{
		return false;
	}

	TSet<FString> MemberIds;
	int32 HostCount = 0;
	for (const FSPPartyMemberState& Member : Members)
	{
		if (!SPPartyGovernance::IsValidMemberIdentity(
				Member.MemberId,
				Member.DisplayName)
			|| MemberIds.Contains(Member.MemberId)
			|| (Member.bReady
				&& (!Member.bPresent
					|| !Member.bConnected
					|| Member.bKicked
					|| !IsHubPhase(RunPhase)))
			|| (Member.bKicked && Member.bPresent))
		{
			return false;
		}
		MemberIds.Add(Member.MemberId);
		HostCount += Member.bIsHost ? 1 : 0;
	}
	if (HostCount != 1)
	{
		return false;
	}

	TSet<FGuid> VoteIds;
	for (const FSPPartyVoteState& Vote : Votes)
	{
		if (!Vote.VoteId.IsValid()
			|| VoteIds.Contains(Vote.VoteId)
			|| !SPPartyGovernance::IsKnownVoteKind(Vote.Kind)
			|| !SPPartyGovernance::IsKnownVoteStatus(Vote.Status)
			|| Vote.ProposerMemberId.IsEmpty()
			|| Vote.ExpiresServerTime < Vote.OpenedServerTime
			|| !SPPartyGovernance::IsFiniteNonNegativeTime(
				Vote.OpenedServerTime)
			|| !SPPartyGovernance::IsFiniteNonNegativeTime(
				Vote.ExpiresServerTime)
			|| (Vote.Kind == ESPPartyVoteKind::KickMember
				&& Vote.TargetMemberId.IsEmpty())
			|| (Vote.Kind == ESPPartyVoteKind::ImportantCraft
				&& !Vote.CraftProposalId.IsValid()))
		{
			return false;
		}
		VoteIds.Add(Vote.VoteId);

		TSet<FString> Voters;
		for (const FSPPartyBallot& Ballot : Vote.Ballots)
		{
			if (Ballot.VoterMemberId.IsEmpty()
				|| Voters.Contains(Ballot.VoterMemberId)
				|| !SPPartyGovernance::IsKnownVoteChoice(Ballot.Choice))
			{
				return false;
			}
			Voters.Add(Ballot.VoterMemberId);
		}
	}

	int64 PreviousSequence = 0;
	for (const FSPPartyChatMessage& ChatMessage : ChatMessages)
	{
		if (ChatMessage.Sequence <= PreviousSequence
			|| ChatMessage.Sequence > LastChatSequence
			|| !SPPartyGovernance::IsFiniteNonNegativeTime(
				ChatMessage.ServerTimestamp)
			|| ChatMessage.SenderMemberId.IsEmpty()
			|| ChatMessage.Message.IsEmpty()
			|| ChatMessage.Message.Len() > MaximumChatCharacters)
		{
			return false;
		}
		PreviousSequence = ChatMessage.Sequence;
	}
	return true;
}

int32 FSPPartyGovernanceState::GetPresentMemberCount() const
{
	int32 PresentMemberCount = 0;
	for (const FSPPartyMemberState& Member : Members)
	{
		PresentMemberCount += Member.bPresent && !Member.bKicked ? 1 : 0;
	}
	return PresentMemberCount;
}

bool FSPPartyGovernanceState::AreAllPresentMembersReady() const
{
	int32 EligibleMemberCount = 0;
	for (const FSPPartyMemberState& Member : Members)
	{
		if (Member.bPresent && !Member.bKicked)
		{
			++EligibleMemberCount;
			if (!Member.bConnected || !Member.bReady)
			{
				return false;
			}
		}
	}
	return EligibleMemberCount > 0 && IsHubPhase(RunPhase);
}

bool FSPPartyGovernanceState::IsLateJoinAllowed() const
{
	return IsHubPhase(RunPhase)
		&& GetPresentMemberCount() < MaximumPartyMembers;
}

bool FSPPartyGovernanceState::AreInvitesAllowed() const
{
	return IsLateJoinAllowed();
}

bool FSPPartyGovernanceState::HasPassedImportantCraftVote(
	const FPrimaryAssetId& ProposalId) const
{
	return ProposalId.IsValid()
		&& Votes.ContainsByPredicate(
			[&ProposalId](const FSPPartyVoteState& Vote)
			{
				return Vote.Kind == ESPPartyVoteKind::ImportantCraft
					&& Vote.CraftProposalId == ProposalId
					&& Vote.Status == ESPPartyVoteStatus::Passed;
			});
}

const FSPPartyMemberState* FSPPartyGovernanceState::FindMember(
	const FString& MemberId) const
{
	return Members.FindByPredicate(
		[&MemberId](const FSPPartyMemberState& Member)
		{
			return Member.MemberId == MemberId;
		});
}

const FSPPartyVoteState* FSPPartyGovernanceState::FindVote(
	const FGuid& VoteId) const
{
	return VoteId.IsValid()
		? Votes.FindByPredicate(
			[&VoteId](const FSPPartyVoteState& Vote)
			{
				return Vote.VoteId == VoteId;
			})
		: nullptr;
}

ESPPartyActionResult FSPPartyGovernanceState::TryRegisterMember(
	const FString& MemberId,
	const FString& DisplayName,
	const FSPPartyActionRequest& Request)
{
	FString NormalizedMemberId = MemberId;
	FString NormalizedDisplayName = DisplayName;
	NormalizedMemberId.TrimStartAndEndInline();
	NormalizedDisplayName.TrimStartAndEndInline();
	const FString Fingerprint = FString::Printf(
		TEXT("register|%lld|%s|%s"),
		Request.ExpectedRevision,
		*NormalizedMemberId,
		*NormalizedDisplayName);
	const ESPPartyActionResult RequestResult =
		CheckRequest(Request, Fingerprint);
	if (RequestResult != ESPPartyActionResult::Success)
	{
		return RequestResult;
	}
	if (!IsStructurallyValid()
		|| !SPPartyGovernance::IsValidMemberIdentity(
			NormalizedMemberId,
			NormalizedDisplayName))
	{
		return ESPPartyActionResult::InvalidRequest;
	}
	if (Request.ExpectedRevision != Revision)
	{
		return ESPPartyActionResult::RevisionMismatch;
	}
	if (!IsHubPhase(RunPhase))
	{
		return ESPPartyActionResult::PhaseLocked;
	}
	if (const FSPPartyMemberState* Existing =
		FindMember(NormalizedMemberId))
	{
		return Existing->bKicked
			? ESPPartyActionResult::MemberKicked
			: ESPPartyActionResult::MemberExists;
	}
	if (GetPresentMemberCount() >= MaximumPartyMembers)
	{
		return ESPPartyActionResult::PartyFull;
	}
	if (Revision == MAX_int64)
	{
		return ESPPartyActionResult::ArithmeticOverflow;
	}

	FSPPartyMemberState& Member = Members.AddDefaulted_GetRef();
	Member.MemberId = MoveTemp(NormalizedMemberId);
	Member.DisplayName = MoveTemp(NormalizedDisplayName);
	RecordRequest(Request, Fingerprint);
	AdvanceRevision();
	return ESPPartyActionResult::Success;
}

ESPPartyActionResult FSPPartyGovernanceState::TrySetMemberConnected(
	const FString& MemberId,
	const bool bConnected,
	const FSPPartyActionRequest& Request)
{
	const FString Fingerprint = FString::Printf(
		TEXT("connection|%lld|%s|%d"),
		Request.ExpectedRevision,
		*MemberId,
		bConnected ? 1 : 0);
	const ESPPartyActionResult RequestResult =
		CheckRequest(Request, Fingerprint);
	if (RequestResult != ESPPartyActionResult::Success)
	{
		return RequestResult;
	}
	if (!IsStructurallyValid())
	{
		return ESPPartyActionResult::InvalidState;
	}
	if (Request.ExpectedRevision != Revision)
	{
		return ESPPartyActionResult::RevisionMismatch;
	}

	FSPPartyMemberState* Member = FindMutableMember(MemberId);
	if (!Member)
	{
		return ESPPartyActionResult::MemberNotFound;
	}
	if (Member->bKicked || !Member->bPresent)
	{
		return ESPPartyActionResult::MemberKicked;
	}
	if (Member->bConnected == bConnected)
	{
		RecordRequest(Request, Fingerprint);
		return ESPPartyActionResult::Success;
	}
	if (Revision == MAX_int64)
	{
		return ESPPartyActionResult::ArithmeticOverflow;
	}

	Member->bConnected = bConnected;
	if (!bConnected)
	{
		Member->bReady = false;
	}
	EvaluateAllActiveVotes();
	RecordRequest(Request, Fingerprint);
	AdvanceRevision();
	return ESPPartyActionResult::Success;
}

ESPPartyActionResult FSPPartyGovernanceState::TrySetReady(
	const FString& RequesterMemberId,
	const bool bReady,
	const FSPPartyActionRequest& Request)
{
	const FString Fingerprint = FString::Printf(
		TEXT("ready|%lld|%s|%d"),
		Request.ExpectedRevision,
		*RequesterMemberId,
		bReady ? 1 : 0);
	const ESPPartyActionResult RequestResult =
		CheckRequest(Request, Fingerprint);
	if (RequestResult != ESPPartyActionResult::Success)
	{
		return RequestResult;
	}
	if (!IsStructurallyValid())
	{
		return ESPPartyActionResult::InvalidState;
	}
	if (Request.ExpectedRevision != Revision)
	{
		return ESPPartyActionResult::RevisionMismatch;
	}
	if (!IsHubPhase(RunPhase))
	{
		return ESPPartyActionResult::PhaseLocked;
	}

	FSPPartyMemberState* Member = FindMutableMember(RequesterMemberId);
	if (!Member)
	{
		return ESPPartyActionResult::MemberNotFound;
	}
	if (!Member->IsVotingEligible())
	{
		return ESPPartyActionResult::NotEligible;
	}
	if (Member->bReady == bReady)
	{
		RecordRequest(Request, Fingerprint);
		return ESPPartyActionResult::Success;
	}
	if (Revision == MAX_int64)
	{
		return ESPPartyActionResult::ArithmeticOverflow;
	}

	Member->bReady = bReady;
	RecordRequest(Request, Fingerprint);
	AdvanceRevision();
	return ESPPartyActionResult::Success;
}

ESPPartyActionResult FSPPartyGovernanceState::TrySetRunPhase(
	const ESPRunPhase RequestedPhase,
	const FSPPartyActionRequest& Request)
{
	const FString Fingerprint = FString::Printf(
		TEXT("phase|%lld|%d"),
		Request.ExpectedRevision,
		static_cast<int32>(RequestedPhase));
	const ESPPartyActionResult RequestResult =
		CheckRequest(Request, Fingerprint);
	if (RequestResult != ESPPartyActionResult::Success)
	{
		return RequestResult;
	}
	if (!IsStructurallyValid()
		|| !SPPartyGovernance::IsKnownRunPhase(RequestedPhase))
	{
		return ESPPartyActionResult::InvalidState;
	}
	if (Request.ExpectedRevision != Revision)
	{
		return ESPPartyActionResult::RevisionMismatch;
	}
	if (RunPhase == RequestedPhase)
	{
		RecordRequest(Request, Fingerprint);
		return ESPPartyActionResult::Success;
	}
	if (!SPCanAdvanceRunPhase(RunPhase, RequestedPhase))
	{
		return ESPPartyActionResult::InvalidRequest;
	}
	if (Revision == MAX_int64)
	{
		return ESPPartyActionResult::ArithmeticOverflow;
	}

	RunPhase = RequestedPhase;
	if (!IsHubPhase(RunPhase))
	{
		for (FSPPartyMemberState& Member : Members)
		{
			Member.bReady = false;
		}
	}
	for (FSPPartyVoteState& Vote : Votes)
	{
		if (Vote.Status != ESPPartyVoteStatus::Active)
		{
			continue;
		}
		const bool bStillAllowed =
			(Vote.Kind == ESPPartyVoteKind::KickMember
				&& IsFieldKickVotePhase(RunPhase))
			|| (Vote.Kind == ESPPartyVoteKind::ImportantCraft
				&& IsHubPhase(RunPhase));
		if (!bStillAllowed)
		{
			Vote.Status = ESPPartyVoteStatus::Rejected;
		}
	}
	RecordRequest(Request, Fingerprint);
	AdvanceRevision();
	return ESPPartyActionResult::Success;
}

ESPPartyActionResult FSPPartyGovernanceState::TryHostKickImmediately(
	const FString& RequesterMemberId,
	const FString& TargetMemberId,
	const FSPPartyActionRequest& Request)
{
	const FString Fingerprint = FString::Printf(
		TEXT("hostkick|%lld|%s|%s"),
		Request.ExpectedRevision,
		*RequesterMemberId,
		*TargetMemberId);
	const ESPPartyActionResult RequestResult =
		CheckRequest(Request, Fingerprint);
	if (RequestResult != ESPPartyActionResult::Success)
	{
		return RequestResult;
	}
	if (!IsStructurallyValid())
	{
		return ESPPartyActionResult::InvalidState;
	}
	if (Request.ExpectedRevision != Revision)
	{
		return ESPPartyActionResult::RevisionMismatch;
	}
	if (!IsHubPhase(RunPhase))
	{
		return ESPPartyActionResult::PhaseLocked;
	}

	const FSPPartyMemberState* Requester = FindMember(RequesterMemberId);
	if (!Requester || !Requester->IsVotingEligible())
	{
		return ESPPartyActionResult::NotEligible;
	}
	if (!Requester->bIsHost)
	{
		return ESPPartyActionResult::NotHost;
	}
	if (RequesterMemberId == TargetMemberId)
	{
		return ESPPartyActionResult::TargetIsSelf;
	}

	FSPPartyMemberState* Target = FindMutableMember(TargetMemberId);
	if (!Target)
	{
		return ESPPartyActionResult::MemberNotFound;
	}
	if (!Target->bPresent || Target->bKicked)
	{
		return ESPPartyActionResult::MemberKicked;
	}
	if (Revision == MAX_int64)
	{
		return ESPPartyActionResult::ArithmeticOverflow;
	}

	ApplyKick(*Target);
	EvaluateAllActiveVotes();
	RecordRequest(Request, Fingerprint);
	AdvanceRevision();
	return ESPPartyActionResult::Success;
}

ESPPartyActionResult FSPPartyGovernanceState::TryStartKickVote(
	const FString& ProposerMemberId,
	const FString& TargetMemberId,
	const FGuid& VoteId,
	const double ServerTime,
	const FSPPartyActionRequest& Request)
{
	const FString Fingerprint = FString::Printf(
		TEXT("startkickvote|%lld|%s|%s|%s"),
		Request.ExpectedRevision,
		*ProposerMemberId,
		*TargetMemberId,
		*VoteId.ToString(EGuidFormats::Digits));
	const ESPPartyActionResult RequestResult =
		CheckRequest(Request, Fingerprint);
	if (RequestResult != ESPPartyActionResult::Success)
	{
		return RequestResult;
	}
	if (!IsStructurallyValid()
		|| !VoteId.IsValid()
		|| !SPPartyGovernance::IsFiniteNonNegativeTime(ServerTime))
	{
		return ESPPartyActionResult::InvalidRequest;
	}
	if (Request.ExpectedRevision != Revision)
	{
		return ESPPartyActionResult::RevisionMismatch;
	}
	if (!IsFieldKickVotePhase(RunPhase))
	{
		return ESPPartyActionResult::PhaseLocked;
	}
	if (ProposerMemberId == TargetMemberId)
	{
		return ESPPartyActionResult::TargetIsSelf;
	}

	const FSPPartyMemberState* Proposer = FindMember(ProposerMemberId);
	const FSPPartyMemberState* Target = FindMember(TargetMemberId);
	if (!Proposer || !Proposer->IsVotingEligible())
	{
		return ESPPartyActionResult::NotEligible;
	}
	if (!Target)
	{
		return ESPPartyActionResult::MemberNotFound;
	}
	if (!Target->bPresent || Target->bKicked)
	{
		return ESPPartyActionResult::MemberKicked;
	}
	if (FindVote(VoteId))
	{
		return ESPPartyActionResult::VoteConflict;
	}
	if (Votes.ContainsByPredicate(
		[&TargetMemberId](const FSPPartyVoteState& Vote)
		{
			return Vote.Status == ESPPartyVoteStatus::Active
				&& Vote.Kind == ESPPartyVoteKind::KickMember
				&& Vote.TargetMemberId == TargetMemberId;
		}))
	{
		return ESPPartyActionResult::ActiveVoteExists;
	}
	if (Revision == MAX_int64 || !MakeRoomForVote())
	{
		return ESPPartyActionResult::ArithmeticOverflow;
	}

	FSPPartyVoteState& Vote = Votes.AddDefaulted_GetRef();
	Vote.VoteId = VoteId;
	Vote.Kind = ESPPartyVoteKind::KickMember;
	Vote.ProposerMemberId = ProposerMemberId;
	Vote.TargetMemberId = TargetMemberId;
	Vote.OpenedServerTime = ServerTime;
	Vote.ExpiresServerTime = ServerTime + VoteDurationSeconds;
	FSPPartyBallot ProposerBallot;
	ProposerBallot.VoterMemberId = ProposerMemberId;
	ProposerBallot.Choice = ESPPartyVoteChoice::Approve;
	Vote.Ballots.Add(ProposerBallot);
	EvaluateVote(Vote);
	RecordRequest(Request, Fingerprint);
	AdvanceRevision();
	return ESPPartyActionResult::Success;
}

ESPPartyActionResult FSPPartyGovernanceState::TryStartImportantCraftVote(
	const FString& ProposerMemberId,
	const FPrimaryAssetId& ProposalId,
	const FGuid& VoteId,
	const double ServerTime,
	const FSPPartyActionRequest& Request)
{
	const FString Fingerprint = FString::Printf(
		TEXT("startcraftvote|%lld|%s|%s|%s"),
		Request.ExpectedRevision,
		*ProposerMemberId,
		*ProposalId.ToString(),
		*VoteId.ToString(EGuidFormats::Digits));
	const ESPPartyActionResult RequestResult =
		CheckRequest(Request, Fingerprint);
	if (RequestResult != ESPPartyActionResult::Success)
	{
		return RequestResult;
	}
	if (!IsStructurallyValid()
		|| !ProposalId.IsValid()
		|| !VoteId.IsValid()
		|| !SPPartyGovernance::IsFiniteNonNegativeTime(ServerTime))
	{
		return ESPPartyActionResult::InvalidRequest;
	}
	if (Request.ExpectedRevision != Revision)
	{
		return ESPPartyActionResult::RevisionMismatch;
	}
	if (!IsHubPhase(RunPhase))
	{
		return ESPPartyActionResult::PhaseLocked;
	}

	const FSPPartyMemberState* Proposer = FindMember(ProposerMemberId);
	if (!Proposer || !Proposer->IsVotingEligible())
	{
		return ESPPartyActionResult::NotEligible;
	}
	if (FindVote(VoteId))
	{
		return ESPPartyActionResult::VoteConflict;
	}
	if (Votes.ContainsByPredicate(
		[&ProposalId](const FSPPartyVoteState& Vote)
		{
			return Vote.Status == ESPPartyVoteStatus::Active
				&& Vote.Kind == ESPPartyVoteKind::ImportantCraft
				&& Vote.CraftProposalId == ProposalId;
		}))
	{
		return ESPPartyActionResult::ActiveVoteExists;
	}
	if (Revision == MAX_int64 || !MakeRoomForVote())
	{
		return ESPPartyActionResult::ArithmeticOverflow;
	}

	FSPPartyVoteState& Vote = Votes.AddDefaulted_GetRef();
	Vote.VoteId = VoteId;
	Vote.Kind = ESPPartyVoteKind::ImportantCraft;
	Vote.ProposerMemberId = ProposerMemberId;
	Vote.CraftProposalId = ProposalId;
	Vote.OpenedServerTime = ServerTime;
	Vote.ExpiresServerTime = ServerTime + VoteDurationSeconds;
	FSPPartyBallot ProposerBallot;
	ProposerBallot.VoterMemberId = ProposerMemberId;
	ProposerBallot.Choice = ESPPartyVoteChoice::Approve;
	Vote.Ballots.Add(ProposerBallot);
	EvaluateVote(Vote);
	RecordRequest(Request, Fingerprint);
	AdvanceRevision();
	return ESPPartyActionResult::Success;
}

ESPPartyActionResult FSPPartyGovernanceState::TryCastVote(
	const FString& VoterMemberId,
	const FGuid& VoteId,
	const ESPPartyVoteChoice Choice,
	const double ServerTime,
	const FSPPartyActionRequest& Request)
{
	const FString Fingerprint = FString::Printf(
		TEXT("castvote|%lld|%s|%s|%d"),
		Request.ExpectedRevision,
		*VoterMemberId,
		*VoteId.ToString(EGuidFormats::Digits),
		static_cast<int32>(Choice));
	const ESPPartyActionResult RequestResult =
		CheckRequest(Request, Fingerprint);
	if (RequestResult != ESPPartyActionResult::Success)
	{
		return RequestResult;
	}
	if (!IsStructurallyValid()
		|| !VoteId.IsValid()
		|| !SPPartyGovernance::IsKnownVoteChoice(Choice)
		|| !SPPartyGovernance::IsFiniteNonNegativeTime(ServerTime))
	{
		return ESPPartyActionResult::InvalidRequest;
	}

	FSPPartyVoteState* Vote = FindMutableVote(VoteId);
	if (!Vote)
	{
		return ESPPartyActionResult::VoteNotFound;
	}
	if (const FSPPartyBallot* ExistingBallot =
		Vote->Ballots.FindByPredicate(
			[&VoterMemberId](const FSPPartyBallot& Ballot)
			{
				return Ballot.VoterMemberId == VoterMemberId;
			}))
	{
		if (ExistingBallot->Choice != Choice)
		{
			return ESPPartyActionResult::VoteConflict;
		}
		RecordRequest(Request, Fingerprint);
		return ESPPartyActionResult::AlreadyProcessed;
	}
	if (Request.ExpectedRevision != Revision)
	{
		return ESPPartyActionResult::RevisionMismatch;
	}
	if (Vote->Status != ESPPartyVoteStatus::Active)
	{
		return ESPPartyActionResult::VoteClosed;
	}
	if (ServerTime >= Vote->ExpiresServerTime)
	{
		if (Revision == MAX_int64)
		{
			return ESPPartyActionResult::ArithmeticOverflow;
		}
		Vote->Status = ESPPartyVoteStatus::Rejected;
		AdvanceRevision();
		return ESPPartyActionResult::VoteClosed;
	}

	const FSPPartyMemberState* Voter = FindMember(VoterMemberId);
	if (!Voter || !Voter->IsVotingEligible()
		|| (Vote->Kind == ESPPartyVoteKind::KickMember
			&& Vote->TargetMemberId == VoterMemberId))
	{
		return ESPPartyActionResult::NotEligible;
	}
	if (Revision == MAX_int64)
	{
		return ESPPartyActionResult::ArithmeticOverflow;
	}

	FSPPartyBallot Ballot;
	Ballot.VoterMemberId = VoterMemberId;
	Ballot.Choice = Choice;
	Vote->Ballots.Add(Ballot);
	EvaluateVote(*Vote);
	RecordRequest(Request, Fingerprint);
	AdvanceRevision();
	return ESPPartyActionResult::Success;
}

ESPPartyActionResult FSPPartyGovernanceState::TrySubmitChat(
	const FString& SenderMemberId,
	const FString& Message,
	const double ServerTime,
	const FSPPartyActionRequest& Request)
{
	const FString NormalizedMessage = NormalizeChatMessage(Message);
	const FString Fingerprint = FString::Printf(
		TEXT("chat|%lld|%s|%s"),
		Request.ExpectedRevision,
		*SenderMemberId,
		*NormalizedMessage);
	const ESPPartyActionResult RequestResult =
		CheckRequest(Request, Fingerprint);
	if (RequestResult != ESPPartyActionResult::Success)
	{
		return RequestResult;
	}
	if (!IsStructurallyValid()
		|| !SPPartyGovernance::IsFiniteNonNegativeTime(ServerTime)
		|| NormalizedMessage.IsEmpty())
	{
		return ESPPartyActionResult::InvalidRequest;
	}
	if (NormalizedMessage.Len() > MaximumChatCharacters)
	{
		return ESPPartyActionResult::MessageTooLong;
	}
	if (Request.ExpectedRevision != Revision)
	{
		return ESPPartyActionResult::RevisionMismatch;
	}
	if (!IsHubPhase(RunPhase))
	{
		return ESPPartyActionResult::PhaseLocked;
	}

	FSPPartyMemberState* Sender = FindMutableMember(SenderMemberId);
	if (!Sender || !Sender->IsVotingEligible())
	{
		return ESPPartyActionResult::NotEligible;
	}
	if (Sender->LastChatServerTime >= 0.0
		&& ServerTime - Sender->LastChatServerTime
			< ChatCooldownSeconds)
	{
		return ESPPartyActionResult::RateLimited;
	}
	if (Revision == MAX_int64 || LastChatSequence == MAX_int64)
	{
		return ESPPartyActionResult::ArithmeticOverflow;
	}

	while (ChatMessages.Num() >= ChatRingCapacity)
	{
		ChatMessages.RemoveAt(0);
	}
	FSPPartyChatMessage& ChatMessage =
		ChatMessages.AddDefaulted_GetRef();
	ChatMessage.Sequence = ++LastChatSequence;
	ChatMessage.ServerTimestamp = ServerTime;
	ChatMessage.SenderMemberId = SenderMemberId;
	ChatMessage.Message = NormalizedMessage;
	Sender->LastChatServerTime = ServerTime;
	RecordRequest(Request, Fingerprint);
	AdvanceRevision();
	return ESPPartyActionResult::Success;
}

bool FSPPartyGovernanceState::RefreshExpiredVotes(const double ServerTime)
{
	if (!SPPartyGovernance::IsFiniteNonNegativeTime(ServerTime)
		|| Revision == MAX_int64)
	{
		return false;
	}

	bool bChanged = false;
	for (FSPPartyVoteState& Vote : Votes)
	{
		if (Vote.Status == ESPPartyVoteStatus::Active
			&& ServerTime >= Vote.ExpiresServerTime)
		{
			Vote.Status = ESPPartyVoteStatus::Rejected;
			bChanged = true;
		}
	}
	if (bChanged)
	{
		AdvanceRevision();
	}
	return bChanged;
}

bool FSPPartyGovernanceState::IsHubPhase(const ESPRunPhase Phase)
{
	return Phase == ESPRunPhase::Preparing
		|| Phase == ESPRunPhase::Settlement;
}

bool FSPPartyGovernanceState::IsFieldKickVotePhase(
	const ESPRunPhase Phase)
{
	return Phase == ESPRunPhase::Expedition
		|| Phase == ESPRunPhase::Collapse;
}

FString FSPPartyGovernanceState::NormalizeChatMessage(
	const FString& Message)
{
	FString Normalized = Message;
	Normalized.ReplaceInline(TEXT("\r"), TEXT(" "));
	Normalized.ReplaceInline(TEXT("\n"), TEXT(" "));
	Normalized.ReplaceInline(TEXT("\t"), TEXT(" "));
	Normalized.TrimStartAndEndInline();
	return Normalized;
}

FSPPartyMemberState* FSPPartyGovernanceState::FindMutableMember(
	const FString& MemberId)
{
	return Members.FindByPredicate(
		[&MemberId](const FSPPartyMemberState& Member)
		{
			return Member.MemberId == MemberId;
		});
}

FSPPartyVoteState* FSPPartyGovernanceState::FindMutableVote(
	const FGuid& VoteId)
{
	return VoteId.IsValid()
		? Votes.FindByPredicate(
			[&VoteId](const FSPPartyVoteState& Vote)
			{
				return Vote.VoteId == VoteId;
			})
		: nullptr;
}

ESPPartyActionResult FSPPartyGovernanceState::CheckRequest(
	const FSPPartyActionRequest& Request,
	const FString& Fingerprint) const
{
	if (!Request.RequestId.IsValid()
		|| Request.ExpectedRevision < 0
		|| Fingerprint.IsEmpty())
	{
		return ESPPartyActionResult::InvalidRequest;
	}

	if (const FRequestReceipt* Receipt =
		ProcessedRequests.FindByPredicate(
			[&Request](const FRequestReceipt& Candidate)
			{
				return Candidate.RequestId == Request.RequestId;
			}))
	{
		return Receipt->Fingerprint == Fingerprint
			? ESPPartyActionResult::AlreadyProcessed
			: ESPPartyActionResult::VoteConflict;
	}
	return ESPPartyActionResult::Success;
}

void FSPPartyGovernanceState::RecordRequest(
	const FSPPartyActionRequest& Request,
	FString Fingerprint)
{
	FRequestReceipt& Receipt = ProcessedRequests.AddDefaulted_GetRef();
	Receipt.RequestId = Request.RequestId;
	Receipt.Fingerprint = MoveTemp(Fingerprint);
}

void FSPPartyGovernanceState::ApplyKick(
	FSPPartyMemberState& Target)
{
	Target.bPresent = false;
	Target.bConnected = false;
	Target.bReady = false;
	Target.bKicked = true;
}

void FSPPartyGovernanceState::EvaluateVote(
	FSPPartyVoteState& Vote)
{
	if (Vote.Status != ESPPartyVoteStatus::Active)
	{
		return;
	}

	if (Vote.Kind == ESPPartyVoteKind::KickMember)
	{
		const FSPPartyMemberState* Target =
			FindMember(Vote.TargetMemberId);
		if (!Target || !Target->bPresent || Target->bKicked)
		{
			Vote.Status = ESPPartyVoteStatus::Rejected;
			return;
		}
	}

	TSet<FString> EligibleVoterIds;
	for (const FSPPartyMemberState& Member : Members)
	{
		if (Member.IsVotingEligible()
			&& (Vote.Kind != ESPPartyVoteKind::KickMember
				|| Member.MemberId != Vote.TargetMemberId))
		{
			EligibleVoterIds.Add(Member.MemberId);
		}
	}
	if (EligibleVoterIds.IsEmpty())
	{
		Vote.Status = ESPPartyVoteStatus::Rejected;
		return;
	}

	int32 ApproveCount = 0;
	int32 RejectCount = 0;
	for (const FSPPartyBallot& Ballot : Vote.Ballots)
	{
		if (!EligibleVoterIds.Contains(Ballot.VoterMemberId))
		{
			continue;
		}
		if (Ballot.Choice == ESPPartyVoteChoice::Approve)
		{
			++ApproveCount;
		}
		else
		{
			++RejectCount;
		}
	}

	const int32 EligibleCount = EligibleVoterIds.Num();
	const int32 RequiredApprovals = EligibleCount / 2 + 1;
	if (ApproveCount >= RequiredApprovals)
	{
		Vote.Status = ESPPartyVoteStatus::Passed;
		if (Vote.Kind == ESPPartyVoteKind::KickMember)
		{
			if (FSPPartyMemberState* Target =
				FindMutableMember(Vote.TargetMemberId))
			{
				ApplyKick(*Target);
			}
		}
		return;
	}

	const int32 CastCount = ApproveCount + RejectCount;
	const int32 RemainingCount =
		FMath::Max(0, EligibleCount - CastCount);
	if (RejectCount >= RequiredApprovals
		|| ApproveCount + RemainingCount < RequiredApprovals)
	{
		Vote.Status = ESPPartyVoteStatus::Rejected;
	}
}

void FSPPartyGovernanceState::EvaluateAllActiveVotes()
{
	for (FSPPartyVoteState& Vote : Votes)
	{
		EvaluateVote(Vote);
	}
}

bool FSPPartyGovernanceState::MakeRoomForVote()
{
	while (Votes.Num() >= RetainedVoteCapacity)
	{
		const int32 ClosedVoteIndex = Votes.IndexOfByPredicate(
			[](const FSPPartyVoteState& Vote)
			{
				return Vote.Status != ESPPartyVoteStatus::Active;
			});
		if (ClosedVoteIndex == INDEX_NONE)
		{
			return false;
		}
		Votes.RemoveAt(ClosedVoteIndex);
	}
	return true;
}

void FSPPartyGovernanceState::AdvanceRevision()
{
	check(Revision < MAX_int64);
	++Revision;
}
