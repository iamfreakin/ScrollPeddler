#include "Game/SPGameState.h"

#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSPGameState, Log, All);

ASPGameState::ASPGameState()
{
	bReplicates = true;
}

void ASPGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASPGameState, SessionId);
	DOREPLIFETIME(ASPGameState, ExpectedPlayers);
	DOREPLIFETIME(ASPGameState, SessionPhase);
	DOREPLIFETIME(ASPGameState, ExtractedPlayerCount);
	DOREPLIFETIME(ASPGameState, bSettlementCommitted);
	DOREPLIFETIME(ASPGameState, RunId);
	DOREPLIFETIME(ASPGameState, RunPhase);
	DOREPLIFETIME(ASPGameState, RunStartServerTime);
	DOREPLIFETIME(ASPGameState, ExpeditionDeadlineServerTime);
	DOREPLIFETIME(ASPGameState, CollapseDeadlineServerTime);
	DOREPLIFETIME(ASPGameState, RunRosterPlayerCount);
	DOREPLIFETIME(ASPGameState, ActiveRunPlayerCount);
	DOREPLIFETIME(ASPGameState, DisconnectedRunPlayerCount);
	DOREPLIFETIME(ASPGameState, RunExtractedPlayerCount);
	DOREPLIFETIME(ASPGameState, MissingRunPlayerCount);
}

void ASPGameState::AuthorityInitializeSession(const FGuid& InSessionId, const int32 InExpectedPlayers)
{
	if (!HasAuthority())
	{
		return;
	}

	SessionId = InSessionId;
	ExpectedPlayers = FMath::Clamp(InExpectedPlayers, 1, 4);
	SessionPhase = ESPSessionPhase::LobbyCreated;
	ExtractedPlayerCount = 0;
	bSettlementCommitted = false;
	AuthorityInitializeRun(SessionId, ExpectedPlayers);
	ForceNetUpdate();

	UE_LOG(LogSPGameState, Display,
		TEXT("SP_SPIKE_SESSION_STATE session=%s expected_players=%d phase=%d"),
		*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), ExpectedPlayers,
		static_cast<int32>(SessionPhase));
}

void ASPGameState::AuthoritySetPhase(const ESPSessionPhase InPhase)
{
	if (!HasAuthority() || bSettlementCommitted || SessionPhase == InPhase)
	{
		return;
	}

	SessionPhase = InPhase;
	if (SessionPhase == ESPSessionPhase::InExpedition)
	{
		AuthorityStartExpedition();
	}
	else if (SessionPhase == ESPSessionPhase::SettlementPending)
	{
		AuthoritySetRunPhase(ESPRunPhase::Settlement);
	}
	ForceNetUpdate();
	UE_LOG(LogSPGameState, Display,
		TEXT("SP_SPIKE_PHASE session=%s phase=%d"),
		*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), static_cast<int32>(SessionPhase));
}

void ASPGameState::AuthoritySetExtractedPlayerCount(const int32 InExtractedPlayerCount)
{
	if (!HasAuthority() || bSettlementCommitted)
	{
		return;
	}

	ExtractedPlayerCount = FMath::Clamp(InExtractedPlayerCount, 0, ExpectedPlayers);
	if (RunRosterPlayerCount > 0)
	{
		RunExtractedPlayerCount = FMath::Clamp(
			ExtractedPlayerCount, 0, RunRosterPlayerCount);
		ActiveRunPlayerCount = FMath::Max(
			0,
			RunRosterPlayerCount - DisconnectedRunPlayerCount
				- RunExtractedPlayerCount - MissingRunPlayerCount);
	}
	ForceNetUpdate();
	UE_LOG(LogSPGameState, Display,
		TEXT("SP_SPIKE_EXTRACTION_PROGRESS session=%s extracted=%d expected=%d"),
		*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower),
		ExtractedPlayerCount, ExpectedPlayers);
}

