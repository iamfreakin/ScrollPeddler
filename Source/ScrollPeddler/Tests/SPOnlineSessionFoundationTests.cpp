#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Online/OnlineSessionNames.h"
#include "Online/SPOnlineSessionSubsystem.h"

namespace
{
FSPOnlineLobbySummary MakeCompatibleLobby()
{
	FSPOnlineLobbySummary Lobby;
	Lobby.SearchResultIndex = 0;
	Lobby.MaxPlayers = 4;
	Lobby.OpenPublicConnections = 2;
	Lobby.BuildUniqueId = 7319;
	Lobby.RulesVersion = 1;
	Lobby.LobbyState = ESPOnlineLobbyState::Hub;
	Lobby.bJoinable = true;
	Lobby.bAllowsInvites = true;
	return Lobby;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPOnlineQuickPlayCompatibilityTest,
	"ScrollPeddler.Online.QuickPlayCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPOnlineQuickPlayCompatibilityTest::RunTest(
	const FString& Parameters)
{
	constexpr int32 RequiredBuild = 7319;
	constexpr int32 RequiredRules = 1;

	const FSPOnlineLobbySummary Compatible = MakeCompatibleLobby();
	TestEqual(
		TEXT("Matching Hub lobby with a free slot is compatible"),
		SPEvaluateQuickPlayCompatibility(
			Compatible,
			RequiredBuild,
			RequiredRules),
		ESPOnlineSessionResult::Success);

	FSPOnlineLobbySummary WrongBuild = Compatible;
	WrongBuild.BuildUniqueId = RequiredBuild + 1;
	TestEqual(
		TEXT("Different builds are rejected"),
		SPEvaluateQuickPlayCompatibility(
			WrongBuild,
			RequiredBuild,
			RequiredRules),
		ESPOnlineSessionResult::BuildMismatch);

	FSPOnlineLobbySummary WrongRules = Compatible;
	WrongRules.RulesVersion = RequiredRules + 1;
	TestEqual(
		TEXT("Different rules versions are rejected"),
		SPEvaluateQuickPlayCompatibility(
			WrongRules,
			RequiredBuild,
			RequiredRules),
		ESPOnlineSessionResult::RulesMismatch);

	FSPOnlineLobbySummary Expedition = Compatible;
	Expedition.LobbyState = ESPOnlineLobbyState::Expedition;
	TestEqual(
		TEXT("Expedition lobby is reconnect-only and rejected"),
		SPEvaluateQuickPlayCompatibility(
			Expedition,
			RequiredBuild,
			RequiredRules),
		ESPOnlineSessionResult::RunInProgress);

	FSPOnlineLobbySummary Closed = Compatible;
	Closed.bJoinable = false;
	TestEqual(
		TEXT("Host joinability toggle is enforced"),
		SPEvaluateQuickPlayCompatibility(
			Closed,
			RequiredBuild,
			RequiredRules),
		ESPOnlineSessionResult::NotJoinable);

	FSPOnlineLobbySummary Full = Compatible;
	Full.OpenPublicConnections = 0;
	TestEqual(
		TEXT("Lobby without a public slot is rejected"),
		SPEvaluateQuickPlayCompatibility(
			Full,
			RequiredBuild,
			RequiredRules),
		ESPOnlineSessionResult::SessionFull);

	FSPOnlineLobbySummary Oversized = Compatible;
	Oversized.MaxPlayers = 8;
	TestEqual(
		TEXT("Lobby outside the one-to-four contract is rejected"),
		SPEvaluateQuickPlayCompatibility(
			Oversized,
			RequiredBuild,
			RequiredRules),
		ESPOnlineSessionResult::InvalidRequest);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPOnlineInviteCompatibilityTest,
	"ScrollPeddler.Online.InviteCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPOnlineInviteCompatibilityTest::RunTest(
	const FString& Parameters)
{
	constexpr int32 RequiredBuild = 7319;
	constexpr int32 RequiredRules = 1;

	const FSPOnlineLobbySummary Compatible = MakeCompatibleLobby();
	TestEqual(
		TEXT("Advertised Hub invitation is compatible"),
		SPEvaluateInviteCompatibility(
			Compatible,
			RequiredBuild,
			RequiredRules),
		ESPOnlineSessionResult::Success);

	FSPOnlineLobbySummary InvitesDisabled = Compatible;
	InvitesDisabled.bAllowsInvites = false;
	TestEqual(
		TEXT("Stale invitation is rejected after invitations close"),
		SPEvaluateInviteCompatibility(
			InvitesDisabled,
			RequiredBuild,
			RequiredRules),
		ESPOnlineSessionResult::InvitesDisabled);

	FSPOnlineLobbySummary WrongBuild = Compatible;
	WrongBuild.BuildUniqueId = RequiredBuild + 1;
	TestEqual(
		TEXT("Invitation cannot bypass build compatibility"),
		SPEvaluateInviteCompatibility(
			WrongBuild,
			RequiredBuild,
			RequiredRules),
		ESPOnlineSessionResult::BuildMismatch);

	FSPOnlineLobbySummary WrongRules = Compatible;
	WrongRules.RulesVersion = RequiredRules + 1;
	TestEqual(
		TEXT("Invitation cannot bypass rules compatibility"),
		SPEvaluateInviteCompatibility(
			WrongRules,
			RequiredBuild,
			RequiredRules),
		ESPOnlineSessionResult::RulesMismatch);

	FSPOnlineLobbySummary Expedition = Compatible;
	Expedition.LobbyState = ESPOnlineLobbyState::Expedition;
	TestEqual(
		TEXT("Invitation cannot bypass the expedition phase lock"),
		SPEvaluateInviteCompatibility(
			Expedition,
			RequiredBuild,
			RequiredRules),
		ESPOnlineSessionResult::RunInProgress);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPOnlineBlueprintContractTest,
	"ScrollPeddler.Online.BlueprintContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPOnlineBlueprintContractTest::RunTest(
	const FString& Parameters)
{
	TestNotNull(
		TEXT("Lobby state is reflected"),
		StaticEnum<ESPOnlineLobbyState>());
	TestNotNull(
		TEXT("Operation state is reflected"),
		StaticEnum<ESPOnlineSessionOperation>());
	TestNotNull(
		TEXT("Result state is reflected"),
		StaticEnum<ESPOnlineSessionResult>());
	TestEqual(
		TEXT("Public lobby supports one to four players"),
		USPOnlineSessionSubsystem::MinPublicPlayers,
		1);
	TestEqual(
		TEXT("Public lobby maximum is four"),
		USPOnlineSessionSubsystem::MaxPublicPlayers,
		4);
	TestEqual(
		TEXT("Initial protocol rules version is stable"),
		USPOnlineSessionSubsystem::GetRequiredRulesVersion(),
		1);
	TestFalse(
		TEXT("Legacy session name is explicit"),
		USPOnlineSessionSubsystem::SessionName.IsNone());
	TestEqual(
		TEXT("Legacy session name matches APlayerState registration"),
		USPOnlineSessionSubsystem::SessionName,
		NAME_GameSession);
	return true;
}

#endif
