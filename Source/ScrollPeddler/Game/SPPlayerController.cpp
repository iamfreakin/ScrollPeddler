#include "Game/SPPlayerController.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/SPGameMode.h"
#include "Game/SPGameState.h"
#include "Game/SPPartyState.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/SPGameInstance.h"
#include "Player/SPCharacter.h"
#include "Player/SPInventoryComponent.h"
#include "ScrollPeddler.h"
#include "TimerManager.h"
#include "World/SPScrollPickup.h"

void ASPPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		if (USPOnlineSessionSubsystem* OnlineSessions =
			GetGameInstance()
				? GetGameInstance()->GetSubsystem<USPOnlineSessionSubsystem>()
				: nullptr)
		{
			OnlineSessions->OnOperationComplete.AddUniqueDynamic(
				this,
				&ASPPlayerController::HandleOnlineOperationComplete);
			OnlineSessions->OnSearchResultsUpdated.AddUniqueDynamic(
				this,
				&ASPPlayerController::HandleOnlineSearchResults);
			OnlineSessions->OnConnectStringReady.AddUniqueDynamic(
				this,
				&ASPPlayerController::HandleOnlineConnectString);
		}
	}
	StartAutoSpikeIfRequested();
}

void ASPPlayerController::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (USPOnlineSessionSubsystem* OnlineSessions =
		GetGameInstance()
			? GetGameInstance()->GetSubsystem<USPOnlineSessionSubsystem>()
			: nullptr)
	{
		OnlineSessions->OnOperationComplete.RemoveDynamic(
			this,
			&ASPPlayerController::HandleOnlineOperationComplete);
		OnlineSessions->OnSearchResultsUpdated.RemoveDynamic(
			this,
			&ASPPlayerController::HandleOnlineSearchResults);
		OnlineSessions->OnConnectStringReady.RemoveDynamic(
			this,
			&ASPPlayerController::HandleOnlineConnectString);
	}

	Super::EndPlay(EndPlayReason);
}

void ASPPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent)
	{
		return;
	}

	InputComponent->BindAction(
		TEXT("PushToTalk"),
		IE_Pressed,
		this,
		&APlayerController::StartTalking);
	InputComponent->BindAction(
		TEXT("PushToTalk"),
		IE_Released,
		this,
		&APlayerController::StopTalking);
}

void ASPPlayerController::SPHost(const int32 InExpectedPlayers)
{
	const int32 ClampedPlayers = FMath::Clamp(InExpectedPlayers, 1, 4);
	const FString Options = FString::Printf(TEXT("listen?ExpectedPlayers=%d"), ClampedPlayers);
	UE_LOG(LogScrollPeddler, Display, TEXT("SP_SPIKE_HOST_REQUEST expected_players=%d"), ClampedPlayers);
	UGameplayStatics::OpenLevel(this, TEXT("/Game/Maps/TechSpike"), true, Options);
}

void ASPPlayerController::SPJoin(const FString& Address)
{
	if (!IsLocalController() || Address.IsEmpty())
	{
		return;
	}

	UE_LOG(LogScrollPeddler, Display, TEXT("SP_SPIKE_JOIN_REQUEST address=%s"), *Address);
	ClientTravel(Address, TRAVEL_Absolute);
}

void ASPPlayerController::SPCreateLobby(const int32 MaxPlayers)
{
	if (!IsLocalController())
	{
		return;
	}

	USPOnlineSessionSubsystem* OnlineSessions =
		GetGameInstance()
			? GetGameInstance()->GetSubsystem<USPOnlineSessionSubsystem>()
			: nullptr;
	if (!OnlineSessions)
	{
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("SP_ONLINE_CREATE_REJECTED reason=missing_subsystem"));
		return;
	}

	PendingOnlineHostPlayers = FMath::Clamp(
		MaxPlayers,
		USPOnlineSessionSubsystem::MinPublicPlayers,
		USPOnlineSessionSubsystem::MaxPublicPlayers);
	UE_LOG(LogScrollPeddler, Display,
		TEXT("SP_ONLINE_CREATE_REQUEST max_players=%d"),
		PendingOnlineHostPlayers);
	OnlineSessions->CreatePublicLobby(PendingOnlineHostPlayers);
}

