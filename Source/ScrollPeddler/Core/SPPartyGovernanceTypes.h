#pragma once

#include "CoreMinimal.h"
#include "Core/SPRunTypes.h"
#include "UObject/PrimaryAssetId.h"
#include "SPPartyGovernanceTypes.generated.h"

UENUM(BlueprintType)
enum class ESPPartyActionResult : uint8
{
	Success,
	AlreadyProcessed,
	NotAuthority,
	InvalidRequest,
	InvalidState,
	RevisionMismatch,
	PartyFull,
	MemberNotFound,
	MemberExists,
	MemberKicked,
	NotHost,
	PhaseLocked,
	NotEligible,
	TargetIsSelf,
	ActiveVoteExists,
	VoteNotFound,
	VoteClosed,
	VoteConflict,
	RateLimited,
	MessageTooLong,
	ArithmeticOverflow
};

UENUM(BlueprintType)
enum class ESPPartyVoteKind : uint8
{
	KickMember,
	ImportantCraft
};

UENUM(BlueprintType)
enum class ESPPartyVoteChoice : uint8
{
	Approve,
	Reject
};

UENUM(BlueprintType)
enum class ESPPartyVoteStatus : uint8
{
	Active,
	Passed,
	Rejected
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPPartyActionRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGuid RequestId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 ExpectedRevision = 0;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPPartyMemberState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString MemberId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString DisplayName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bIsHost = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bPresent = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bConnected = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bReady = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bKicked = false;

	/** Authority-only anti-spam state; deliberately omitted from replication. */
	double LastChatServerTime = -1.0;

	bool IsVotingEligible() const
	{
		return bPresent && bConnected && !bKicked;
	}
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPPartyBallot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString VoterMemberId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPPartyVoteChoice Choice = ESPPartyVoteChoice::Reject;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPPartyVoteState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGuid VoteId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPPartyVoteKind Kind = ESPPartyVoteKind::KickMember;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString ProposerMemberId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString TargetMemberId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FPrimaryAssetId CraftProposalId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	double OpenedServerTime = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	double ExpiresServerTime = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPPartyVoteStatus Status = ESPPartyVoteStatus::Active;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FSPPartyBallot> Ballots;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPPartyChatMessage
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 Sequence = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	double ServerTimestamp = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString SenderMemberId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString Message;
};

/**
 * Pure, server-owned party state.
 *
 * The replicated actor wraps these deterministic operations with HasAuthority
 * checks. Request receipts and chat rate-limit timestamps remain server-only.
 */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPPartyGovernanceState
{
	GENERATED_BODY()

	static constexpr int32 MaximumPartyMembers = 4;
	static constexpr int32 MaximumMemberIdCharacters = 128;
	static constexpr int32 MaximumDisplayNameCharacters = 64;
	static constexpr int32 MaximumChatCharacters = 200;
	static constexpr int32 ChatRingCapacity = 32;
	static constexpr int32 RetainedVoteCapacity = 32;
	static constexpr double ChatCooldownSeconds = 1.0;
	static constexpr double VoteDurationSeconds = 30.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 Revision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPRunPhase RunPhase = ESPRunPhase::Preparing;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FSPPartyMemberState> Members;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FSPPartyVoteState> Votes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FSPPartyChatMessage> ChatMessages;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 LastChatSequence = 0;

	bool Initialize(const FString& HostMemberId, const FString& HostDisplayName);
	bool IsStructurallyValid() const;

	int32 GetPresentMemberCount() const;
	bool AreAllPresentMembersReady() const;
	bool IsLateJoinAllowed() const;
	bool AreInvitesAllowed() const;
	bool HasPassedImportantCraftVote(const FPrimaryAssetId& ProposalId) const;

	const FSPPartyMemberState* FindMember(const FString& MemberId) const;
	const FSPPartyVoteState* FindVote(const FGuid& VoteId) const;

	ESPPartyActionResult TryRegisterMember(
		const FString& MemberId,
		const FString& DisplayName,
		const FSPPartyActionRequest& Request);

	/** Existing roster reconnect/disconnect; this is not a late join. */
	ESPPartyActionResult TrySetMemberConnected(
		const FString& MemberId,
		bool bConnected,
		const FSPPartyActionRequest& Request);

	ESPPartyActionResult TrySetReady(
		const FString& RequesterMemberId,
		bool bReady,
		const FSPPartyActionRequest& Request);

	ESPPartyActionResult TrySetRunPhase(
		ESPRunPhase RequestedPhase,
		const FSPPartyActionRequest& Request);

	ESPPartyActionResult TryHostKickImmediately(
		const FString& RequesterMemberId,
		const FString& TargetMemberId,
		const FSPPartyActionRequest& Request);

	ESPPartyActionResult TryStartKickVote(
		const FString& ProposerMemberId,
		const FString& TargetMemberId,
		const FGuid& VoteId,
		double ServerTime,
		const FSPPartyActionRequest& Request);

	ESPPartyActionResult TryStartImportantCraftVote(
		const FString& ProposerMemberId,
		const FPrimaryAssetId& ProposalId,
		const FGuid& VoteId,
		double ServerTime,
		const FSPPartyActionRequest& Request);

	ESPPartyActionResult TryCastVote(
		const FString& VoterMemberId,
		const FGuid& VoteId,
		ESPPartyVoteChoice Choice,
		double ServerTime,
		const FSPPartyActionRequest& Request);

	ESPPartyActionResult TrySubmitChat(
		const FString& SenderMemberId,
		const FString& Message,
		double ServerTime,
		const FSPPartyActionRequest& Request);

	/** Rejects active votes whose authoritative deadline has elapsed. */
	bool RefreshExpiredVotes(double ServerTime);

private:
	struct FRequestReceipt
	{
		FGuid RequestId;
		FString Fingerprint;
	};

	TArray<FRequestReceipt> ProcessedRequests;

	static bool IsHubPhase(ESPRunPhase Phase);
	static bool IsFieldKickVotePhase(ESPRunPhase Phase);
	static FString NormalizeChatMessage(const FString& Message);

	FSPPartyMemberState* FindMutableMember(const FString& MemberId);
	FSPPartyVoteState* FindMutableVote(const FGuid& VoteId);

	ESPPartyActionResult CheckRequest(
		const FSPPartyActionRequest& Request,
		const FString& Fingerprint) const;
	void RecordRequest(
		const FSPPartyActionRequest& Request,
		FString Fingerprint);
	void ApplyKick(FSPPartyMemberState& Target);
	void EvaluateVote(FSPPartyVoteState& Vote);
	void EvaluateAllActiveVotes();
	bool MakeRoomForVote();
	void AdvanceRevision();
};
