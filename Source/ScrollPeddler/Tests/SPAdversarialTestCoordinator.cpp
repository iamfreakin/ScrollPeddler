#include "Tests/SPAdversarialTestCoordinator.h"

#if !UE_BUILD_SHIPPING

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Data/SPScrollDefinition.h"
#include "Data/SPScrollEngravingDefinition.h"
#include "Game/SPGameMode.h"
#include "Game/SPGameState.h"
#include "Game/SPPlayerController.h"
#include "Game/SPPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/Parse.h"
#include "Player/SPCharacter.h"
#include "Player/SPInventoryComponent.h"
#include "World/SPGrayboxBlock.h"
#include "World/SPScrollPickup.h"

DEFINE_LOG_CATEGORY_STATIC(LogSPAdversarial, Log, All);

namespace
{
constexpr double RosterTimeoutSeconds = 30.0;
constexpr double CaseTimeoutSeconds = 15.0;
constexpr double SettlementTimeoutSeconds = 30.0;
constexpr double OwnershipAbsenceWindowSeconds = 2.0;

const FName ActionPickup(TEXT("Pickup"));
const FName ActionUse(TEXT("Use"));
const FName ActionExtract(TEXT("Extract"));
const FName ActionOwnershipSpoof(TEXT("OwnershipSpoof"));

const FName ResultSuccess(TEXT("Success"));
const FName ResultInvalidRequest(TEXT("InvalidRequest"));
const FName ResultOutOfRange(TEXT("OutOfRange"));
const FName ResultInventoryFull(TEXT("InventoryFull"));
const FName ResultObstructed(TEXT("Obstructed"));
const FName ResultContested(TEXT("Contested"));
const FName ResultNotOwned(TEXT("NotOwned"));
const FName ResultInactivePlayer(TEXT("InactivePlayer"));
const FName ResultExtracted(TEXT("Extracted"));
const FName ResultDispatched(TEXT("Dispatched"));
}

FString FSPAdversarialTestCoordinator::FAuthoritySnapshot::ToToken() const
{
	return FString::Printf(
		TEXT("inventory:%d|picked:%d|consumed:%d|players_extracted:%d|scrolls_extracted:%d|gold:%d|silence_end_total:%.3f"),
		InventoryCount,
		PickedUpCount,
		ConsumedCount,
		ExtractedCount,
		ExtractedScrollCount,
		GoldDelta,
		SilenceEndServerTimeTotal);
}

FSPAdversarialTestCoordinator::FSPAdversarialTestCoordinator(ASPGameMode& InGameMode)
	: GameMode(&InGameMode)
{
	FParse::Value(FCommandLine::Get(), TEXT("SPAdversarialRunId="), RunId);
	FParse::Value(FCommandLine::Get(), TEXT("SPAdversarialSeed="), Seed);
	FParse::Value(FCommandLine::Get(), TEXT("SPAdversarialPartySize="), PartySize);
	FParse::Value(FCommandLine::Get(), TEXT("SPAdversarialRttMs="), RttMs);
	FParse::Value(FCommandLine::Get(), TEXT("SPAdversarialLossPct="), LossPct);
	FParse::Value(FCommandLine::Get(), TEXT("SPAdversarialCommitSha="), CommitSha);

	if (RunId.IsEmpty())
	{
		RunId = TEXT("local");
	}
	PartySize = FMath::Clamp(PartySize > 0 ? PartySize : InGameMode.GetExpectedPlayers(), 1, 4);
	Seed = Seed == 0 ? 1 : Seed;
	BuildVersion = FApp::GetBuildVersion();
	if (BuildVersion.IsEmpty())
	{
		BuildVersion = FEngineVersion::Current().ToString();
	}
	if (CommitSha.IsEmpty())
	{
		CommitSha = TEXT("unknown");
	}

	Scenarios =
	{
		EScenario::OutOfRange,
		EScenario::Obstructed,
		EScenario::InventoryFull,
		EScenario::OwnershipSpoof,
		EScenario::ConcurrentClaim,
		EScenario::PickupReplay,
		EScenario::PickupRequestIdConflict,
		EScenario::UseNotOwned,
		EScenario::UseReplay,
		EScenario::PositiveSettlement,
		EScenario::InactivePlayer
	};

	const uint32 SeedBits = static_cast<uint32>(Seed);
	NextRequestId += (SeedBits & 0x000fffffu) << 8u;
	if (NextRequestId == 0)
	{
		NextRequestId = 0xA0000000u;
	}
}

FSPAdversarialTestCoordinator::~FSPAdversarialTestCoordinator()
{
	if (ASPGameMode* ScrollGameMode = GameMode.Get())
	{
		if (UWorld* World = ScrollGameMode->GetWorld())
		{
			World->GetTimerManager().ClearTimer(TickTimerHandle);
		}
	}
	CleanupScenarioFixtures();
}

void FSPAdversarialTestCoordinator::Start()
{
	ASPGameMode* ScrollGameMode = GameMode.Get();
	UWorld* World = ScrollGameMode ? ScrollGameMode->GetWorld() : nullptr;
	if (bStarted || !World || !ScrollGameMode->HasAuthority())
	{
		return;
	}

	bStarted = true;
	RunState = ERunState::WaitingForRoster;
	StateDeadlineSeconds = World->GetTimeSeconds() + RosterTimeoutSeconds;
	World->GetTimerManager().SetTimer(
		TickTimerHandle,
		FTimerDelegate::CreateRaw(this, &FSPAdversarialTestCoordinator::Tick),
		0.10f,
		true,
		0.10f);

	UE_LOG(LogSPAdversarial, Display,
		TEXT("SP_ADV_SUITE_BEGIN build_version=%s commit_sha=%s run_id=%s session_id=%s seed=%d party_size=%d rtt_ms=%d loss_pct=%d"),
		*SanitizeToken(BuildVersion),
		*SanitizeToken(CommitSha),
		*SanitizeToken(RunId),
		*GetSessionIdToken(),
		Seed,
		PartySize,
		RttMs,
		LossPct);
}

void FSPAdversarialTestCoordinator::RegisterClient(
	ASPPlayerController* PlayerController,
	const int32 ClientIndex,
	const FString& ClientRunId,
	const int32 ClientSeed,
	const int32 ClientPartySize)
{
	if (RunState == ERunState::Finished || !IsValid(PlayerController))
	{
		return;
	}

	if (ClientIndex < 0 || ClientIndex >= PartySize ||
		ClientRunId != RunId || ClientSeed != Seed || ClientPartySize != PartySize)
	{
		UE_LOG(LogSPAdversarial, Error,
			TEXT("SP_ADV_REGISTRATION_REJECTED controller=%s client_index=%d run_id=%s seed=%d party_size=%d"),
			*GetNameSafe(PlayerController),
			ClientIndex,
			*SanitizeToken(ClientRunId),
			ClientSeed,
			ClientPartySize);
		return;
	}

	if (const TWeakObjectPtr<ASPPlayerController>* Existing = Clients.Find(ClientIndex);
		Existing && Existing->IsValid() && Existing->Get() != PlayerController)
	{
		UE_LOG(LogSPAdversarial, Error,
			TEXT("SP_ADV_REGISTRATION_REJECTED controller=%s client_index=%d reason=duplicate_index"),
			*GetNameSafe(PlayerController),
			ClientIndex);
		return;
	}

	Clients.Add(ClientIndex, PlayerController);
	UE_LOG(LogSPAdversarial, Display,
		TEXT("SP_ADV_CLIENT_REGISTERED run_id=%s client_index=%d registered=%d expected=%d"),
		*SanitizeToken(RunId),
		ClientIndex,
		Clients.Num(),
		PartySize);
}