void ASPPlayerController::SPFindLobbies()
{
	if (!IsLocalController())
	{
		return;
	}

	if (USPOnlineSessionSubsystem* OnlineSessions =
		GetGameInstance()
			? GetGameInstance()->GetSubsystem<USPOnlineSessionSubsystem>()
			: nullptr)
	{
		OnlineSessions->FindPublicLobbies();
	}
}

void ASPPlayerController::SPQuickPlay()
{
	if (!IsLocalController())
	{
		return;
	}

	if (USPOnlineSessionSubsystem* OnlineSessions =
		GetGameInstance()
			? GetGameInstance()->GetSubsystem<USPOnlineSessionSubsystem>()
			: nullptr)
	{
		OnlineSessions->QuickPlay();
	}
}

void ASPPlayerController::SPJoinLobby(const int32 SearchResultIndex)
{
	if (!IsLocalController())
	{
		return;
	}

	if (USPOnlineSessionSubsystem* OnlineSessions =
		GetGameInstance()
			? GetGameInstance()->GetSubsystem<USPOnlineSessionSubsystem>()
			: nullptr)
	{
		OnlineSessions->JoinLobbyByIndex(SearchResultIndex);
	}
}

void ASPPlayerController::HandleOnlineOperationComplete(
	const ESPOnlineSessionOperation Operation,
	const ESPOnlineSessionResult Result)
{
	const FString OperationName =
		StaticEnum<ESPOnlineSessionOperation>()->GetNameStringByValue(
			static_cast<int64>(Operation));
	const FString ResultName =
		StaticEnum<ESPOnlineSessionResult>()->GetNameStringByValue(
			static_cast<int64>(Result));

	if (Result == ESPOnlineSessionResult::Success)
	{
		UE_LOG(LogScrollPeddler, Display,
			TEXT("SP_ONLINE_OPERATION operation=%s result=%s"),
			*OperationName,
			*ResultName);
	}
	else
	{
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("SP_ONLINE_OPERATION operation=%s result=%s"),
			*OperationName,
			*ResultName);
	}

	if (Operation != ESPOnlineSessionOperation::Create)
	{
		return;
	}

	const int32 HostPlayers = PendingOnlineHostPlayers;
	PendingOnlineHostPlayers = 0;
	if (Result != ESPOnlineSessionResult::Success
		|| HostPlayers < USPOnlineSessionSubsystem::MinPublicPlayers)
	{
		return;
	}

	const FString Options = FString::Printf(
		TEXT("listen?ExpectedPlayers=%d"),
		HostPlayers);
	UGameplayStatics::OpenLevel(
		this,
		TEXT("/Game/Maps/TechSpike"),
		true,
		Options);
}

void ASPPlayerController::HandleOnlineSearchResults(
	const TArray<FSPOnlineLobbySummary>& Results)
{
	UE_LOG(LogScrollPeddler, Display,
		TEXT("SP_ONLINE_SEARCH_RESULTS count=%d"),
		Results.Num());
	for (const FSPOnlineLobbySummary& Result : Results)
	{
		UE_LOG(LogScrollPeddler, Display,
			TEXT("SP_ONLINE_LOBBY index=%d host=%s open=%d max=%d ping=%d joinable=%d invites=%d"),
			Result.SearchResultIndex,
			*Result.HostDisplayName,
			Result.OpenPublicConnections,
			Result.MaxPlayers,
			Result.PingMs,
			Result.bJoinable ? 1 : 0,
			Result.bAllowsInvites ? 1 : 0);
	}
}

