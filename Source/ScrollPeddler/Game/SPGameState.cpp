#include "Game/SPGameState.h"

#include "Net/UnrealNetwork.h"

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
	ForceNetUpdate();
	UE_LOG(LogSPGameState, Display,
		TEXT("SP_SPIKE_SETTLEMENT_COMMITTED session=%s"),
		*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower));
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