void FSPAdversarialTestCoordinator::ReportActionArmed(
	ASPPlayerController* PlayerController,
	const FName ScenarioId,
	const int32 ClientIndex)
{
	const TWeakObjectPtr<ASPPlayerController>* Registered = Clients.Find(ClientIndex);
	if (RunState != ERunState::WaitingForArmed ||
		ScenarioId != FName(GetScenarioName(Scenarios[ScenarioIndex])) ||
		!Registered || Registered->Get() != PlayerController ||
		!CurrentActions.ContainsByPredicate(
			[ClientIndex](const FActionSpec& Action) { return Action.ClientIndex == ClientIndex; }))
	{
		return;
	}

	ArmedClients.Add(ClientIndex);
}

void FSPAdversarialTestCoordinator::ReportActionResult(
	ASPPlayerController* PlayerController,
	const FName ScenarioId,
	const int32 ClientIndex,
	const uint32 RequestId,
	const FName ResultCode,
	const bool bDispatched,
	const FString& ClientState)
{
	const TWeakObjectPtr<ASPPlayerController>* Registered = Clients.Find(ClientIndex);
	const FActionSpec* Action = CurrentActions.FindByPredicate(
		[ClientIndex](const FActionSpec& Candidate) { return Candidate.ClientIndex == ClientIndex; });
	if (RunState != ERunState::WaitingForResults ||
		ScenarioId != FName(GetScenarioName(Scenarios[ScenarioIndex])) ||
		!Registered || Registered->Get() != PlayerController ||
		!Action || Action->RequestId != RequestId ||
		ClientReports.Contains(ClientIndex))
	{
		return;
	}

	FClientReport& Report = ClientReports.Add(ClientIndex);
	Report.RequestId = RequestId;
	Report.ResultCode = ResultCode;
	Report.bDispatched = bDispatched;
	Report.ClientState = SanitizeToken(ClientState);
	LogActionResult(*Action, Report);
}

void FSPAdversarialTestCoordinator::HandleLogout(ASPPlayerController* PlayerController)
{
	bool bRemovedRegisteredClient = false;
	for (auto Iterator = Clients.CreateIterator(); Iterator; ++Iterator)
	{
		if (Iterator.Value().Get() == PlayerController)
		{
			Iterator.RemoveCurrent();
			bRemovedRegisteredClient = true;
		}
	}
	if (bRemovedRegisteredClient && bStarted && RunState != ERunState::Finished)
	{
		FailRemainingScenarios(TEXT("ClientDisconnected"));
	}
}

void FSPAdversarialTestCoordinator::Tick()
{
	if (!GameMode.IsValid() || RunState == ERunState::Finished)
	{
		return;
	}

	const double Now = GetWorldTimeSeconds();
	switch (RunState)
	{
	case ERunState::WaitingForRoster:
		if (IsRosterReady())
		{
			LogRosterReady();
			RunState = ERunState::PreparingScenario;
		}
		else if (Now >= StateDeadlineSeconds)
		{
			FailRemainingScenarios(TEXT("RosterTimeout"));
		}
		break;

	case ERunState::PreparingScenario:
		BeginNextScenario();
		break;

	case ERunState::WaitingForArmed:
		if (HaveAllActionsArmed())
		{
			ReleaseArmedActions();
		}
		else if (Now >= StateDeadlineSeconds)
		{
			FinishCurrentScenario(false, FName(TEXT("ArmTimeout")), TEXT("NotAllClientsArmed"));
		}
		break;

	case ERunState::WaitingForResults:
		if (HaveAllActionResults())
		{
			OnActionsCompleted();
		}
		else if (Now >= StateDeadlineSeconds)
		{
			FinishCurrentScenario(false, FName(TEXT("ActionTimeout")), TEXT("MissingClientResult"));
		}
		break;

	case ERunState::WaitingForAbsenceWindow:
		if (Now >= CurrentCaseDeadlineSeconds && Now < AbsenceWindowDeadlineSeconds)
		{
			FinishCurrentScenario(false, FName(TEXT("CaseTimeout")), TEXT("OwnershipAbsenceWindowIncomplete"));
		}
		else if (Now >= AbsenceWindowDeadlineSeconds)
		{
			EvaluateCurrentScenario();
		}
		break;

	case ERunState::WaitingForSettlement:
		if (const ASPGameMode* ScrollGameMode = GameMode.Get())
		{
			const ASPGameState* ScrollGameState = ScrollGameMode->GetGameState<ASPGameState>();
			if (ScrollGameState && ScrollGameState->IsSettlementCommitted())
			{
				EvaluateCurrentScenario();
			}
			else if (Now >= StateDeadlineSeconds)
			{
				FinishCurrentScenario(false, FName(TEXT("SettlementTimeout")), TEXT("SettlementNotCommitted"));
			}
		}
		break;

	case ERunState::Finished:
	default:
		break;
	}
}

bool FSPAdversarialTestCoordinator::IsRosterReady() const
{
	ASPGameMode* ScrollGameMode = GameMode.Get();
	if (!ScrollGameMode || ScrollGameMode->GetNumPlayers() != PartySize || Clients.Num() != PartySize)
	{
		return false;
	}

	for (int32 ClientIndex = 0; ClientIndex < PartySize; ++ClientIndex)
	{
		const TWeakObjectPtr<ASPPlayerController>* Controller = Clients.Find(ClientIndex);
		if (!Controller || !Controller->IsValid() ||
			!Cast<ASPCharacter>((*Controller)->GetPawn()) ||
			!(*Controller)->GetPlayerState<ASPPlayerState>())
		{
			return false;
		}
	}
	return true;
}

void FSPAdversarialTestCoordinator::BeginNextScenario()
{
	CleanupScenarioFixtures();
	++ScenarioIndex;
	CurrentStage = 0;
	bCurrentCaseLogged = false;
	if (!Scenarios.IsValidIndex(ScenarioIndex))
	{
		FinishSuite();
		return;
	}

	CurrentCaseDeadlineSeconds = GetWorldTimeSeconds() + CaseTimeoutSeconds;
	BeginScenario(Scenarios[ScenarioIndex]);
}

void FSPAdversarialTestCoordinator::BeginScenario(const EScenario Scenario)
{
	switch (Scenario)
	{
	case EScenario::OutOfRange:
		PrepareOutOfRange();
		break;
	case EScenario::Obstructed:
		PrepareObstructed();
		break;
	case EScenario::InventoryFull:
		PrepareInventoryFull();
		break;
	case EScenario::OwnershipSpoof:
		PrepareOwnershipSpoof();
		break;
	case EScenario::ConcurrentClaim:
		PrepareConcurrentClaim();
		break;
	case EScenario::PickupReplay:
		PreparePickupReplay();
		break;
	case EScenario::PickupRequestIdConflict:
		PreparePickupRequestIdConflict();
		break;
	case EScenario::UseNotOwned:
		PrepareUseNotOwned();
		break;
	case EScenario::UseReplay:
		PrepareUseReplay();
		break;
	case EScenario::PositiveSettlement:
		PreparePositiveSettlement();
		break;
	case EScenario::InactivePlayer:
		PrepareInactivePlayer();
		break;
	default:
		FinishCurrentScenario(false, FName(TEXT("HarnessError")), TEXT("UnknownScenario"));
		break;
	}
}