void ASPPlayerController::HandleOnlineConnectString(
	const FString& ConnectString)
{
	if (!IsLocalController() || ConnectString.IsEmpty())
	{
		return;
	}

	UE_LOG(LogScrollPeddler, Display,
		TEXT("SP_ONLINE_TRAVEL connect=%s"),
		*ConnectString);
	ClientTravel(ConnectString, TRAVEL_Absolute);
}

ASPPartyState* ASPPlayerController::FindPartyState() const
{
	if (!GetWorld())
	{
		return nullptr;
	}

	TActorIterator<ASPPartyState> Iterator(GetWorld());
	if (Iterator)
	{
		return *Iterator;
	}
	return nullptr;
}

FSPPartyActionRequest ASPPlayerController::MakePartyActionRequest() const
{
	FSPPartyActionRequest Request;
	Request.RequestId = FGuid::NewGuid();
	const ASPPartyState* CurrentPartyState = FindPartyState();
	Request.ExpectedRevision = CurrentPartyState
		? CurrentPartyState->GetGovernanceState().Revision
		: 0;
	return Request;
}

void ASPPlayerController::SPReady(const bool bReady)
{
	if (IsLocalController())
	{
		ServerSetPartyReady(bReady, MakePartyActionRequest());
	}
}

void ASPPlayerController::SPChat(const FString& Message)
{
	if (IsLocalController() && !Message.IsEmpty())
	{
		ServerSubmitPartyChat(Message, MakePartyActionRequest());
	}
}

void ASPPlayerController::SPKick(const int32 TargetPlayerId)
{
	if (IsLocalController())
	{
		ServerRequestHostKick(
			TargetPlayerId,
			MakePartyActionRequest());
	}
}

void ASPPlayerController::SPVoteKick(const int32 TargetPlayerId)
{
	if (IsLocalController())
	{
		ServerRequestKickVote(
			TargetPlayerId,
			FGuid::NewGuid(),
			MakePartyActionRequest());
	}
}

void ASPPlayerController::SPVote(
	const FString& VoteId,
	const bool bApprove)
{
	FGuid ParsedVoteId;
	if (IsLocalController() && FGuid::Parse(VoteId, ParsedVoteId))
	{
		ServerCastPartyVote(
			ParsedVoteId,
			bApprove
				? ESPPartyVoteChoice::Approve
				: ESPPartyVoteChoice::Reject,
			MakePartyActionRequest());
	}
}

void ASPPlayerController::SPMutePlayer(
	const int32 TargetPlayerId,
	const bool bMute)
{
	const ASPGameState* ScrollGameState =
		GetWorld() ? GetWorld()->GetGameState<ASPGameState>() : nullptr;
	if (!IsLocalController() || !ScrollGameState)
	{
		return;
	}

	for (APlayerState* CandidatePlayerState : ScrollGameState->PlayerArray)
	{
		if (CandidatePlayerState
			&& CandidatePlayerState->GetPlayerId() == TargetPlayerId
			&& CandidatePlayerState->GetUniqueId().IsValid())
		{
			if (bMute)
			{
				ServerMutePlayer(CandidatePlayerState->GetUniqueId());
			}
			else
			{
				ServerUnmutePlayer(CandidatePlayerState->GetUniqueId());
			}
			return;
		}
	}
}

void ASPPlayerController::StartAutoSpikeIfRequested()
{
	if (!IsLocalController() || !FParse::Param(FCommandLine::Get(), TEXT("SPAutoSpike")) || !GetWorld())
	{
		return;
	}

	AutoSpikeStep = EAutoSpikeStep::WaitingForPawn;
	AutoStepStartedAtSeconds = GetWorld()->GetTimeSeconds();
	AutoContestedBarrierStartedAtSeconds = 0.0;
	bAutoContestedAttemptMade = false;
	GetWorldTimerManager().SetTimer(
		AutoSpikeTimerHandle,
		this,
		&ASPPlayerController::RunAutoSpikeStep,
		0.5f,
		true,
		1.5f);
	UE_LOG(LogScrollPeddler, Display, TEXT("SP_SPIKE_AUTO_STARTED controller=%s"), *GetNameSafe(this));
}

