#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/SPRunTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/SPGameMode.h"
#include "Game/SPGameState.h"
#include "Game/SPPartyState.h"
#include "Game/SPPlayerState.h"
#include "World/SPNoiseSubsystem.h"

namespace
{
class FSPScopedRunTestWorld
{
public:
	FSPScopedRunTestWorld()
	{
		if (!GEngine)
		{
			return;
		}

		const FName WorldName = MakeUniqueObjectName(
			nullptr,
			UWorld::StaticClass(),
			NAME_None,
			EUniqueObjectNameOptions::GloballyUnique);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		World = UWorld::CreateWorld(
			EWorldType::Game,
			false,
			WorldName,
			GetTransientPackage());
		if (!World)
		{
			return;
		}

		World->AddToRoot();
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
	}

	~FSPScopedRunTestWorld()
	{
		if (!World || !GEngine)
		{
			return;
		}

		if (World->AreActorsInitialized())
		{
			for (AActor* Actor : FActorRange(World))
			{
				if (Actor)
				{
					Actor->RouteEndPlay(EEndPlayReason::LevelTransition);
				}
			}
		}
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	UWorld* Get() const
	{
		return World;
	}

private:
	UWorld* World = nullptr;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPRunTransitionPolicyTest,
	"ScrollPeddler.Run.TransitionPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPRunTransitionPolicyTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Expedition lasts 25 minutes"),
		SPGetExpeditionDurationSeconds(), 1500.0);
	TestEqual(TEXT("Collapse lasts 2 minutes"),
		SPGetCollapseDurationSeconds(), 120.0);
	TestEqual(TEXT("First Down has 45 seconds"),
		SPGetBleedoutDurationSeconds(1), 45.0f);
	TestEqual(TEXT("Second Down has 30 seconds"),
		SPGetBleedoutDurationSeconds(2), 30.0f);
	TestEqual(TEXT("Third Down has 15 seconds"),
		SPGetBleedoutDurationSeconds(3), 15.0f);
	TestEqual(TEXT("Later Downs stay at 15 seconds"),
		SPGetBleedoutDurationSeconds(20), 15.0f);

	TestTrue(TEXT("Normal may become Down directly"),
		SPCanTransitionPlayerCondition(
			ESPPlayerCondition::Normal, ESPPlayerCondition::Down));
	TestTrue(TEXT("Down may be stabilized to Injured"),
		SPCanTransitionPlayerCondition(
			ESPPlayerCondition::Down, ESPPlayerCondition::Injured));
	TestFalse(TEXT("Down cannot jump directly to Normal"),
		SPCanTransitionPlayerCondition(
			ESPPlayerCondition::Down, ESPPlayerCondition::Normal));
	TestFalse(TEXT("Missing is terminal"),
		SPCanTransitionPlayerCondition(
			ESPPlayerCondition::Missing, ESPPlayerCondition::Injured));