void FSPAdversarialTestCoordinator::OnActionsCompleted()
{
	const EScenario Scenario = Scenarios[ScenarioIndex];
	if (Scenario == EScenario::OwnershipSpoof)
	{
		AbsenceWindowDeadlineSeconds = GetWorldTimeSeconds() + OwnershipAbsenceWindowSeconds;
		RunState = ERunState::WaitingForAbsenceWindow;
		return;
	}

	if (Scenario == EScenario::PickupReplay && CurrentStage == 0)
	{
		const FClientReport* Report = FindReport(0);
		if (!Report || Report->ResultCode != ResultSuccess)
		{
			FinishCurrentScenario(false, Report ? Report->ResultCode : FName(TEXT("MissingResult")),
				TEXT("InitialPickupFailed"));
			return;
		}

		MiddleSnapshot = CaptureAuthoritySnapshot();
		const FGuid ClaimedTargetId = CurrentActions[0].TargetInstanceId;
		if (ASPScrollPickup* ClaimedPickup = PrimaryPickup.Get())
		{
			ClaimedPickup->Destroy();
		}
		PrimaryPickup.Reset();
		CurrentStage = 1;
		FActionSpec Replay;
		Replay.ClientIndex = 0;
		Replay.Action = ActionPickup;
		Replay.TargetPickup = nullptr;
		Replay.TargetInstanceId = ClaimedTargetId;
		Replay.RequestId = CurrentRequestId;
		Replay.bAllowMissingTarget = true;
		ArmActions({Replay});
		return;
	}

	if (Scenario == EScenario::PickupRequestIdConflict && CurrentStage == 0)
	{
		const FClientReport* Report = FindReport(0);
		if (!Report || Report->ResultCode != ResultSuccess || !SecondaryPickup.IsValid())
		{
			FinishCurrentScenario(false, Report ? Report->ResultCode : FName(TEXT("MissingResult")),
				TEXT("InitialPickupFailed"));
			return;
		}

		MiddleSnapshot = CaptureAuthoritySnapshot();
		CurrentStage = 1;
		FActionSpec Conflict;
		Conflict.ClientIndex = 0;
		Conflict.Action = ActionPickup;
		Conflict.TargetPickup = SecondaryPickup;
		Conflict.TargetInstanceId = SecondaryPickup->GetScrollInstance().InstanceId;
		Conflict.RequestId = CurrentRequestId;
		ArmActions({Conflict});
		return;
	}

	if (Scenario == EScenario::UseReplay && CurrentStage == 0)
	{
		const FClientReport* Report = FindReport(0);
		if (!Report || Report->ResultCode != ResultSuccess)
		{
			FinishCurrentScenario(false, Report ? Report->ResultCode : FName(TEXT("MissingResult")),
				TEXT("InitialUseFailed"));
			return;
		}

		MiddleSnapshot = CaptureAuthoritySnapshot();
		CurrentStage = 1;
		FActionSpec Replay;
		Replay.ClientIndex = 0;
		Replay.Action = ActionUse;
		Replay.TargetInstanceId = CurrentActions[0].TargetInstanceId;
		Replay.RequestId = CurrentRequestId;
		ArmActions({Replay});
		return;
	}

	if (Scenario == EScenario::PositiveSettlement)
	{
		if (CurrentStage == 0)
		{
			if (CountReportedResult(ResultSuccess) != CurrentActions.Num() || !AllPlayersHaveInventory())
			{
				FinishCurrentScenario(false, FName(TEXT("PickupSetupFailed")), TEXT("PositiveInventoryPreparation"));
				return;
			}
			BeginPositiveExtraction();
			return;
		}

		if (CurrentStage == 1)
		{
			if (CountReportedResult(ResultExtracted) != PartySize)
			{
				FinishCurrentScenario(false, FName(TEXT("ExtractionFailed")), TEXT("NotAllClientsExtracted"));
				return;
			}
			RunState = ERunState::WaitingForSettlement;
			StateDeadlineSeconds = GetWorldTimeSeconds() + SettlementTimeoutSeconds;
			return;
		}
	}

	EvaluateCurrentScenario();
}

void FSPAdversarialTestCoordinator::EvaluateCurrentScenario()
{
	const EScenario Scenario = Scenarios[ScenarioIndex];
	const FAuthoritySnapshot After = CaptureAuthoritySnapshot();
	const bool bNoInventoryLedgerOrEffectMutation =
		After.InventoryCount == BeforeSnapshot.InventoryCount &&
		After.PickedUpCount == BeforeSnapshot.PickedUpCount &&
		After.ConsumedCount == BeforeSnapshot.ConsumedCount &&
		FMath::IsNearlyEqual(
			After.SilenceEndServerTimeTotal,
			BeforeSnapshot.SilenceEndServerTimeTotal);

	switch (Scenario)
	{
	case EScenario::OutOfRange:
	{
		const FClientReport* Report = FindReport(0);
		const bool bPassed = Report && Report->ResultCode == ResultOutOfRange &&
			bNoInventoryLedgerOrEffectMutation && IsPrimaryPickupAvailable();
		FinishCurrentScenario(
			bPassed,
			Report ? Report->ResultCode : FName(TEXT("MissingResult")),
			FString::Printf(TEXT("before=%s,after=%s,target_available=%d"),
				*BeforeSnapshot.ToToken(), *After.ToToken(), IsPrimaryPickupAvailable() ? 1 : 0));
		break;
	}
	case EScenario::Obstructed:
	{
		const FClientReport* Report = FindReport(0);
		const bool bPassed = Report && Report->ResultCode == ResultObstructed &&
			bNoInventoryLedgerOrEffectMutation && IsPrimaryPickupAvailable();
		FinishCurrentScenario(
			bPassed,
			Report ? Report->ResultCode : FName(TEXT("MissingResult")),
			FString::Printf(TEXT("before=%s,after=%s,target_available=%d"),
				*BeforeSnapshot.ToToken(), *After.ToToken(), IsPrimaryPickupAvailable() ? 1 : 0));
		break;
	}
	case EScenario::InventoryFull:
	{
		const FClientReport* Report = FindReport(0);
		const bool bPassed = Report && Report->ResultCode == ResultInventoryFull &&
			bNoInventoryLedgerOrEffectMutation && IsPrimaryPickupAvailable();
		FinishCurrentScenario(
			bPassed,
			Report ? Report->ResultCode : FName(TEXT("MissingResult")),
			FString::Printf(TEXT("before=%s,after=%s,target_available=%d"),
				*BeforeSnapshot.ToToken(), *After.ToToken(), IsPrimaryPickupAvailable() ? 1 : 0));
		break;
	}
	case EScenario::OwnershipSpoof:
	{
		const FClientReport* Report = FindReport(1);
		const bool bPassed = Report && Report->ResultCode == ResultDispatched &&
			Report->bDispatched && bNoInventoryLedgerOrEffectMutation && IsPrimaryPickupAvailable();
		FinishCurrentScenario(
			bPassed,
			Report ? FName(TEXT("NoServerMutation")) : FName(TEXT("MissingDispatch")),
			FString::Printf(TEXT("absence_window_ms=2000,before=%s,after=%s,target_available=%d"),
				*BeforeSnapshot.ToToken(), *After.ToToken(), IsPrimaryPickupAvailable() ? 1 : 0));
		break;
	}
	case EScenario::ConcurrentClaim:
	{
		const int32 Successes = CountReportedResult(ResultSuccess);
		const int32 Contested = CountReportedResult(ResultContested);
		int32 SuccessClientIndex = INDEX_NONE;
		for (const TPair<int32, FClientReport>& Entry : ClientReports)
		{
			if (Entry.Value.ResultCode == ResultSuccess)
			{
				SuccessClientIndex = Entry.Key;
				break;
			}
		}
		const FGuid TargetId = CurrentActions.IsEmpty()
			? FGuid()
			: CurrentActions[0].TargetInstanceId;
		int32 OwnerClientIndex = INDEX_NONE;
		const int32 OwnerCount = CountInventoryOwners(TargetId, OwnerClientIndex);
		const bool bPassed =
			Successes == 1 &&
			Contested == PartySize - 1 &&
			OwnerCount == 1 &&
			OwnerClientIndex == SuccessClientIndex &&
			After.InventoryCount == BeforeSnapshot.InventoryCount + 1 &&
			After.PickedUpCount == BeforeSnapshot.PickedUpCount + 1 &&
			!IsPrimaryPickupAvailable();
		FinishCurrentScenario(
			bPassed,
			FName(TEXT("OneSuccessRestContested")),
			FString::Printf(TEXT("success=%d,contested=%d,owner_count=%d,success_client=%d,owner_client=%d,before=%s,after=%s"),
				Successes, Contested, OwnerCount, SuccessClientIndex, OwnerClientIndex,
				*BeforeSnapshot.ToToken(), *After.ToToken()));
		break;
	}
	case EScenario::PickupReplay:
	{
		const FClientReport* Report = FindReport(0);
		const bool bPassed =
			CurrentStage == 1 &&
			Report && Report->ResultCode == ResultSuccess &&
			MiddleSnapshot.InventoryCount == BeforeSnapshot.InventoryCount + 1 &&
			MiddleSnapshot.PickedUpCount == BeforeSnapshot.PickedUpCount + 1 &&
			After.InventoryCount == MiddleSnapshot.InventoryCount &&
			After.PickedUpCount == MiddleSnapshot.PickedUpCount;
		FinishCurrentScenario(
			bPassed,
			Report ? Report->ResultCode : FName(TEXT("MissingResult")),
			FString::Printf(TEXT("before=%s,first=%s,replay=%s"),
				*BeforeSnapshot.ToToken(), *MiddleSnapshot.ToToken(), *After.ToToken()));
		break;
	}
	case EScenario::PickupRequestIdConflict:
	{
		const FClientReport* Report = FindReport(0);
		const bool bPassed =
			CurrentStage == 1 &&
			Report && Report->ResultCode == ResultInvalidRequest &&
			MiddleSnapshot.InventoryCount == BeforeSnapshot.InventoryCount + 1 &&
			MiddleSnapshot.PickedUpCount == BeforeSnapshot.PickedUpCount + 1 &&
			After.InventoryCount == MiddleSnapshot.InventoryCount &&
			After.PickedUpCount == MiddleSnapshot.PickedUpCount &&
			SecondaryPickup.IsValid() && SecondaryPickup->IsAvailable();
		FinishCurrentScenario(
			bPassed,
			Report ? Report->ResultCode : FName(TEXT("MissingResult")),
			FString::Printf(TEXT("before=%s,first=%s,conflict=%s,second_available=%d"),
				*BeforeSnapshot.ToToken(), *MiddleSnapshot.ToToken(), *After.ToToken(),
				SecondaryPickup.IsValid() && SecondaryPickup->IsAvailable() ? 1 : 0));
		break;
	}
	case EScenario::UseNotOwned:
	{
		const FClientReport* Report = FindReport(0);
		const bool bPassed = Report && Report->ResultCode == ResultNotOwned &&
			bNoInventoryLedgerOrEffectMutation;
		FinishCurrentScenario(
			bPassed,
			Report ? Report->ResultCode : FName(TEXT("MissingResult")),
			FString::Printf(TEXT("before=%s,after=%s"),
				*BeforeSnapshot.ToToken(), *After.ToToken()));
		break;
	}
	case EScenario::UseReplay:
	{
		const FClientReport* Report = FindReport(0);
		const bool bPassed =
			CurrentStage == 1 &&
			Report && Report->ResultCode == ResultSuccess &&
			MiddleSnapshot.InventoryCount == BeforeSnapshot.InventoryCount - 1 &&
			MiddleSnapshot.ConsumedCount == BeforeSnapshot.ConsumedCount + 1 &&
			MiddleSnapshot.SilenceEndServerTimeTotal >
				BeforeSnapshot.SilenceEndServerTimeTotal &&
			After.InventoryCount == MiddleSnapshot.InventoryCount &&
			After.ConsumedCount == MiddleSnapshot.ConsumedCount &&
			FMath::IsNearlyEqual(
				After.SilenceEndServerTimeTotal,
				MiddleSnapshot.SilenceEndServerTimeTotal);
		FinishCurrentScenario(
			bPassed,
			Report ? Report->ResultCode : FName(TEXT("MissingResult")),
			FString::Printf(TEXT("before=%s,first=%s,replay=%s"),
				*BeforeSnapshot.ToToken(), *MiddleSnapshot.ToToken(), *After.ToToken()));
		break;
	}
	case EScenario::PositiveSettlement:
	{
		const bool bPassed = AllPlayersExtractedWithPositiveResult();
		FinishCurrentScenario(
			bPassed,
			bPassed ? FName(TEXT("SettlementCommitted")) : FName(TEXT("InvalidSettlementState")),
			FString::Printf(TEXT("before=%s,after=%s"),
				*BeforeSnapshot.ToToken(), *After.ToToken()));
		break;
	}
	case EScenario::InactivePlayer:
	{
		const FClientReport* Report = FindReport(0);
		const bool bPassed = Report && Report->ResultCode == ResultInactivePlayer &&
			bNoInventoryLedgerOrEffectMutation && IsPrimaryPickupAvailable();
		FinishCurrentScenario(
			bPassed,
			Report ? Report->ResultCode : FName(TEXT("MissingResult")),
			FString::Printf(TEXT("before=%s,after=%s,target_available=%d"),
				*BeforeSnapshot.ToToken(), *After.ToToken(), IsPrimaryPickupAvailable() ? 1 : 0));
		break;
	}
	default:
		FinishCurrentScenario(false, FName(TEXT("HarnessError")), TEXT("UnhandledEvaluation"));
		break;
	}
}