void ASPPlayerController::RunAutoSpikeStep()
{
	UWorld* World = GetWorld();
	ASPCharacter* ScrollCharacter = Cast<ASPCharacter>(GetPawn());
	if (!World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	auto TransitionTo = [this, Now](const EAutoSpikeStep NewStep)
	{
		AutoSpikeStep = NewStep;
		AutoStepStartedAtSeconds = Now;
	};

	switch (AutoSpikeStep)
	{
	case EAutoSpikeStep::WaitingForPawn:
		if (ScrollCharacter)
		{
			TransitionTo(EAutoSpikeStep::WaitingForPickup);
		}
		break;

	case EAutoSpikeStep::WaitingForPickup:
		if (!ScrollCharacter)
		{
			TransitionTo(EAutoSpikeStep::WaitingForPawn);
			break;
		}
		{
			ASPScrollPickup* NearestPickup = nullptr;
			float NearestDistanceSquared = TNumericLimits<float>::Max();
			FString LowestInstanceKey;
			const bool bForceFirstContestedAttempt =
				!bAutoContestedAttemptMade &&
				FParse::Param(FCommandLine::Get(), TEXT("SPAutoContestedPickup"));
			if (bForceFirstContestedAttempt)
			{
				const ASPGameState* ScrollGameState = World->GetGameState<ASPGameState>();
				const ESPSessionPhase SessionPhase = ScrollGameState
					? ScrollGameState->GetSessionPhase()
					: ESPSessionPhase::LobbyCreated;
				const bool bRosterReady = ScrollGameState &&
					(SessionPhase == ESPSessionPhase::InExpedition ||
						SessionPhase == ESPSessionPhase::Extraction) &&
					ScrollGameState->PlayerArray.Num() >= ScrollGameState->GetExpectedPlayers();
				if (!bRosterReady)
				{
					AutoContestedBarrierStartedAtSeconds = 0.0;
					break;
				}

				if (AutoContestedBarrierStartedAtSeconds <= 0.0)
				{
					AutoContestedBarrierStartedAtSeconds = Now;
					UE_LOG(LogScrollPeddler, Display,
						TEXT("SP_SPIKE_AUTO_CONTESTED_BARRIER controller=%s players=%d expected=%d"),
						*GetNameSafe(this), ScrollGameState->PlayerArray.Num(),
						ScrollGameState->GetExpectedPlayers());
					break;
				}

				if (Now - AutoContestedBarrierStartedAtSeconds < 1.0)
				{
					break;
				}
			}
			for (TActorIterator<ASPScrollPickup> Iterator(World); Iterator; ++Iterator)
			{
				ASPScrollPickup* Candidate = *Iterator;
				if (!IsValid(Candidate) || !Candidate->GetScrollInstance().IsValid() ||
					(!bForceFirstContestedAttempt && !Candidate->IsAvailable()))
				{
					continue;
				}

				const float DistanceSquared = FVector::DistSquared(
					ScrollCharacter->GetActorLocation(), Candidate->GetActorLocation());
				const FString InstanceKey = Candidate->GetScrollInstance().InstanceId.ToString(
					EGuidFormats::Digits);
				const bool bPreferCandidate = bForceFirstContestedAttempt
					? (NearestPickup == nullptr || InstanceKey < LowestInstanceKey)
					: DistanceSquared < NearestDistanceSquared;
				if (bPreferCandidate)
				{
					NearestDistanceSquared = DistanceSquared;
					NearestPickup = Candidate;
					LowestInstanceKey = InstanceKey;
				}
			}

			if (NearestPickup)
			{
				bAutoContestedAttemptMade = true;
				AutoContestedBarrierStartedAtSeconds = 0.0;
				UE_LOG(LogScrollPeddler, Display,
					TEXT("SP_SPIKE_AUTO_PICKUP_REQUEST controller=%s pickup=%s distance=%.1f contested_first=%d"),
					*GetNameSafe(this), *GetNameSafe(NearestPickup), FMath::Sqrt(NearestDistanceSquared),
					bForceFirstContestedAttempt ? 1 : 0);
				ScrollCharacter->RequestPickup(NearestPickup);
				TransitionTo(EAutoSpikeStep::WaitingForInventory);
			}
		}
		break;

	case EAutoSpikeStep::WaitingForInventory:
		if (ScrollCharacter && ScrollCharacter->GetInventory().GetItemCount() > 0)
		{
			ScrollCharacter->RequestUseFirst();
			TransitionTo(EAutoSpikeStep::WaitingForConsumption);
		}
		else if (Now - AutoStepStartedAtSeconds > 10.0)
		{
			TransitionTo(EAutoSpikeStep::WaitingForPickup);
		}
		break;

	case EAutoSpikeStep::WaitingForConsumption:
		if (ScrollCharacter && ScrollCharacter->GetInventory().GetItemCount() == 0)
		{
			ScrollCharacter->ServerRequestAutoExtract();
			TransitionTo(EAutoSpikeStep::WaitingForSettlement);
		}
		else if (Now - AutoStepStartedAtSeconds > 10.0)
		{
			TransitionTo(EAutoSpikeStep::WaitingForInventory);
		}
		break;

	case EAutoSpikeStep::WaitingForSettlement:
		if (const ASPGameState* ScrollGameState = World->GetGameState<ASPGameState>();
			ScrollGameState && ScrollGameState->IsSettlementCommitted())
		{
			GetWorldTimerManager().ClearTimer(AutoSpikeTimerHandle);
			TransitionTo(EAutoSpikeStep::Finished);
			UE_LOG(LogScrollPeddler, Display, TEXT("SP_SPIKE_AUTO_FINISHED controller=%s"), *GetNameSafe(this));
			ScheduleAutoQuitIfRequested();
		}
		break;

	case EAutoSpikeStep::Finished:
	default:
		break;
	}
}

void ASPPlayerController::ClientCommitSessionResult_Implementation(const FSPSessionResult& Result)
{
	USPGameInstance* ScrollGameInstance = GetGameInstance<USPGameInstance>();
	const bool bSaved = ScrollGameInstance && ScrollGameInstance->CommitSessionResult(Result);
	if (bSaved)
	{
		UE_LOG(LogScrollPeddler, Display,
			TEXT("SP_SPIKE_RESULT_LOCAL_COMMIT session=%s player=%s saved=1"),
			*Result.SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), *Result.PlayerId);
	}
	else
	{
		UE_LOG(LogScrollPeddler, Error,
			TEXT("SP_SPIKE_RESULT_LOCAL_COMMIT session=%s player=%s saved=0"),
			*Result.SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), *Result.PlayerId);
	}

	ServerAcknowledgeSessionResult(Result.SessionId, Result.ResultHash, bSaved);
}

