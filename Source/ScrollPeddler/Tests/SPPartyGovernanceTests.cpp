#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/SPPartyGovernanceTypes.h"
#include "Game/SPPartyState.h"

namespace SPPartyGovernanceTests
{
	FSPPartyActionRequest MakeRequest(
		const FSPPartyGovernanceState& State)
	{
		FSPPartyActionRequest Request;
		Request.RequestId = FGuid::NewGuid();
		Request.ExpectedRevision = State.Revision;
		return Request;
	}

	bool RegisterMember(
		FSPPartyGovernanceState& State,
		const FString& MemberId)
	{
		return State.TryRegisterMember(
			MemberId,
			MemberId,
			MakeRequest(State))
			== ESPPartyActionResult::Success;
	}

	bool AdvanceToExpedition(FSPPartyGovernanceState& State)
	{
		return State.TrySetRunPhase(
			ESPRunPhase::Expedition,
			MakeRequest(State))
			== ESPPartyActionResult::Success;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPPartyRosterReadinessTest,
	"ScrollPeddler.Party.RosterReadinessAndJoinLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPPartyRosterReadinessTest::RunTest(const FString& Parameters)
{
	FSPPartyGovernanceState State;
	TestTrue(TEXT("Host initializes a party"), State.Initialize(TEXT("Host"), TEXT("Host")));
	TestTrue(TEXT("Second member joins"), SPPartyGovernanceTests::RegisterMember(State, TEXT("MemberB")));
	TestTrue(TEXT("Third member joins"), SPPartyGovernanceTests::RegisterMember(State, TEXT("MemberC")));
	TestTrue(TEXT("Fourth member joins"), SPPartyGovernanceTests::RegisterMember(State, TEXT("MemberD")));
	TestEqual(TEXT("Party contains four present members"), State.GetPresentMemberCount(), 4);
	TestFalse(TEXT("Full party cannot accept late join"), State.IsLateJoinAllowed());
	TestFalse(TEXT("Full party cannot issue another invite"), State.AreInvitesAllowed());

	TestEqual(
		TEXT("Fifth member is rejected"),
		State.TryRegisterMember(
			TEXT("MemberE"),
			TEXT("MemberE"),
			SPPartyGovernanceTests::MakeRequest(State)),
		ESPPartyActionResult::PartyFull);

	for (const FString& MemberId :
		{FString(TEXT("Host")), FString(TEXT("MemberB")),
		 FString(TEXT("MemberC")), FString(TEXT("MemberD"))})
	{
		TestEqual(
			*FString::Printf(TEXT("%s can become ready"), *MemberId),
			State.TrySetReady(
				MemberId,
				true,
				SPPartyGovernanceTests::MakeRequest(State)),
			ESPPartyActionResult::Success);
	}
	TestTrue(TEXT("All four connected members are ready"), State.AreAllPresentMembersReady());

	TestTrue(TEXT("Party advances to expedition"), SPPartyGovernanceTests::AdvanceToExpedition(State));
	TestFalse(TEXT("Ready flags reset outside the hub"), State.AreAllPresentMembersReady());
	TestFalse(TEXT("Expedition rejects late join"), State.IsLateJoinAllowed());
	TestFalse(TEXT("Expedition rejects invites"), State.AreInvitesAllowed());
	TestEqual(
		TEXT("New roster member cannot join an expedition"),
		State.TryRegisterMember(
			TEXT("LateMember"),
			TEXT("LateMember"),
			SPPartyGovernanceTests::MakeRequest(State)),
		ESPPartyActionResult::PhaseLocked);

	TestEqual(
		TEXT("Existing roster member may disconnect"),
		State.TrySetMemberConnected(
			TEXT("MemberB"),
			false,
			SPPartyGovernanceTests::MakeRequest(State)),
		ESPPartyActionResult::Success);
	TestEqual(
		TEXT("Existing roster member may reconnect during expedition"),
		State.TrySetMemberConnected(
			TEXT("MemberB"),
			true,
			SPPartyGovernanceTests::MakeRequest(State)),
		ESPPartyActionResult::Success);
	TestTrue(TEXT("Reconnect path preserves a valid state"), State.IsStructurallyValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPPartyHostKickPolicyTest,
	"ScrollPeddler.Party.HostKickIsHubOnlyAndIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPPartyHostKickPolicyTest::RunTest(const FString& Parameters)
{
	FSPPartyGovernanceState State;
	TestTrue(TEXT("Host initializes party"), State.Initialize(TEXT("Host"), TEXT("Host")));
	TestTrue(TEXT("Guest joins"), SPPartyGovernanceTests::RegisterMember(State, TEXT("Guest")));

	TestEqual(
		TEXT("Non-host cannot immediately kick"),
		State.TryHostKickImmediately(
			TEXT("Guest"),
			TEXT("Host"),
			SPPartyGovernanceTests::MakeRequest(State)),
		ESPPartyActionResult::NotHost);

	const FSPPartyActionRequest KickRequest =
		SPPartyGovernanceTests::MakeRequest(State);
	TestEqual(
		TEXT("Host immediately kicks a guest in hub"),
		State.TryHostKickImmediately(
			TEXT("Host"),
			TEXT("Guest"),
			KickRequest),
		ESPPartyActionResult::Success);
	TestEqual(
		TEXT("Exact kick retry is idempotent"),
		State.TryHostKickImmediately(
			TEXT("Host"),
			TEXT("Guest"),
			KickRequest),
		ESPPartyActionResult::AlreadyProcessed);
	const FSPPartyMemberState* Guest = State.FindMember(TEXT("Guest"));
	TestNotNull(TEXT("Kicked member record remains available"), Guest);
	if (Guest)
	{
		TestTrue(TEXT("Guest is marked kicked"), Guest->bKicked);
		TestFalse(TEXT("Kicked guest is no longer present"), Guest->bPresent);
	}

	FSPPartyGovernanceState ExpeditionState;
	TestTrue(TEXT("Second party initializes"), ExpeditionState.Initialize(TEXT("Host"), TEXT("Host")));
	TestTrue(TEXT("Second guest joins"), SPPartyGovernanceTests::RegisterMember(ExpeditionState, TEXT("Guest")));
	TestTrue(TEXT("Second party enters expedition"), SPPartyGovernanceTests::AdvanceToExpedition(ExpeditionState));
	TestEqual(
		TEXT("Host immediate kick is locked in expedition"),
		ExpeditionState.TryHostKickImmediately(
			TEXT("Host"),
			TEXT("Guest"),
			SPPartyGovernanceTests::MakeRequest(ExpeditionState)),
		ESPPartyActionResult::PhaseLocked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPPartyKickVoteMajorityTest,
	"ScrollPeddler.Party.KickVoteExcludesTargetAndRejectsTie",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPPartyKickVoteMajorityTest::RunTest(const FString& Parameters)
{
	FSPPartyGovernanceState State;
	TestTrue(TEXT("Party initializes"), State.Initialize(TEXT("Host"), TEXT("Host")));
	TestTrue(TEXT("Member B joins"), SPPartyGovernanceTests::RegisterMember(State, TEXT("MemberB")));
	TestTrue(TEXT("Member C joins"), SPPartyGovernanceTests::RegisterMember(State, TEXT("MemberC")));
	TestTrue(TEXT("Target joins"), SPPartyGovernanceTests::RegisterMember(State, TEXT("Target")));
	TestTrue(TEXT("Party enters expedition"), SPPartyGovernanceTests::AdvanceToExpedition(State));

	const FGuid VoteId = FGuid::NewGuid();
	TestEqual(
		TEXT("Eligible member starts a kick vote"),
		State.TryStartKickVote(
			TEXT("Host"),
			TEXT("Target"),
			VoteId,
			10.0,
			SPPartyGovernanceTests::MakeRequest(State)),
		ESPPartyActionResult::Success);
	TestEqual(
		TEXT("Target cannot vote in its own kick vote"),
		State.TryCastVote(
			TEXT("Target"),
			VoteId,
			ESPPartyVoteChoice::Reject,
			11.0,
			SPPartyGovernanceTests::MakeRequest(State)),
		ESPPartyActionResult::NotEligible);

	const FSPPartyActionRequest MemberBVote =
		SPPartyGovernanceTests::MakeRequest(State);
	TestEqual(
		TEXT("Second approval reaches strict majority"),
		State.TryCastVote(
			TEXT("MemberB"),
			VoteId,
			ESPPartyVoteChoice::Approve,
			12.0,
			MemberBVote),
		ESPPartyActionResult::Success);
	const FSPPartyVoteState* PassedVote = State.FindVote(VoteId);
	TestNotNull(TEXT("Passed kick vote remains visible"), PassedVote);
	if (PassedVote)
	{
		TestEqual(TEXT("Kick vote passes"), PassedVote->Status, ESPPartyVoteStatus::Passed);
	}
	const FSPPartyMemberState* Target = State.FindMember(TEXT("Target"));
	TestTrue(TEXT("Passed vote kicks target"), Target && Target->bKicked);

	FSPPartyActionRequest DuplicateBallot =
		SPPartyGovernanceTests::MakeRequest(State);
	TestEqual(
		TEXT("Duplicate same-choice ballot is idempotent even after close"),
		State.TryCastVote(
			TEXT("MemberB"),
			VoteId,
			ESPPartyVoteChoice::Approve,
			13.0,
			DuplicateBallot),
		ESPPartyActionResult::AlreadyProcessed);

	FSPPartyGovernanceState TieState;
	TestTrue(TEXT("Tie party initializes"), TieState.Initialize(TEXT("Host"), TEXT("Host")));
	TestTrue(TEXT("Tie voter joins"), SPPartyGovernanceTests::RegisterMember(TieState, TEXT("Voter")));
	TestTrue(TEXT("Tie target joins"), SPPartyGovernanceTests::RegisterMember(TieState, TEXT("Target")));
	TestTrue(TEXT("Tie party enters expedition"), SPPartyGovernanceTests::AdvanceToExpedition(TieState));
	const FGuid TieVoteId = FGuid::NewGuid();
	TestEqual(
		TEXT("Two-voter kick vote starts with proposer approval"),
		TieState.TryStartKickVote(
			TEXT("Host"),
			TEXT("Target"),
			TieVoteId,
			20.0,
			SPPartyGovernanceTests::MakeRequest(TieState)),
		ESPPartyActionResult::Success);
	TestEqual(
		TEXT("One approval and one rejection closes as rejection"),
		TieState.TryCastVote(
			TEXT("Voter"),
			TieVoteId,
			ESPPartyVoteChoice::Reject,
			21.0,
			SPPartyGovernanceTests::MakeRequest(TieState)),
		ESPPartyActionResult::Success);
	const FSPPartyVoteState* TieVote = TieState.FindVote(TieVoteId);
	TestTrue(
		TEXT("Tie is rejected"),
		TieVote && TieVote->Status == ESPPartyVoteStatus::Rejected);
	const FSPPartyMemberState* TieTarget = TieState.FindMember(TEXT("Target"));
	TestTrue(TEXT("Rejected tie preserves target"), TieTarget && TieTarget->bPresent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPPartyImportantCraftVoteTest,
	"ScrollPeddler.Party.ImportantCraftRequiresHubMajority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPPartyImportantCraftVoteTest::RunTest(const FString& Parameters)
{
	FSPPartyGovernanceState State;
	TestTrue(TEXT("Party initializes"), State.Initialize(TEXT("Host"), TEXT("Host")));
	TestTrue(TEXT("Member B joins"), SPPartyGovernanceTests::RegisterMember(State, TEXT("MemberB")));
	TestTrue(TEXT("Member C joins"), SPPartyGovernanceTests::RegisterMember(State, TEXT("MemberC")));

	const FPrimaryAssetId ProposalId(
		FPrimaryAssetType(TEXT("SPCraftingRecipe")),
		TEXT("DA_ImportantCraft"));
	const FGuid VoteId = FGuid::NewGuid();
	TestEqual(
		TEXT("Important craft vote starts in hub"),
		State.TryStartImportantCraftVote(
			TEXT("Host"),
			ProposalId,
			VoteId,
			100.0,
			SPPartyGovernanceTests::MakeRequest(State)),
		ESPPartyActionResult::Success);
	TestFalse(
		TEXT("Single proposer is not a majority of three"),
		State.HasPassedImportantCraftVote(ProposalId));
	TestEqual(
		TEXT("Second approval passes important craft"),
		State.TryCastVote(
			TEXT("MemberB"),
			VoteId,
			ESPPartyVoteChoice::Approve,
			101.0,
			SPPartyGovernanceTests::MakeRequest(State)),
		ESPPartyActionResult::Success);
	TestTrue(
		TEXT("Passed proposal can authorize workshop quote"),
		State.HasPassedImportantCraftVote(ProposalId));

	FSPPartyGovernanceState ExpeditionState;
	TestTrue(TEXT("Expedition party initializes"), ExpeditionState.Initialize(TEXT("Host"), TEXT("Host")));
	TestTrue(TEXT("Expedition party enters field"), SPPartyGovernanceTests::AdvanceToExpedition(ExpeditionState));
	TestEqual(
		TEXT("Important craft vote cannot start in expedition"),
		ExpeditionState.TryStartImportantCraftVote(
			TEXT("Host"),
			ProposalId,
			FGuid::NewGuid(),
			200.0,
			SPPartyGovernanceTests::MakeRequest(ExpeditionState)),
		ESPPartyActionResult::PhaseLocked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPPartyChatPolicyTest,
	"ScrollPeddler.Party.ChatRingRateLimitAndServerSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPPartyChatPolicyTest::RunTest(const FString& Parameters)
{
	FSPPartyGovernanceState State;
	TestTrue(TEXT("Party initializes"), State.Initialize(TEXT("Host"), TEXT("Host")));

	const FString MaximumMessage =
		FString::ChrN(FSPPartyGovernanceState::MaximumChatCharacters, TCHAR('A'));
	const FSPPartyActionRequest FirstRequest =
		SPPartyGovernanceTests::MakeRequest(State);
	TestEqual(
		TEXT("Exactly 200 characters are accepted"),
		State.TrySubmitChat(
			TEXT("Host"),
			MaximumMessage,
			0.0,
			FirstRequest),
		ESPPartyActionResult::Success);
	TestEqual(TEXT("First chat gets sequence one"), State.LastChatSequence, int64(1));
	TestEqual(
		TEXT("Exact chat retry is idempotent"),
		State.TrySubmitChat(
			TEXT("Host"),
			MaximumMessage,
			0.1,
			FirstRequest),
		ESPPartyActionResult::AlreadyProcessed);
	TestEqual(
		TEXT("Sender cooldown rejects a burst"),
		State.TrySubmitChat(
			TEXT("Host"),
			TEXT("Too soon"),
			0.5,
			SPPartyGovernanceTests::MakeRequest(State)),
		ESPPartyActionResult::RateLimited);
	TestEqual(
		TEXT("Message after one second is accepted"),
		State.TrySubmitChat(
			TEXT("Host"),
			TEXT("Second"),
			1.0,
			SPPartyGovernanceTests::MakeRequest(State)),
		ESPPartyActionResult::Success);

	const FString OversizedMessage =
		FString::ChrN(FSPPartyGovernanceState::MaximumChatCharacters + 1, TCHAR('B'));
	TestEqual(
		TEXT("201 characters are rejected"),
		State.TrySubmitChat(
			TEXT("Host"),
			OversizedMessage,
			2.0,
			SPPartyGovernanceTests::MakeRequest(State)),
		ESPPartyActionResult::MessageTooLong);

	double ServerTime = 2.0;
	for (int32 Index = 0;
		Index < FSPPartyGovernanceState::ChatRingCapacity + 3;
		++Index)
	{
		TestEqual(
			*FString::Printf(TEXT("Ring message %d is accepted"), Index),
			State.TrySubmitChat(
				TEXT("Host"),
				FString::Printf(TEXT("Message %d"), Index),
				ServerTime,
				SPPartyGovernanceTests::MakeRequest(State)),
			ESPPartyActionResult::Success);
		ServerTime += FSPPartyGovernanceState::ChatCooldownSeconds;
	}
	TestEqual(
		TEXT("Chat retains only ring capacity"),
		State.ChatMessages.Num(),
		FSPPartyGovernanceState::ChatRingCapacity);
	TestEqual(
		TEXT("Server sequence counts accepted messages"),
		State.LastChatSequence,
		int64(FSPPartyGovernanceState::ChatRingCapacity + 5));
	TestEqual(
		TEXT("Newest chat carries authoritative sequence"),
		State.ChatMessages.Last().Sequence,
		State.LastChatSequence);
	TestEqual(
		TEXT("Newest chat carries authoritative timestamp"),
		State.ChatMessages.Last().ServerTimestamp,
		ServerTime - FSPPartyGovernanceState::ChatCooldownSeconds);

	TestTrue(TEXT("Party enters expedition"), SPPartyGovernanceTests::AdvanceToExpedition(State));
	TestEqual(
		TEXT("Field text chat is disabled"),
		State.TrySubmitChat(
			TEXT("Host"),
			TEXT("Field message"),
			ServerTime,
			SPPartyGovernanceTests::MakeRequest(State)),
		ESPPartyActionResult::PhaseLocked);
	TestTrue(TEXT("Chat operations preserve structural validity"), State.IsStructurallyValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPPartyStateReplicationContractTest,
	"ScrollPeddler.Party.ReplicatedActorContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPPartyStateReplicationContractTest::RunTest(const FString& Parameters)
{
	const ASPPartyState* PartyStateCDO = GetDefault<ASPPartyState>();
	TestTrue(TEXT("Party state actor replicates"), PartyStateCDO->GetIsReplicated());
	TestTrue(TEXT("Party state is always relevant"), PartyStateCDO->bAlwaysRelevant);
	TestFalse(TEXT("Party state does not replicate movement"), PartyStateCDO->IsReplicatingMovement());
	return true;
}

#endif