void FSPAdversarialTestCoordinator::FinishCurrentScenario(
	const bool bPassed,
	const FName ResultCode,
	const FString& Detail,
	const bool bSkipped)
{
	if (!Scenarios.IsValidIndex(ScenarioIndex) || bCurrentCaseLogged)
	{
		return;
	}
	bCurrentCaseLogged = true;
	if (bSkipped)
	{
		++SkippedCases;
	}
	else if (bPassed)
	{
		++PassedCases;
	}
	else
	{
		++FailedCases;
	}

	const TCHAR* ScenarioName = GetScenarioName(Scenarios[ScenarioIndex]);
	const FString SanitizedRunId = SanitizeToken(RunId);
	const FString SanitizedResult = SanitizeToken(ResultCode.ToString());
	const FString SanitizedDetail = SanitizeToken(Detail);
	const FString StateAfter = CaptureAuthoritySnapshot().ToToken();
	if (bPassed)
	{
		UE_LOG(LogSPAdversarial, Display,
			TEXT("SP_ADV_CASE_RESULT scenario=%s passed=1 skipped=%d build_version=%s commit_sha=%s run_id=%s session_id=%s seed=%d party_size=%d rtt_ms=%d loss_pct=%d server_frame=%llu result=%s state_before=%s state_after=%s detail=%s"),
			ScenarioName,
			bSkipped ? 1 : 0,
			*SanitizeToken(BuildVersion),
			*SanitizeToken(CommitSha),
			*SanitizedRunId,
			*GetSessionIdToken(),
			Seed,
			PartySize,
			RttMs,
			LossPct,
			static_cast<unsigned long long>(GFrameCounter),
			*SanitizedResult,
			*BeforeSnapshot.ToToken(),
			*StateAfter,
			*SanitizedDetail);
	}
	else
	{
		UE_LOG(LogSPAdversarial, Error,
			TEXT("SP_ADV_CASE_RESULT scenario=%s passed=0 skipped=%d build_version=%s commit_sha=%s run_id=%s session_id=%s seed=%d party_size=%d rtt_ms=%d loss_pct=%d server_frame=%llu result=%s state_before=%s state_after=%s detail=%s"),
			ScenarioName,
			bSkipped ? 1 : 0,
			*SanitizeToken(BuildVersion),
			*SanitizeToken(CommitSha),
			*SanitizedRunId,
			*GetSessionIdToken(),
			Seed,
			PartySize,
			RttMs,
			LossPct,
			static_cast<unsigned long long>(GFrameCounter),
			*SanitizedResult,
			*BeforeSnapshot.ToToken(),
			*StateAfter,
			*SanitizedDetail);
	}

	RunState = ERunState::PreparingScenario;
}