void ASPPlayerController::ServerAcknowledgeSessionResult_Implementation(
	const FGuid SessionId,
	const FString& ResultHash,
	const bool bSaved)
{
	ASPGameMode* ScrollGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASPGameMode>() : nullptr;
	if (!ScrollGameMode)
	{
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("SP_SPIKE_SETTLEMENT_ACK_REJECTED reason=missing_game_mode controller=%s"),
			*GetNameSafe(this));
		return;
	}

	ScrollGameMode->HandleSettlementAck(this, SessionId, ResultHash, bSaved);
}

void ASPPlayerController::ServerSetPartyReady_Implementation(
	const bool bReady,
	const FSPPartyActionRequest& Request)
{
	ASPGameMode* ScrollGameMode =
		GetWorld() ? GetWorld()->GetAuthGameMode<ASPGameMode>() : nullptr;
	ClientNotifyPartyAction(ScrollGameMode
		? ScrollGameMode->HandlePartyReadyRequest(
			this,
			bReady,
			Request)
		: ESPPartyActionResult::InvalidState);
}

void ASPPlayerController::ServerSubmitPartyChat_Implementation(
	const FString& Message,
	const FSPPartyActionRequest& Request)
{
	ASPGameMode* ScrollGameMode =
		GetWorld() ? GetWorld()->GetAuthGameMode<ASPGameMode>() : nullptr;
	ClientNotifyPartyAction(ScrollGameMode
		? ScrollGameMode->HandlePartyChatRequest(
			this,
			Message,
			Request)
		: ESPPartyActionResult::InvalidState);
}