void ASPGameState::AuthorityMarkSettlementCommitted()
{
	if (!HasAuthority() || bSettlementCommitted)
	{
		return;
	}

	SessionPhase = ESPSessionPhase::SettlementCommitted;
	bSettlementCommitted = true;
	AuthoritySetRunPhase(ESPRunPhase::Settlement);
	ForceNetUpdate();
	UE_LOG(LogSPGameState, Display,
		TEXT("SP_SPIKE_SETTLEMENT_COMMITTED session=%s"),
		*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower));
}

bool ASPGameState::AuthorityInitializeRun(
	const FGuid& InRunId,
	const int32 InRosterPlayerCount)
{
	if (!HasAuthority() || !InRunId.IsValid()
		|| InRosterPlayerCount < 1 || InRosterPlayerCount > 4)
	{
		return false;
	}

	ClearRunDeadlineTimers();
	RunId = InRunId;
	RunPhase = ESPRunPhase::Preparing;
	RunStartServerTime = 0.0;
	ExpeditionDeadlineServerTime = 0.0;
	CollapseDeadlineServerTime = 0.0;
	RunRosterPlayerCount = InRosterPlayerCount;
	ActiveRunPlayerCount = 0;
	DisconnectedRunPlayerCount = 0;
	RunExtractedPlayerCount = 0;
	MissingRunPlayerCount = 0;
	ForceNetUpdate();

	UE_LOG(LogSPGameState, Display,
		TEXT("SP_RUN_INITIALIZED run=%s roster=%d"),
		*RunId.ToString(EGuidFormats::DigitsWithHyphensLower),
		RunRosterPlayerCount);
	return true;
}

bool ASPGameState::AuthorityStartExpedition(const double InStartServerTime)
{
	if (!HasAuthority() || !RunId.IsValid())
	{
		return false;
	}

	if (ExpeditionDeadlineServerTime > 0.0)
	{
		return static_cast<uint8>(RunPhase)
			>= static_cast<uint8>(ESPRunPhase::Expedition);
	}

	if (RunPhase != ESPRunPhase::Preparing)
	{
		return false;
	}

	const double StartServerTime = ResolveServerTime(InStartServerTime);
	if (!FMath::IsFinite(StartServerTime) || StartServerTime < 0.0)
	{
		return false;
	}

	RunStartServerTime = StartServerTime;
	ExpeditionDeadlineServerTime =
		RunStartServerTime + SPGetExpeditionDurationSeconds();
	CollapseDeadlineServerTime =
		ExpeditionDeadlineServerTime + SPGetCollapseDurationSeconds();
	RunPhase = ESPRunPhase::Expedition;
	ActiveRunPlayerCount = FMath::Max(
		0,
		RunRosterPlayerCount - DisconnectedRunPlayerCount
			- RunExtractedPlayerCount - MissingRunPlayerCount);
	ScheduleRunDeadlineTimers();
	ForceNetUpdate();

	UE_LOG(LogSPGameState, Display,
		TEXT("SP_RUN_STARTED run=%s start=%.3f collapse=%.3f resolution=%.3f roster=%d"),
		*RunId.ToString(EGuidFormats::DigitsWithHyphensLower),
		RunStartServerTime, ExpeditionDeadlineServerTime,
		CollapseDeadlineServerTime, RunRosterPlayerCount);
	return true;
}

bool ASPGameState::AuthoritySetRunPhase(const ESPRunPhase InPhase)
{
	if (!HasAuthority())
	{
		return false;
	}

	if (InPhase == ESPRunPhase::Expedition
		&& RunPhase == ESPRunPhase::Preparing
		&& ExpeditionDeadlineServerTime <= 0.0)
	{
		return AuthorityStartExpedition();
	}

	if (RunPhase == InPhase)
	{
		return true;
	}

	if (!SPCanAdvanceRunPhase(RunPhase, InPhase))
	{
		UE_LOG(LogSPGameState, Warning,
			TEXT("SP_RUN_PHASE_REJECTED run=%s current=%d requested=%d"),
			*RunId.ToString(EGuidFormats::DigitsWithHyphensLower),
			static_cast<int32>(RunPhase), static_cast<int32>(InPhase));
		return false;
	}

	const ESPRunPhase PreviousPhase = RunPhase;
	RunPhase = InPhase;
	if (static_cast<uint8>(RunPhase) >= static_cast<uint8>(ESPRunPhase::Resolution))
	{
		ClearRunDeadlineTimers();
	}
	ForceNetUpdate();

	UE_LOG(LogSPGameState, Display,
		TEXT("SP_RUN_PHASE run=%s previous=%d current=%d"),
		*RunId.ToString(EGuidFormats::DigitsWithHyphensLower),
		static_cast<int32>(PreviousPhase), static_cast<int32>(RunPhase));
	return true;
}