void FSPAdversarialTestCoordinator::FinishSuite()
{
	CleanupScenarioFixtures();
	RunState = ERunState::Finished;
	const bool bPassed =
		FailedCases == 0 &&
		PassedCases + SkippedCases == Scenarios.Num();
	if (bPassed)
	{
		UE_LOG(LogSPAdversarial, Display,
			TEXT("SP_ADV_SUITE_RESULT passed=1 build_version=%s commit_sha=%s run_id=%s session_id=%s seed=%d party_size=%d rtt_ms=%d loss_pct=%d server_frame=%llu cases_passed=%d cases_failed=%d cases_skipped=%d"),
			*SanitizeToken(BuildVersion),
			*SanitizeToken(CommitSha),
			*SanitizeToken(RunId),
			*GetSessionIdToken(),
			Seed,
			PartySize,
			RttMs,
			LossPct,
			static_cast<unsigned long long>(GFrameCounter),
			PassedCases,
			FailedCases,
			SkippedCases);
	}
	else
	{
		UE_LOG(LogSPAdversarial, Error,
			TEXT("SP_ADV_SUITE_RESULT passed=0 build_version=%s commit_sha=%s run_id=%s session_id=%s seed=%d party_size=%d rtt_ms=%d loss_pct=%d server_frame=%llu cases_passed=%d cases_failed=%d cases_skipped=%d"),
			*SanitizeToken(BuildVersion),
			*SanitizeToken(CommitSha),
			*SanitizeToken(RunId),
			*GetSessionIdToken(),
			Seed,
			PartySize,
			RttMs,
			LossPct,
			static_cast<unsigned long long>(GFrameCounter),
			PassedCases,
			FailedCases,
			SkippedCases);
	}

	for (const TPair<int32, TWeakObjectPtr<ASPPlayerController>>& Entry : Clients)
	{
		if (ASPPlayerController* PlayerController = Entry.Value.Get())
		{
			PlayerController->ClientFinishAdversarialSuite(bPassed);
		}
	}

	if (ASPGameMode* ScrollGameMode = GameMode.Get())
	{
		if (UWorld* World = ScrollGameMode->GetWorld())
		{
			World->GetTimerManager().ClearTimer(TickTimerHandle);
		}
	}
}

void FSPAdversarialTestCoordinator::FailRemainingScenarios(const FString& Detail)
{
	if (RunState == ERunState::Finished)
	{
		return;
	}
	if (Scenarios.IsValidIndex(ScenarioIndex) && !bCurrentCaseLogged)
	{
		FinishCurrentScenario(false, FName(TEXT("HarnessError")), Detail);
	}
	while (++ScenarioIndex < Scenarios.Num())
	{
		bCurrentCaseLogged = false;
		BeforeSnapshot = CaptureAuthoritySnapshot();
		LogScenarioBegin(Scenarios[ScenarioIndex]);
		FinishCurrentScenario(false, FName(TEXT("HarnessError")), Detail);
	}
	FinishSuite();
}

void FSPAdversarialTestCoordinator::ArmActions(const TArray<FActionSpec>& Actions)
{
	CurrentActions = Actions;
	ArmedClients.Reset();
	ClientReports.Reset();
	RunState = ERunState::WaitingForArmed;
	StateDeadlineSeconds = CurrentCaseDeadlineSeconds;

	const FName ScenarioId(GetScenarioName(Scenarios[ScenarioIndex]));
	for (const FActionSpec& Action : CurrentActions)
	{
		if (const TWeakObjectPtr<ASPPlayerController>* Controller = Clients.Find(Action.ClientIndex);
			Controller && Controller->IsValid())
		{
			(*Controller)->ClientArmAdversarialAction(
				ScenarioId,
				Action.Action,
				Action.ActionPawn.Get(),
				Action.TargetPickup.Get(),
				Action.TargetInstanceId,
				Action.RequestId,
				Action.bAllowMissingTarget);
		}
	}
}

void FSPAdversarialTestCoordinator::ReleaseArmedActions()
{
	const FName ScenarioId(GetScenarioName(Scenarios[ScenarioIndex]));
	RunState = ERunState::WaitingForResults;
	StateDeadlineSeconds = CurrentCaseDeadlineSeconds;
	for (const FActionSpec& Action : CurrentActions)
	{
		if (const TWeakObjectPtr<ASPPlayerController>* Controller = Clients.Find(Action.ClientIndex);
			Controller && Controller->IsValid())
		{
			(*Controller)->ClientReleaseAdversarialAction(ScenarioId);
		}
	}
}

bool FSPAdversarialTestCoordinator::HaveAllActionsArmed() const
{
	return CurrentActions.Num() > 0 && ArmedClients.Num() == CurrentActions.Num();
}

bool FSPAdversarialTestCoordinator::HaveAllActionResults() const
{
	return CurrentActions.Num() > 0 && ClientReports.Num() == CurrentActions.Num();
}

int32 FSPAdversarialTestCoordinator::CountReportedResult(const FName ResultCode) const
{
	int32 Count = 0;
	for (const TPair<int32, FClientReport>& Entry : ClientReports)
	{
		Count += Entry.Value.ResultCode == ResultCode ? 1 : 0;
	}
	return Count;
}

const FSPAdversarialTestCoordinator::FClientReport*
FSPAdversarialTestCoordinator::FindReport(const int32 ClientIndex) const
{
	return ClientReports.Find(ClientIndex);
}

void FSPAdversarialTestCoordinator::PrepareOutOfRange()
{
	PlaceParticipants();
	const ASPCharacter* Character = GetCharacter(0);
	PrimaryPickup = Character
		? SpawnPickup(Character->GetActorLocation() + FVector(500.0f, 0.0f, -30.0f))
		: nullptr;
	BeforeSnapshot = CaptureAuthoritySnapshot();
	LogScenarioBegin(EScenario::OutOfRange);
	if (!PrimaryPickup.IsValid())
	{
		FinishCurrentScenario(false, FName(TEXT("FixtureError")), TEXT("MissingPickup"));
		return;
	}

	CurrentRequestId = AllocateRequestId();
	FActionSpec Action;
	Action.ClientIndex = 0;
	Action.Action = ActionPickup;
	Action.TargetPickup = PrimaryPickup;
	Action.TargetInstanceId = PrimaryPickup->GetScrollInstance().InstanceId;
	Action.RequestId = CurrentRequestId;
	ArmActions({Action});
}

void FSPAdversarialTestCoordinator::PrepareObstructed()
{
	PlaceParticipants();
	const ASPCharacter* Character = GetCharacter(0);
	if (Character)
	{
		const FVector CharacterLocation = Character->GetActorLocation();
		PrimaryPickup = SpawnPickup(CharacterLocation + FVector(240.0f, 0.0f, -30.0f));
		SpawnLineOfSightBlocker(CharacterLocation + FVector(120.0f, 0.0f, 35.0f));
	}
	BeforeSnapshot = CaptureAuthoritySnapshot();
	LogScenarioBegin(EScenario::Obstructed);
	if (!PrimaryPickup.IsValid())
	{
		FinishCurrentScenario(false, FName(TEXT("FixtureError")), TEXT("MissingPickup"));
		return;
	}

	CurrentRequestId = AllocateRequestId();
	FActionSpec Action;
	Action.ClientIndex = 0;
	Action.Action = ActionPickup;
	Action.TargetPickup = PrimaryPickup;
	Action.TargetInstanceId = PrimaryPickup->GetScrollInstance().InstanceId;
	Action.RequestId = CurrentRequestId;
	ArmActions({Action});
}

void FSPAdversarialTestCoordinator::PrepareInventoryFull()
{
	PlaceParticipants();
	FillInventoryForCapacityCase(0);
	const ASPCharacter* Character = GetCharacter(0);
	PrimaryPickup = Character
		? SpawnPickup(Character->GetActorLocation() + FVector(180.0f, 0.0f, -30.0f))
		: nullptr;
	BeforeSnapshot = CaptureAuthoritySnapshot();
	LogScenarioBegin(EScenario::InventoryFull);
	if (!PrimaryPickup.IsValid())
	{
		FinishCurrentScenario(false, FName(TEXT("FixtureError")), TEXT("MissingPickup"));
		return;
	}

	CurrentRequestId = AllocateRequestId();
	FActionSpec Action;
	Action.ClientIndex = 0;
	Action.Action = ActionPickup;
	Action.TargetPickup = PrimaryPickup;
	Action.TargetInstanceId = PrimaryPickup->GetScrollInstance().InstanceId;
	Action.RequestId = CurrentRequestId;
	ArmActions({Action});
}