void ASPPlayerController::ServerRequestHostKick_Implementation(
	const int32 TargetPlayerId,
	const FSPPartyActionRequest& Request)
{
	ASPGameMode* ScrollGameMode =
		GetWorld() ? GetWorld()->GetAuthGameMode<ASPGameMode>() : nullptr;
	ClientNotifyPartyAction(ScrollGameMode
		? ScrollGameMode->HandleHostKickRequest(
			this,
			TargetPlayerId,
			Request)
		: ESPPartyActionResult::InvalidState);
}

void ASPPlayerController::ServerRequestKickVote_Implementation(
	const int32 TargetPlayerId,
	const FGuid VoteId,
	const FSPPartyActionRequest& Request)
{
	ASPGameMode* ScrollGameMode =
		GetWorld() ? GetWorld()->GetAuthGameMode<ASPGameMode>() : nullptr;
	ClientNotifyPartyAction(ScrollGameMode
		? ScrollGameMode->HandleStartKickVoteRequest(
			this,
			TargetPlayerId,
			VoteId,
			Request)
		: ESPPartyActionResult::InvalidState);
}

void ASPPlayerController::ServerCastPartyVote_Implementation(
	const FGuid VoteId,
	const ESPPartyVoteChoice Choice,
	const FSPPartyActionRequest& Request)
{
	ASPGameMode* ScrollGameMode =
		GetWorld() ? GetWorld()->GetAuthGameMode<ASPGameMode>() : nullptr;
	ClientNotifyPartyAction(ScrollGameMode
		? ScrollGameMode->HandleCastPartyVoteRequest(
			this,
			VoteId,
			Choice,
			Request)
		: ESPPartyActionResult::InvalidState);
}

void ASPPlayerController::ClientNotifyPartyAction_Implementation(
	const ESPPartyActionResult Result)
{
	const FString ResultName =
		StaticEnum<ESPPartyActionResult>()->GetNameStringByValue(
			static_cast<int64>(Result));
	if (Result == ESPPartyActionResult::Success
		|| Result == ESPPartyActionResult::AlreadyProcessed)
	{
		UE_LOG(LogScrollPeddler, Display,
			TEXT("SP_PARTY_ACTION_RESULT controller=%s result=%s"),
			*GetNameSafe(this),
			*ResultName);
	}
	else
	{
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("SP_PARTY_ACTION_RESULT controller=%s result=%s"),
			*GetNameSafe(this),
			*ResultName);
	}
}

void ASPPlayerController::ScheduleAutoQuitIfRequested()
{
	if (!IsLocalController() || !FParse::Param(FCommandLine::Get(), TEXT("SPAutoQuit")) || !GetWorld())
	{
		return;
	}

	const float AutoQuitDelaySeconds = GetNetMode() == NM_ListenServer ? 5.0f : 1.0f;
	GetWorldTimerManager().SetTimer(
		AutoQuitTimerHandle,
		this,
		&ASPPlayerController::RequestAutoQuit,
		AutoQuitDelaySeconds,
		false);
	UE_LOG(LogScrollPeddler, Display,
		TEXT("SP_SPIKE_AUTO_QUIT_SCHEDULED controller=%s delay=%.1f net_mode=%d"),
		*GetNameSafe(this), AutoQuitDelaySeconds, static_cast<int32>(GetNetMode()));
}

void ASPPlayerController::RequestAutoQuit()
{
	UE_LOG(LogScrollPeddler, Display, TEXT("SP_SPIKE_AUTO_QUIT controller=%s"), *GetNameSafe(this));
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}