bool ASPGameState::AuthoritySetRunRosterCounts(
	const int32 InActivePlayerCount,
	const int32 InDisconnectedPlayerCount,
	const int32 InRunExtractedPlayerCount,
	const int32 InMissingPlayerCount)
{
	if (!HasAuthority() || RunRosterPlayerCount < 1
		|| InActivePlayerCount < 0 || InDisconnectedPlayerCount < 0
		|| InRunExtractedPlayerCount < 0 || InMissingPlayerCount < 0)
	{
		return false;
	}

	const int32 AccountedPlayers = InActivePlayerCount
		+ InDisconnectedPlayerCount
		+ InRunExtractedPlayerCount
		+ InMissingPlayerCount;
	if (AccountedPlayers > RunRosterPlayerCount)
	{
		UE_LOG(LogSPGameState, Warning,
			TEXT("SP_RUN_ROSTER_REJECTED run=%s accounted=%d roster=%d"),
			*RunId.ToString(EGuidFormats::DigitsWithHyphensLower),
			AccountedPlayers, RunRosterPlayerCount);
		return false;
	}

	if (ActiveRunPlayerCount == InActivePlayerCount
		&& DisconnectedRunPlayerCount == InDisconnectedPlayerCount
		&& RunExtractedPlayerCount == InRunExtractedPlayerCount
		&& MissingRunPlayerCount == InMissingPlayerCount)
	{
		return true;
	}

	ActiveRunPlayerCount = InActivePlayerCount;
	DisconnectedRunPlayerCount = InDisconnectedPlayerCount;
	RunExtractedPlayerCount = InRunExtractedPlayerCount;
	MissingRunPlayerCount = InMissingPlayerCount;
	ForceNetUpdate();

	UE_LOG(LogSPGameState, Display,
		TEXT("SP_RUN_ROSTER run=%s active=%d disconnected=%d extracted=%d missing=%d unaccounted=%d"),
		*RunId.ToString(EGuidFormats::DigitsWithHyphensLower),
		ActiveRunPlayerCount, DisconnectedRunPlayerCount,
		RunExtractedPlayerCount, MissingRunPlayerCount,
		RunRosterPlayerCount - AccountedPlayers);
	return true;
}

bool ASPGameState::AuthorityRefreshRunPhase(const double CurrentServerTime)
{
	if (!HasAuthority() || ExpeditionDeadlineServerTime <= 0.0
		|| CollapseDeadlineServerTime <= ExpeditionDeadlineServerTime)
	{
		return false;
	}

	const double ServerTime = ResolveServerTime(CurrentServerTime);
	if (!FMath::IsFinite(ServerTime) || ServerTime < 0.0)
	{
		return false;
	}

	if (ServerTime >= CollapseDeadlineServerTime)
	{
		return AuthoritySetRunPhase(ESPRunPhase::Resolution);
	}

	if (ServerTime >= ExpeditionDeadlineServerTime)
	{
		return AuthoritySetRunPhase(ESPRunPhase::Collapse);
	}

	return RunPhase == ESPRunPhase::Expedition;
}

int32 ASPGameState::GetUnresolvedRunPlayerCount() const
{
	return FMath::Max(
		0,
		RunRosterPlayerCount - RunExtractedPlayerCount - MissingRunPlayerCount);
}

double ASPGameState::GetSecondsUntilCollapse() const
{
	if (ExpeditionDeadlineServerTime <= 0.0)
	{
		return 0.0;
	}

	return FMath::Max(
		0.0, ExpeditionDeadlineServerTime - ResolveServerTime(-1.0));
}