void FSPAdversarialTestCoordinator::PrepareOwnershipSpoof()
{
	PlaceParticipants();
	if (PartySize < 2)
	{
		BeforeSnapshot = CaptureAuthoritySnapshot();
		LogScenarioBegin(EScenario::OwnershipSpoof);
		FinishCurrentScenario(true, FName(TEXT("NotApplicable")), TEXT("Requires2Players"), true);
		return;
	}

	const ASPCharacter* Victim = GetCharacter(0);
	PrimaryPickup = Victim
		? SpawnPickup(Victim->GetActorLocation() + FVector(180.0f, 0.0f, -30.0f))
		: nullptr;
	BeforeSnapshot = CaptureAuthoritySnapshot();
	LogScenarioBegin(EScenario::OwnershipSpoof);
	if (!PrimaryPickup.IsValid() || !Victim)
	{
		FinishCurrentScenario(false, FName(TEXT("FixtureError")), TEXT("MissingVictimOrPickup"));
		return;
	}

	CurrentRequestId = AllocateRequestId();
	FActionSpec Action;
	Action.ClientIndex = 1;
	Action.Action = ActionOwnershipSpoof;
	Action.ActionPawn = GetCharacter(0);
	Action.TargetPickup = PrimaryPickup;
	Action.TargetInstanceId = PrimaryPickup->GetScrollInstance().InstanceId;
	Action.RequestId = CurrentRequestId;
	ArmActions({Action});
}

void FSPAdversarialTestCoordinator::PrepareConcurrentClaim()
{
	PlaceParticipants();
	if (PartySize < 2)
	{
		BeforeSnapshot = CaptureAuthoritySnapshot();
		LogScenarioBegin(EScenario::ConcurrentClaim);
		FinishCurrentScenario(true, FName(TEXT("NotApplicable")), TEXT("Requires2Players"), true);
		return;
	}

	PrimaryPickup = SpawnPickup(FVector(-300.0f, 0.0f, 80.0f));
	BeforeSnapshot = CaptureAuthoritySnapshot();
	LogScenarioBegin(EScenario::ConcurrentClaim);
	if (!PrimaryPickup.IsValid())
	{
		FinishCurrentScenario(false, FName(TEXT("FixtureError")), TEXT("MissingPickup"));
		return;
	}

	TArray<FActionSpec> Actions;
	const FGuid TargetId = PrimaryPickup->GetScrollInstance().InstanceId;
	for (int32 ClientIndex = 0; ClientIndex < PartySize; ++ClientIndex)
	{
		if (ASPCharacter* Character = GetCharacter(ClientIndex))
		{
			const float OffsetY = (static_cast<float>(ClientIndex) - (PartySize - 1) * 0.5f) * 50.0f;
			Character->SetActorLocation(FVector(-520.0f, OffsetY, 110.0f), false, nullptr, ETeleportType::TeleportPhysics);
		}

		FActionSpec& Action = Actions.AddDefaulted_GetRef();
		Action.ClientIndex = ClientIndex;
		Action.Action = ActionPickup;
		Action.TargetPickup = PrimaryPickup;
		Action.TargetInstanceId = TargetId;
		Action.RequestId = AllocateRequestId();
	}
	ArmActions(Actions);
}

void FSPAdversarialTestCoordinator::PreparePickupReplay()
{
	PlaceParticipants();
	const ASPCharacter* Character = GetCharacter(0);
	PrimaryPickup = Character
		? SpawnPickup(Character->GetActorLocation() + FVector(180.0f, 0.0f, -30.0f))
		: nullptr;
	BeforeSnapshot = CaptureAuthoritySnapshot();
	LogScenarioBegin(EScenario::PickupReplay);
	if (!PrimaryPickup.IsValid())
	{
		FinishCurrentScenario(false, FName(TEXT("FixtureError")), TEXT("MissingPickup"));
		return;
	}

	CurrentRequestId = AllocateRequestId();
	FActionSpec Action;
	Action.ClientIndex = 0;
	Action.Action = ActionPickup;
	Action.TargetPickup = PrimaryPickup;
	Action.TargetInstanceId = PrimaryPickup->GetScrollInstance().InstanceId;
	Action.RequestId = CurrentRequestId;
	ArmActions({Action});
}

void FSPAdversarialTestCoordinator::PreparePickupRequestIdConflict()
{
	PlaceParticipants();
	const ASPCharacter* Character = GetCharacter(0);
	if (Character)
	{
		const FVector BaseLocation = Character->GetActorLocation();
		PrimaryPickup = SpawnPickup(BaseLocation + FVector(170.0f, -60.0f, -30.0f));
		SecondaryPickup = SpawnPickup(BaseLocation + FVector(170.0f, 60.0f, -30.0f));
	}
	BeforeSnapshot = CaptureAuthoritySnapshot();
	LogScenarioBegin(EScenario::PickupRequestIdConflict);
	if (!PrimaryPickup.IsValid() || !SecondaryPickup.IsValid())
	{
		FinishCurrentScenario(false, FName(TEXT("FixtureError")), TEXT("MissingPickup"));
		return;
	}

	CurrentRequestId = AllocateRequestId();
	FActionSpec Action;
	Action.ClientIndex = 0;
	Action.Action = ActionPickup;
	Action.TargetPickup = PrimaryPickup;
	Action.TargetInstanceId = PrimaryPickup->GetScrollInstance().InstanceId;
	Action.RequestId = CurrentRequestId;
	ArmActions({Action});
}

void FSPAdversarialTestCoordinator::PrepareUseNotOwned()
{
	PlaceParticipants();
	BeforeSnapshot = CaptureAuthoritySnapshot();
	LogScenarioBegin(EScenario::UseNotOwned);

	CurrentRequestId = AllocateRequestId();
	FActionSpec Action;
	Action.ClientIndex = 0;
	Action.Action = ActionUse;
	Action.TargetInstanceId = FGuid(0x53504144u, static_cast<uint32>(Seed), 0xdead0001u, 0xbad00001u);
	Action.RequestId = CurrentRequestId;
	ArmActions({Action});
}

void FSPAdversarialTestCoordinator::PrepareUseReplay()
{
	PlaceParticipants();
	ASPCharacter* Character = GetCharacter(0);
	const FGuid OwnedInstanceId = Character
		? Character->GetInventory().GetFirstInstanceId()
		: FGuid();
	BeforeSnapshot = CaptureAuthoritySnapshot();
	LogScenarioBegin(EScenario::UseReplay);
	if (!OwnedInstanceId.IsValid())
	{
		FinishCurrentScenario(false, FName(TEXT("FixtureError")), TEXT("MissingOwnedScroll"));
		return;
	}

	CurrentRequestId = AllocateRequestId();
	FActionSpec Action;
	Action.ClientIndex = 0;
	Action.Action = ActionUse;
	Action.TargetInstanceId = OwnedInstanceId;
	Action.RequestId = CurrentRequestId;
	ArmActions({Action});
}

void FSPAdversarialTestCoordinator::PreparePositiveSettlement()
{
	PlaceParticipants();
	BeforeSnapshot = CaptureAuthoritySnapshot();
	LogScenarioBegin(EScenario::PositiveSettlement);

	TArray<FActionSpec> Actions;
	for (int32 ClientIndex = 0; ClientIndex < PartySize; ++ClientIndex)
	{
		ASPCharacter* Character = GetCharacter(ClientIndex);
		if (!Character || Character->GetInventory().GetItemCount() > 0)
		{
			continue;
		}

		ASPScrollPickup* Pickup = SpawnPickup(
			Character->GetActorLocation() + FVector(170.0f, 0.0f, -30.0f));
		if (!Pickup)
		{
			continue;
		}

		FActionSpec& Action = Actions.AddDefaulted_GetRef();
		Action.ClientIndex = ClientIndex;
		Action.Action = ActionPickup;
		Action.TargetPickup = Pickup;
		Action.TargetInstanceId = Pickup->GetScrollInstance().InstanceId;
		Action.RequestId = AllocateRequestId();
	}

	if (Actions.IsEmpty())
	{
		BeginPositiveExtraction();
		return;
	}
	CurrentStage = 0;
	ArmActions(Actions);
}