	TestTrue(TEXT("Disconnected player may reconnect"),
		SPCanTransitionParticipationState(
			ESPParticipationState::Disconnected,
			ESPParticipationState::Active));
	TestFalse(TEXT("Spectator cannot rejoin"),
		SPCanTransitionParticipationState(
			ESPParticipationState::Spectating,
			ESPParticipationState::Active));
	TestTrue(TEXT("Run may skip forward for an abort"),
		SPCanAdvanceRunPhase(
			ESPRunPhase::Expedition, ESPRunPhase::Resolution));
	TestFalse(TEXT("Run phase cannot rewind"),
		SPCanAdvanceRunPhase(
			ESPRunPhase::Collapse, ESPRunPhase::Expedition));
	TestFalse(TEXT("Preparing rejects field gameplay actions"),
		SPAllowsFieldGameplayAction(ESPRunPhase::Preparing));
	TestTrue(TEXT("Expedition accepts field gameplay actions"),
		SPAllowsFieldGameplayAction(ESPRunPhase::Expedition));
	TestTrue(TEXT("Collapse accepts field gameplay actions"),
		SPAllowsFieldGameplayAction(ESPRunPhase::Collapse));
	TestFalse(TEXT("Resolution rejects field gameplay actions"),
		SPAllowsFieldGameplayAction(ESPRunPhase::Resolution));
	TestFalse(TEXT("Settlement rejects field gameplay actions"),
		SPAllowsFieldGameplayAction(ESPRunPhase::Settlement));
	TestFalse(TEXT("Hub participants cannot perform field gameplay actions"),
		SPAllowsPlayerFieldGameplayAction(
			ESPRunPhase::Expedition,
			ESPParticipationState::Hub,
			ESPPlayerCondition::Normal));
	TestTrue(TEXT("Active injured participants retain field gameplay access"),
		SPAllowsPlayerFieldGameplayAction(
			ESPRunPhase::Collapse,
			ESPParticipationState::Active,
			ESPPlayerCondition::Injured));
	TestFalse(TEXT("Missing participants cannot perform field gameplay actions"),
		SPAllowsPlayerFieldGameplayAction(
			ESPRunPhase::Expedition,
			ESPParticipationState::Active,
			ESPPlayerCondition::Missing));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPPlayerRunStateTest,
	"ScrollPeddler.Run.PlayerStateAuthorityTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPPlayerRunStateTest::RunTest(const FString& Parameters)
{
	FSPScopedRunTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Authority test world exists"), World);
	if (!World)
	{
		return false;
	}

	ASPPlayerState* PlayerState = World->SpawnActor<ASPPlayerState>();
	TestNotNull(TEXT("PlayerState spawns"), PlayerState);
	if (!PlayerState)
	{
		return false;
	}

	TestTrue(TEXT("Standalone world owns authority"), PlayerState->HasAuthority());
	TestEqual(TEXT("Player starts healthy"),
		PlayerState->GetPlayerCondition(), ESPPlayerCondition::Normal);
	TestEqual(TEXT("Player starts in hub"),
		PlayerState->GetParticipationState(), ESPParticipationState::Hub);
	TestTrue(TEXT("Player can enter the run"),
		PlayerState->AuthorityTransitionParticipation(
			ESPParticipationState::Active));

	TestTrue(TEXT("First Down applies"),
		PlayerState->AuthorityTransitionCondition(ESPPlayerCondition::Down));
	TestEqual(TEXT("First Down is counted"), PlayerState->GetDownCount(), 1);
	TestEqual(TEXT("First Down duration is exposed"),
		PlayerState->GetBleedoutDurationForCurrentDown(), 45.0f);
	TestTrue(TEXT("First Down has an authority deadline"),
		PlayerState->GetBleedoutEndServerTime() > 0.0);
	TestTrue(TEXT("Repeating Down is idempotent"),
		PlayerState->AuthorityTransitionCondition(ESPPlayerCondition::Down));
	TestEqual(TEXT("Repeated Down does not increment"), PlayerState->GetDownCount(), 1);

	TestTrue(TEXT("First revive returns Injured"),
		PlayerState->AuthorityTransitionCondition(ESPPlayerCondition::Injured));
	TestTrue(TEXT("Second Down applies"),
		PlayerState->AuthorityTransitionCondition(ESPPlayerCondition::Down));
	TestEqual(TEXT("Second Down is counted"), PlayerState->GetDownCount(), 2);
	TestEqual(TEXT("Second Down lasts 30 seconds"),
		PlayerState->GetBleedoutDurationForCurrentDown(), 30.0f);

	TestTrue(TEXT("Second revive returns Injured"),
		PlayerState->AuthorityTransitionCondition(ESPPlayerCondition::Injured));
	TestTrue(TEXT("Third Down applies"),
		PlayerState->AuthorityTransitionCondition(ESPPlayerCondition::Down));
	TestEqual(TEXT("Third Down is counted"), PlayerState->GetDownCount(), 3);
	TestEqual(TEXT("Third Down lasts 15 seconds"),
		PlayerState->GetBleedoutDurationForCurrentDown(), 15.0f);

	TestTrue(TEXT("Extraction commits once"), PlayerState->AuthorityMarkExtracted());
	TestTrue(TEXT("Extraction replay is an idempotent success"),
		PlayerState->AuthorityMarkExtracted());
	TestTrue(TEXT("Legacy extraction flag remains compatible"),
		PlayerState->IsExtracted());
	TestEqual(TEXT("Participation records extraction"),
		PlayerState->GetParticipationState(), ESPParticipationState::Extracted);
	TestTrue(TEXT("Extracted player is terminal"), PlayerState->IsRunTerminal());

	ASPPlayerState* MissingPlayerState = World->SpawnActor<ASPPlayerState>();
	TestNotNull(TEXT("Second PlayerState spawns"), MissingPlayerState);
	if (!MissingPlayerState)
	{
		return false;
	}

	TestTrue(TEXT("Missing transition commits once"),
		MissingPlayerState->AuthorityMarkMissing());
	TestTrue(TEXT("Missing replay is an idempotent success"),
		MissingPlayerState->AuthorityMarkMissing());
	TestEqual(TEXT("Missing condition is terminal"),
		MissingPlayerState->GetPlayerCondition(), ESPPlayerCondition::Missing);
	TestEqual(TEXT("Missing player becomes spectator"),
		MissingPlayerState->GetParticipationState(),
		ESPParticipationState::Spectating);
	AddExpectedError(
		TEXT("SP_RUN_EXTRACTION_REJECTED"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestFalse(TEXT("Missing player cannot extract"),
		MissingPlayerState->AuthorityMarkExtracted());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPReconnectBleedoutDeadlineTest,
	"ScrollPeddler.Run.ReconnectPreservesAbsoluteBleedoutDeadline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPReconnectBleedoutDeadlineTest::RunTest(
	const FString& Parameters)
{
	FSPScopedRunTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Authority test world exists"), World);
	if (!World)
	{
		return false;
	}

	ASPPlayerState* Original = World->SpawnActor<ASPPlayerState>();
	ASPPlayerState* FirstReconnect =
		World->SpawnActor<ASPPlayerState>();
	TestNotNull(TEXT("Original PlayerState spawns"), Original);
	TestNotNull(TEXT("Reconnect PlayerState spawns"), FirstReconnect);
	if (!Original || !FirstReconnect)
	{
		return false;
	}

	TestTrue(TEXT("Original player enters the run"),
		Original->AuthorityTransitionParticipation(
			ESPParticipationState::Active));
	TestTrue(TEXT("Original player becomes Down"),
		Original->AuthorityTransitionCondition(
			ESPPlayerCondition::Down));

	FSPPlayerRunSnapshot FirstSnapshot;
	TestTrue(TEXT("Disconnect captures a valid snapshot"),
		Original->AuthorityBuildReconnectSnapshot(FirstSnapshot));
	const double OriginalDeadline =
		FirstSnapshot.BleedoutDeadlineServerTime;
	TestTrue(TEXT("Down snapshot carries an absolute deadline"),
		OriginalDeadline > 0.0);

	TestTrue(TEXT("First reconnect restores before expiry"),
		FirstReconnect->AuthorityRestoreReconnectSnapshot(
			FirstSnapshot));
	TestEqual(TEXT("Restore keeps the exact original deadline"),
		FirstReconnect->GetBleedoutEndServerTime(),
		OriginalDeadline);

	FSPPlayerRunSnapshot SecondSnapshot;
	TestTrue(TEXT("A repeated disconnect can be captured"),
		FirstReconnect->AuthorityBuildReconnectSnapshot(
			SecondSnapshot));
	TestEqual(TEXT("Repeated reconnect cannot extend bleedout"),
		SecondSnapshot.BleedoutDeadlineServerTime,
		OriginalDeadline);

	FSPPlayerRunSnapshot ExpiredSnapshot = SecondSnapshot;
	ExpiredSnapshot.BleedoutDeadlineServerTime =
		static_cast<double>(World->GetTimeSeconds()) + 0.01;
	TestTrue(TEXT("Near-expiry snapshot remains structurally valid"),
		ExpiredSnapshot.IsStructurallyValid());
	World->Tick(LEVELTICK_All, 0.1f);
	TestTrue(TEXT("Offline elapsed time expires bleedout"),
		ExpiredSnapshot.IsBleedoutExpired(
			static_cast<double>(World->GetTimeSeconds())));

	ASPPlayerState* ExpiredReconnect =
		World->SpawnActor<ASPPlayerState>();
	TestNotNull(TEXT("Expired reconnect PlayerState spawns"),
		ExpiredReconnect);
	if (!ExpiredReconnect)
	{
		return false;
	}
	TestFalse(TEXT("Expired Down snapshot cannot restore active play"),
		ExpiredReconnect->AuthorityRestoreReconnectSnapshot(
			ExpiredSnapshot));
	TestEqual(TEXT("Expired reconnect becomes Missing"),
		ExpiredReconnect->GetPlayerCondition(),
		ESPPlayerCondition::Missing);
	TestEqual(TEXT("Expired reconnect becomes a spectator"),
		ExpiredReconnect->GetParticipationState(),
		ESPParticipationState::Spectating);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPGameModeKickAuthorityCleanupTest,
	"ScrollPeddler.Party.GameModeKickAuthorityCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPGameModeKickAuthorityCleanupTest::RunTest(
	const FString& Parameters)
{
	FSPScopedRunTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Authority test world exists"), World);
	if (!World)
	{
		return false;
	}

	ASPGameMode* GameMode = World->SpawnActor<ASPGameMode>();
	TestNotNull(TEXT("GameMode spawns"), GameMode);
	if (!GameMode)
	{
		return false;
	}

	const FString HubMemberId(TEXT("HubMember"));
	GameMode->RunRosterKeys.Add(HubMemberId);
	GameMode->DisconnectedRunPlayers.Add(
		HubMemberId,
		FSPDisconnectedRunRecord());
	GameMode->PendingReconnectInventories.Add(
		HubMemberId,
		FSPInventoryState());
	GameMode->RunOutcomes.Add(
		HubMemberId,
		FSPRunOutcomeRecord());
	GameMode->ApplyKickedMemberRunPolicy(
		HubMemberId,
		ESPRunPhase::Preparing,
		nullptr);
	TestFalse(TEXT("Hub kick frees the fixed roster slot"),
		GameMode->RunRosterKeys.Contains(HubMemberId));
	TestFalse(TEXT("Hub kick removes reconnect state"),
		GameMode->DisconnectedRunPlayers.Contains(HubMemberId));
	TestFalse(TEXT("Hub kick removes pending inventory state"),
		GameMode->PendingReconnectInventories.Contains(HubMemberId));
	TestFalse(TEXT("Hub kick removes stale outcome state"),
		GameMode->RunOutcomes.Contains(HubMemberId));

	const FString FieldMemberId(TEXT("FieldMember"));
	FSPDisconnectedRunRecord FieldRecord;
	FieldRecord.PlayerSnapshot.ParticipationState =
		ESPParticipationState::Disconnected;
	FieldRecord.ExpiresAtServerTime = 120.0;
	TestTrue(TEXT("Field reconnect snapshot is valid"),
		FieldRecord.PlayerSnapshot.IsStructurallyValid());
	TestTrue(TEXT("Field inventory snapshot is valid"),
		FieldRecord.InventorySnapshot.IsStructurallyValid());
	GameMode->RunRosterKeys.Add(FieldMemberId);
	GameMode->DisconnectedRunPlayers.Add(
		FieldMemberId,
		FieldRecord);
	GameMode->PendingReconnectInventories.Add(
		FieldMemberId,
		FieldRecord.InventorySnapshot);
	GameMode->ApplyKickedMemberRunPolicy(
		FieldMemberId,
		ESPRunPhase::Expedition,
		nullptr);
	TestTrue(TEXT("Field kick keeps the fixed settlement roster"),
		GameMode->RunRosterKeys.Contains(FieldMemberId));
	const FSPRunOutcomeRecord* FieldOutcome =
		GameMode->RunOutcomes.Find(FieldMemberId);
	TestTrue(TEXT("Field kick records a terminal outcome"),
		FieldOutcome && !FieldOutcome->bExtracted);
	TestFalse(TEXT("Field kick removes reconnect state"),
		GameMode->DisconnectedRunPlayers.Contains(FieldMemberId));
	TestFalse(TEXT("Field kick removes pending inventory state"),
		GameMode->PendingReconnectInventories.Contains(FieldMemberId));
	TestFalse(TEXT("Terminal field member cannot reconnect"),
		GameMode->IsRunReconnectAllowed(FieldMemberId));

	ASPPartyState* PartyState =
		World->SpawnActor<ASPPartyState>();
	TestNotNull(TEXT("PartyState spawns"), PartyState);
	if (!PartyState)
	{
		return false;
	}
	TestTrue(TEXT("Party initializes"),
		PartyState->AuthorityInitializeParty(
			TEXT("Host"),
			TEXT("Host")));
	FSPPartyActionRequest RegisterRequest;
	RegisterRequest.RequestId = FGuid::NewGuid();
	RegisterRequest.ExpectedRevision =
		PartyState->GetGovernanceState().Revision;
	TestEqual(TEXT("Kicked identity first joins"),
		PartyState->AuthorityRegisterMember(
			TEXT("KickedMember"),
			TEXT("KickedMember"),
			RegisterRequest),
		ESPPartyActionResult::Success);
	FSPPartyActionRequest KickRequest;
	KickRequest.RequestId = FGuid::NewGuid();
	KickRequest.ExpectedRevision =
		PartyState->GetGovernanceState().Revision;
	TestEqual(TEXT("Host removes the identity"),
		PartyState->AuthorityHostKickImmediately(
			TEXT("Host"),
			TEXT("KickedMember"),
			KickRequest),
		ESPPartyActionResult::Success);
	GameMode->PartyState = PartyState;
	TestTrue(TEXT("GameMode rejects a kicked identity"),
		GameMode->IsPartyMemberKicked(TEXT("KickedMember")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPGameRunStateTest,
	"ScrollPeddler.Run.GameStateDeadlinesAndRoster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPGameRunStateTest::RunTest(const FString& Parameters)
{
	FSPScopedRunTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Authority test world exists"), World);
	if (!World)
	{
		return false;
	}

	ASPGameState* GameState = World->SpawnActor<ASPGameState>();
	TestNotNull(TEXT("GameState spawns"), GameState);
	if (!GameState)
	{
		return false;
	}

	const FGuid RunId = FGuid::NewGuid();
	TestTrue(TEXT("Run initializes"),
		GameState->AuthorityInitializeRun(RunId, 4));
	TestEqual(TEXT("Run id is retained"), GameState->GetRunId(), RunId);
	TestEqual(TEXT("Run starts Preparing"),
		GameState->GetRunPhase(), ESPRunPhase::Preparing);

	TestTrue(TEXT("Expedition starts with an explicit server time"),
		GameState->AuthorityStartExpedition(100.0));
	TestEqual(TEXT("Start time is retained"),
		GameState->GetRunStartServerTime(), 100.0);
	TestEqual(TEXT("Collapse begins after 25 minutes"),
		GameState->GetExpeditionDeadlineServerTime(), 1600.0);
	TestEqual(TEXT("Resolution begins after another 2 minutes"),
		GameState->GetCollapseDeadlineServerTime(), 1720.0);
	TestEqual(TEXT("Starting roster is active"),
		GameState->GetActiveRunPlayerCount(), 4);

	TestTrue(TEXT("Roster update is accepted"),
		GameState->AuthoritySetRunRosterCounts(1, 1, 1, 1));
	TestEqual(TEXT("Active count is stored"),
		GameState->GetActiveRunPlayerCount(), 1);
	TestEqual(TEXT("Disconnected count is stored"),
		GameState->GetDisconnectedRunPlayerCount(), 1);
	TestEqual(TEXT("Extracted count is stored"),
		GameState->GetRunExtractedPlayerCount(), 1);
	TestEqual(TEXT("Missing count is stored"),
		GameState->GetMissingRunPlayerCount(), 1);
	TestEqual(TEXT("Extracted and Missing resolve two players"),
		GameState->GetUnresolvedRunPlayerCount(), 2);

	TestTrue(TEXT("Pre-collapse refresh is stable"),
		GameState->AuthorityRefreshRunPhase(1599.0));
	TestEqual(TEXT("Phase remains Expedition"),
		GameState->GetRunPhase(), ESPRunPhase::Expedition);
	TestTrue(TEXT("Collapse deadline advances the phase"),
		GameState->AuthorityRefreshRunPhase(1600.0));
	TestEqual(TEXT("Phase becomes Collapse"),
		GameState->GetRunPhase(), ESPRunPhase::Collapse);
	TestTrue(TEXT("Final deadline advances the phase"),
		GameState->AuthorityRefreshRunPhase(1720.0));
	TestEqual(TEXT("Phase becomes Resolution"),
		GameState->GetRunPhase(), ESPRunPhase::Resolution);
	TestFalse(TEXT("Resolved run cannot rewind"),
		GameState->AuthoritySetRunPhase(ESPRunPhase::Expedition));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPNoiseSubsystemTest,
	"ScrollPeddler.World.NoiseSubsystemAuthorityLedger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPNoiseSubsystemTest::RunTest(const FString& Parameters)
{
	FSPNoiseEvent Shape;
	Shape.Location = FVector(10.0, 20.0, 30.0);
	Shape.Loudness = 0.75f;
	Shape.Radius = 800.0f;
	Shape.ServerTimeSeconds = 10.0;
	TestTrue(TEXT("Finite audible event is valid"), Shape.IsValid());
	TestFalse(TEXT("Event remains live inside its lifetime"),
		Shape.IsExpired(14.0, 5.0));
	TestTrue(TEXT("Event expires at its lifetime boundary"),
		Shape.IsExpired(15.0, 5.0));

	FSPScopedRunTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Authority test world exists"), World);
	if (!World)
	{
		return false;
	}

	USPNoiseSubsystem* NoiseSubsystem =
		World->GetSubsystem<USPNoiseSubsystem>();
	TestNotNull(TEXT("World owns a noise subsystem"), NoiseSubsystem);
	if (!NoiseSubsystem)
	{
		return false;
	}

	int32 ListenerCalls = 0;
	const FDelegateHandle ListenerHandle =
		NoiseSubsystem->OnNoisePublishedNative().AddLambda(
			[&ListenerCalls](const FSPNoiseEvent& PublishedEvent)
			{
				if (PublishedEvent.IsValid())
				{
					++ListenerCalls;
				}
			});

	for (int32 Index = 0;
		Index < USPNoiseSubsystem::MaxRecentEventCount + 3;
		++Index)
	{
		FSPNoiseEvent Event = Shape;
		Event.Location.X = static_cast<double>(Index);
		Event.ServerTimeSeconds = 999999.0; // Must be replaced by authority time.
		TestTrue(
			*FString::Printf(TEXT("Noise %d publishes"), Index),
			NoiseSubsystem->PublishNoiseEvent(Event));
	}

	TestEqual(TEXT("Every publication reaches the listener"),
		ListenerCalls, USPNoiseSubsystem::MaxRecentEventCount + 3);
	TestEqual(TEXT("Recent ledger is bounded"),
		NoiseSubsystem->GetRecentNoiseEventCount(),
		USPNoiseSubsystem::MaxRecentEventCount);

	const TArray<FSPNoiseEvent> RecentEvents =
		NoiseSubsystem->GetRecentNoiseEvents();
	TestEqual(TEXT("Bounded ledger can be queried"),
		RecentEvents.Num(), USPNoiseSubsystem::MaxRecentEventCount);
	if (!RecentEvents.IsEmpty())
	{
		TestTrue(TEXT("Caller time is replaced by server time"),
			RecentEvents.Last().ServerTimeSeconds < 999999.0);
	}

	NoiseSubsystem->PruneExpiredEvents(100.0);
	TestEqual(TEXT("Expired events are removed"),
		NoiseSubsystem->GetRecentNoiseEvents().Num(), 0);
	NoiseSubsystem->OnNoisePublishedNative().Remove(ListenerHandle);
	return true;
}

#endif