double ASPGameState::GetSecondsUntilResolution() const
{
	if (CollapseDeadlineServerTime <= 0.0)
	{
		return 0.0;
	}

	return FMath::Max(
		0.0, CollapseDeadlineServerTime - ResolveServerTime(-1.0));
}

void ASPGameState::ScheduleRunDeadlineTimers()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}

	ClearRunDeadlineTimers();
	const double CurrentServerTime = ResolveServerTime(-1.0);
	const float UntilCollapse = static_cast<float>(
		FMath::Max(0.0, ExpeditionDeadlineServerTime - CurrentServerTime));
	const float UntilResolution = static_cast<float>(
		FMath::Max(0.0, CollapseDeadlineServerTime - CurrentServerTime));

	World->GetTimerManager().SetTimer(
		ExpeditionDeadlineTimerHandle,
		this,
		&ASPGameState::HandleExpeditionDeadline,
		FMath::Max(KINDA_SMALL_NUMBER, UntilCollapse),
		false);
	World->GetTimerManager().SetTimer(
		CollapseDeadlineTimerHandle,
		this,
		&ASPGameState::HandleCollapseDeadline,
		FMath::Max(KINDA_SMALL_NUMBER, UntilResolution),
		false);
}

void ASPGameState::ClearRunDeadlineTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ExpeditionDeadlineTimerHandle);
		World->GetTimerManager().ClearTimer(CollapseDeadlineTimerHandle);
	}
}

void ASPGameState::HandleExpeditionDeadline()
{
	AuthorityRefreshRunPhase();
}

void ASPGameState::HandleCollapseDeadline()
{
	AuthorityRefreshRunPhase();
}

double ASPGameState::ResolveServerTime(const double RequestedServerTime) const
{
	if (RequestedServerTime >= 0.0)
	{
		return RequestedServerTime;
	}

	return GetWorld()
		? static_cast<double>(GetServerWorldTimeSeconds())
		: 0.0;
}

void ASPGameState::OnRep_SessionMetadata()
{
	UE_LOG(LogSPGameState, Display,
		TEXT("SP_SPIKE_SESSION_REPLICATED session=%s expected_players=%d"),
		*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), ExpectedPlayers);
}

void ASPGameState::OnRep_SessionPhase()
{
	UE_LOG(LogSPGameState, Display,
		TEXT("SP_SPIKE_PHASE_REPLICATED session=%s phase=%d committed=%d"),
		*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower),
		static_cast<int32>(SessionPhase), bSettlementCommitted ? 1 : 0);
}

void ASPGameState::OnRep_ExtractionProgress()
{
	UE_LOG(LogSPGameState, Display,
		TEXT("SP_SPIKE_EXTRACTION_REPLICATED session=%s extracted=%d expected=%d"),
		*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower),
		ExtractedPlayerCount, ExpectedPlayers);
}

void ASPGameState::OnRep_RunMetadata()
{
	UE_LOG(LogSPGameState, Display,
		TEXT("SP_RUN_METADATA_REPLICATED run=%s start=%.3f collapse=%.3f resolution=%.3f"),
		*RunId.ToString(EGuidFormats::DigitsWithHyphensLower),
		RunStartServerTime, ExpeditionDeadlineServerTime,
		CollapseDeadlineServerTime);
}

void ASPGameState::OnRep_RunPhase()
{
	UE_LOG(LogSPGameState, Display,
		TEXT("SP_RUN_PHASE_REPLICATED run=%s phase=%d"),
		*RunId.ToString(EGuidFormats::DigitsWithHyphensLower),
		static_cast<int32>(RunPhase));
}

void ASPGameState::OnRep_RunRoster()
{
	UE_LOG(LogSPGameState, Verbose,
		TEXT("SP_RUN_ROSTER_REPLICATED run=%s roster=%d active=%d disconnected=%d extracted=%d missing=%d"),
		*RunId.ToString(EGuidFormats::DigitsWithHyphensLower),
		RunRosterPlayerCount, ActiveRunPlayerCount,
		DisconnectedRunPlayerCount, RunExtractedPlayerCount,
		MissingRunPlayerCount);
}