void FSPAdversarialTestCoordinator::PrepareInactivePlayer()
{
	PlaceParticipants();
	const ASPCharacter* Character = GetCharacter(0);
	PrimaryPickup = Character
		? SpawnPickup(Character->GetActorLocation() + FVector(180.0f, 0.0f, -30.0f))
		: nullptr;
	BeforeSnapshot = CaptureAuthoritySnapshot();
	LogScenarioBegin(EScenario::InactivePlayer);
	if (!PrimaryPickup.IsValid())
	{
		FinishCurrentScenario(false, FName(TEXT("FixtureError")), TEXT("MissingPickup"));
		return;
	}

	CurrentRequestId = AllocateRequestId();
	FActionSpec Action;
	Action.ClientIndex = 0;
	Action.Action = ActionPickup;
	Action.TargetPickup = PrimaryPickup;
	Action.TargetInstanceId = PrimaryPickup->GetScrollInstance().InstanceId;
	Action.RequestId = CurrentRequestId;
	ArmActions({Action});
}

void FSPAdversarialTestCoordinator::BeginPositiveExtraction()
{
	if (!AllPlayersHaveInventory())
	{
		FinishCurrentScenario(false, FName(TEXT("FixtureError")), TEXT("MissingPositiveInventory"));
		return;
	}

	CurrentStage = 1;
	TArray<FActionSpec> Actions;
	for (int32 ClientIndex = 0; ClientIndex < PartySize; ++ClientIndex)
	{
		FActionSpec& Action = Actions.AddDefaulted_GetRef();
		Action.ClientIndex = ClientIndex;
		Action.Action = ActionExtract;
		Action.RequestId = 0;
	}
	ArmActions(Actions);
}

void FSPAdversarialTestCoordinator::PlaceParticipants()
{
	for (int32 ClientIndex = 0; ClientIndex < PartySize; ++ClientIndex)
	{
		if (ASPCharacter* Character = GetCharacter(ClientIndex))
		{
			if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
			{
				Movement->StopMovementImmediately();
			}
			const FVector Location(-700.0f, -300.0f + ClientIndex * 200.0f, 110.0f);
			Character->SetActorLocationAndRotation(
				Location,
				FRotator::ZeroRotator,
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
		}
	}
}

ASPCharacter* FSPAdversarialTestCoordinator::GetCharacter(const int32 ClientIndex) const
{
	const TWeakObjectPtr<ASPPlayerController>* Controller = Clients.Find(ClientIndex);
	return Controller && Controller->IsValid()
		? Cast<ASPCharacter>((*Controller)->GetPawn())
		: nullptr;
}

ASPPlayerState* FSPAdversarialTestCoordinator::GetPlayerState(const int32 ClientIndex) const
{
	const TWeakObjectPtr<ASPPlayerController>* Controller = Clients.Find(ClientIndex);
	return Controller && Controller->IsValid()
		? (*Controller)->GetPlayerState<ASPPlayerState>()
		: nullptr;
}

ASPScrollPickup* FSPAdversarialTestCoordinator::SpawnPickup(const FVector& Location)
{
	ASPGameMode* ScrollGameMode = GameMode.Get();
	UWorld* World = ScrollGameMode ? ScrollGameMode->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = FName(*FString::Printf(TEXT("SP_ADV_Pickup_%d"), FixtureSerial + 1));
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASPScrollPickup* Pickup = World->SpawnActor<ASPScrollPickup>(
		ASPScrollPickup::StaticClass(),
		FTransform(FRotator::ZeroRotator, Location),
		SpawnParameters);
	if (Pickup)
	{
		Pickup->InitializeScroll(MakeFixtureScroll());
		Pickup->ForceNetUpdate();
		ScenarioFixtures.Add(Pickup);
	}
	return Pickup;
}

AActor* FSPAdversarialTestCoordinator::SpawnLineOfSightBlocker(const FVector& Location)
{
	ASPGameMode* ScrollGameMode = GameMode.Get();
	UWorld* World = ScrollGameMode ? ScrollGameMode->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = FName(*FString::Printf(TEXT("SP_ADV_Blocker_%d"), FixtureSerial + 1));
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Blocker = World->SpawnActor<ASPGrayboxBlock>(
		ASPGrayboxBlock::StaticClass(),
		FTransform(FRotator::ZeroRotator, Location, FVector(0.20f, 1.0f, 2.0f)),
		SpawnParameters);
	if (Blocker)
	{
		ScenarioFixtures.Add(Blocker);
	}
	return Blocker;
}

FSPScrollInstance FSPAdversarialTestCoordinator::MakeFixtureScroll()
{
	++FixtureSerial;
	FSPScrollInstance Item;
	Item.InstanceId = FGuid(
		0x53504144u,
		static_cast<uint32>(Seed),
		static_cast<uint32>(ScenarioIndex + 1),
		static_cast<uint32>(FixtureSerial));
	Item.BaseDefinitionId = FPrimaryAssetId(
		USPScrollDefinition::PrimaryAssetType,
		TEXT("DA_Scroll_VeilOfSilence"));
	Item.EngravingDefinitionId = FPrimaryAssetId(
		USPScrollEngravingDefinition::PrimaryAssetType,
		TEXT("DA_Engraving_Stable"));
	Item.Quality = ESPScrollQuality::B;
	Item.Contamination = 0.0f;
	Item.Misfire = ESPMisfireType::None;
	return Item;
}

void FSPAdversarialTestCoordinator::CleanupScenarioFixtures()
{
	RemoveTemporaryInventoryItems();
	for (const TWeakObjectPtr<AActor>& Fixture : ScenarioFixtures)
	{
		if (AActor* Actor = Fixture.Get())
		{
			Actor->Destroy();
		}
	}
	ScenarioFixtures.Reset();
	PrimaryPickup.Reset();
	SecondaryPickup.Reset();
	CurrentActions.Reset();
	ArmedClients.Reset();
	ClientReports.Reset();
}

void FSPAdversarialTestCoordinator::FillInventoryForCapacityCase(const int32 ClientIndex)
{
	ASPCharacter* Character = GetCharacter(ClientIndex);
	if (!Character)
	{
		return;
	}

	USPInventoryComponent& Inventory = Character->GetInventory();
	TArray<FGuid>& AddedIds = TemporaryInventoryItems.FindOrAdd(ClientIndex);
	while (Inventory.HasCapacity())
	{
		const FSPScrollInstance Item = MakeFixtureScroll();
		if (!Inventory.TryAddItem(Item))
		{
			break;
		}
		AddedIds.Add(Item.InstanceId);
	}
}

void FSPAdversarialTestCoordinator::RemoveTemporaryInventoryItems()
{
	for (const TPair<int32, TArray<FGuid>>& Entry : TemporaryInventoryItems)
	{
		ASPCharacter* Character = GetCharacter(Entry.Key);
		if (!Character)
		{
			continue;
		}
		for (const FGuid& InstanceId : Entry.Value)
		{
			FSPScrollInstance RemovedItem;
			Character->GetInventory().RemoveItemByInstanceId(InstanceId, RemovedItem);
		}
	}
	TemporaryInventoryItems.Reset();
}

FSPAdversarialTestCoordinator::FAuthoritySnapshot
FSPAdversarialTestCoordinator::CaptureAuthoritySnapshot() const
{
	FAuthoritySnapshot Snapshot;
	for (int32 ClientIndex = 0; ClientIndex < PartySize; ++ClientIndex)
	{
		if (const ASPCharacter* Character = GetCharacter(ClientIndex))
		{
			Snapshot.InventoryCount += Character->GetInventory().GetItemCount();
			Snapshot.SilenceEndServerTimeTotal += Character->GetSilenceEndServerTime();
		}
		if (const ASPPlayerState* PlayerState = GetPlayerState(ClientIndex))
		{
			Snapshot.PickedUpCount += PlayerState->GetPickedUpCount();
			Snapshot.ConsumedCount += PlayerState->GetConsumedScrollCount();
			Snapshot.ExtractedCount += PlayerState->IsExtracted() ? 1 : 0;
			Snapshot.ExtractedScrollCount += PlayerState->GetExtractedScrollCount();
			Snapshot.GoldDelta += PlayerState->GetGoldDelta();
		}
	}
	return Snapshot;
}

bool FSPAdversarialTestCoordinator::IsPrimaryPickupAvailable() const
{
	return PrimaryPickup.IsValid() && PrimaryPickup->IsAvailable();
}

bool FSPAdversarialTestCoordinator::AllPlayersHaveInventory() const
{
	for (int32 ClientIndex = 0; ClientIndex < PartySize; ++ClientIndex)
	{
		const ASPCharacter* Character = GetCharacter(ClientIndex);
		if (!Character || Character->GetInventory().GetItemCount() <= 0)
		{
			return false;
		}
	}
	return true;
}

bool FSPAdversarialTestCoordinator::AllPlayersExtractedWithPositiveResult() const
{
	const ASPGameMode* ScrollGameMode = GameMode.Get();
	const ASPGameState* ScrollGameState = ScrollGameMode
		? ScrollGameMode->GetGameState<ASPGameState>()
		: nullptr;
	if (!ScrollGameState || !ScrollGameState->IsSettlementCommitted())
	{
		return false;
	}

	bool bAnyPositiveResult = false;
	for (int32 ClientIndex = 0; ClientIndex < PartySize; ++ClientIndex)
	{
		const ASPPlayerState* PlayerState = GetPlayerState(ClientIndex);
		if (!PlayerState || !PlayerState->IsExtracted())
		{
			return false;
		}
		bAnyPositiveResult |=
			PlayerState->GetExtractedScrollCount() > 0 &&
			PlayerState->GetGoldDelta() > 0;
	}
	return bAnyPositiveResult;
}

int32 FSPAdversarialTestCoordinator::CountInventoryOwners(
	const FGuid& InstanceId,
	int32& OutOwnerClientIndex) const
{
	OutOwnerClientIndex = INDEX_NONE;
	int32 OwnerCount = 0;
	for (int32 ClientIndex = 0; ClientIndex < PartySize; ++ClientIndex)
	{
		const ASPCharacter* Character = GetCharacter(ClientIndex);
		if (Character && Character->GetInventory().FindItemByInstanceId(InstanceId))
		{
			++OwnerCount;
			OutOwnerClientIndex = ClientIndex;
		}
	}
	return OwnerCount;
}

void FSPAdversarialTestCoordinator::LogScenarioBegin(const EScenario Scenario)
{
	UE_LOG(LogSPAdversarial, Display,
		TEXT("SP_ADV_CASE_BEGIN scenario=%s build_version=%s commit_sha=%s run_id=%s session_id=%s seed=%d party_size=%d rtt_ms=%d loss_pct=%d server_frame=%llu state_before=%s"),
		GetScenarioName(Scenario),
		*SanitizeToken(BuildVersion),
		*SanitizeToken(CommitSha),
		*SanitizeToken(RunId),
		*GetSessionIdToken(),
		Seed,
		PartySize,
		RttMs,
		LossPct,
		static_cast<unsigned long long>(GFrameCounter),
		*BeforeSnapshot.ToToken());
}

void FSPAdversarialTestCoordinator::LogActionResult(
	const FActionSpec& Action,
	const FClientReport& Report) const
{
	const FString TargetId = Action.TargetInstanceId.IsValid()
		? Action.TargetInstanceId.ToString(EGuidFormats::DigitsWithHyphensLower)
		: TEXT("none");
	UE_LOG(LogSPAdversarial, Display,
		TEXT("SP_ADV_ACTION_RESULT build_version=%s commit_sha=%s run_id=%s session_id=%s seed=%d scenario_id=%s party_size=%d rtt_ms=%d loss_pct=%d client_id=%d request_id=%u action=%s target_id=%s server_frame=%llu result=%s state_before=%s state_after=%s client_state=%s dispatched=%d"),
		*SanitizeToken(BuildVersion),
		*SanitizeToken(CommitSha),
		*SanitizeToken(RunId),
		*GetSessionIdToken(),
		Seed,
		GetScenarioName(Scenarios[ScenarioIndex]),
		PartySize,
		RttMs,
		LossPct,
		Action.ClientIndex,
		Action.RequestId,
		*SanitizeToken(Action.Action.ToString()),
		*TargetId,
		static_cast<unsigned long long>(GFrameCounter),
		*SanitizeToken(Report.ResultCode.ToString()),
		*BeforeSnapshot.ToToken(),
		*CaptureAuthoritySnapshot().ToToken(),
		*Report.ClientState,
		Report.bDispatched ? 1 : 0);
}

void FSPAdversarialTestCoordinator::LogRosterReady()
{
	if (bRosterLogged)
	{
		return;
	}
	bRosterLogged = true;
	UE_LOG(LogSPAdversarial, Display,
		TEXT("SP_ADV_ROSTER_READY run_id=%s connected=%d expected=%d"),
		*SanitizeToken(RunId),
		Clients.Num(),
		PartySize);
}

const TCHAR* FSPAdversarialTestCoordinator::GetScenarioName(const EScenario Scenario)
{
	switch (Scenario)
	{
	case EScenario::OutOfRange: return TEXT("OutOfRange");
	case EScenario::Obstructed: return TEXT("Obstructed");
	case EScenario::InventoryFull: return TEXT("InventoryFull");
	case EScenario::OwnershipSpoof: return TEXT("OwnershipSpoof");
	case EScenario::ConcurrentClaim: return TEXT("ConcurrentClaim");
	case EScenario::PickupReplay: return TEXT("PickupReplay");
	case EScenario::PickupRequestIdConflict: return TEXT("PickupRequestIdConflict");
	case EScenario::UseNotOwned: return TEXT("UseNotOwned");
	case EScenario::UseReplay: return TEXT("UseReplay");
	case EScenario::PositiveSettlement: return TEXT("PositiveSettlement");
	case EScenario::InactivePlayer: return TEXT("InactivePlayer");
	default: return TEXT("Unknown");
	}
}

FString FSPAdversarialTestCoordinator::SanitizeToken(const FString& Value)
{
	FString Sanitized = Value.IsEmpty() ? TEXT("none") : Value;
	Sanitized.ReplaceInline(TEXT(" "), TEXT("_"));
	Sanitized.ReplaceInline(TEXT("\t"), TEXT("_"));
	Sanitized.ReplaceInline(TEXT("\r"), TEXT("_"));
	Sanitized.ReplaceInline(TEXT("\n"), TEXT("_"));
	return Sanitized;
}

FString FSPAdversarialTestCoordinator::GetSessionIdToken() const
{
	const ASPGameMode* ScrollGameMode = GameMode.Get();
	const ASPGameState* ScrollGameState = ScrollGameMode
		? ScrollGameMode->GetGameState<ASPGameState>()
		: nullptr;
	return ScrollGameState && ScrollGameState->GetSessionId().IsValid()
		? ScrollGameState->GetSessionId().ToString(EGuidFormats::DigitsWithHyphensLower)
		: TEXT("pending");
}

uint32 FSPAdversarialTestCoordinator::AllocateRequestId()
{
	const uint32 RequestId = NextRequestId++;
	if (NextRequestId == 0)
	{
		NextRequestId = 0xA0000000u;
	}
	return RequestId == 0 ? AllocateRequestId() : RequestId;
}

double FSPAdversarialTestCoordinator::GetWorldTimeSeconds() const
{
	const ASPGameMode* ScrollGameMode = GameMode.Get();
	const UWorld* World = ScrollGameMode ? ScrollGameMode->GetWorld() : nullptr;
	return World ? World->GetTimeSeconds() : 0.0;
}

#endif
