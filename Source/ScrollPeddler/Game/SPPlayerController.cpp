#include "Game/SPPlayerController.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/SPGameMode.h"
#include "Game/SPGameState.h"
#include "Game/SPPlayerState.h"
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
	StartAutoSpikeIfRequested();
#if !UE_BUILD_SHIPPING
	StartAdversarialSuiteIfRequested();
#endif
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

#if !UE_BUILD_SHIPPING
void ASPPlayerController::StartAdversarialSuiteIfRequested()
{
#if UE_BUILD_SHIPPING
	return;
#else
	if (!IsLocalController() ||
		!FParse::Param(FCommandLine::Get(), TEXT("SPAdversarialSuite")) ||
		!GetWorld())
	{
		return;
	}

	FParse::Value(FCommandLine::Get(), TEXT("SPAdversarialRunId="), AdversarialRunId);
	FParse::Value(FCommandLine::Get(), TEXT("SPAdversarialSeed="), AdversarialSeed);
	FParse::Value(FCommandLine::Get(), TEXT("SPClientIndex="), AdversarialClientIndex);
	FParse::Value(FCommandLine::Get(), TEXT("SPAdversarialPartySize="), AdversarialPartySize);
	if (AdversarialRunId.IsEmpty())
	{
		AdversarialRunId = TEXT("local");
	}
	if (AdversarialClientIndex < 0)
	{
		AdversarialClientIndex = GetNetMode() == NM_ListenServer ? 0 : 1;
	}

	GetWorldTimerManager().SetTimer(
		AdversarialClientTimerHandle,
		this,
		&ASPPlayerController::PollAdversarialLocalState,
		0.05f,
		true,
		0.05f);
#endif
}

void ASPPlayerController::PollAdversarialLocalState()
{
#if UE_BUILD_SHIPPING
	return;
#else
	UWorld* World = GetWorld();
	if (!World || !IsLocalController())
	{
		return;
	}

	ASPCharacter* LocalCharacter = Cast<ASPCharacter>(GetPawn());
	const ASPGameState* ScrollGameState = World->GetGameState<ASPGameState>();
	if (!bAdversarialClientRegistered)
	{
		if (!LocalCharacter || !ScrollGameState || !ScrollGameState->GetSessionId().IsValid())
		{
			return;
		}

		if (AdversarialPartySize <= 0)
		{
			AdversarialPartySize = ScrollGameState->GetExpectedPlayers();
		}
		const TCHAR* RoleToken = GetNetMode() == NM_ListenServer ? TEXT("host") : TEXT("client");
		UE_LOG(LogScrollPeddler, Display,
			TEXT("SP_ADV_SUITE_READY role=%s run_id=%s client_index=%d party_size=%d"),
			RoleToken, *AdversarialRunId, AdversarialClientIndex, AdversarialPartySize);
		bAdversarialClientRegistered = true;
		ServerRegisterAdversarialClient(
			AdversarialClientIndex,
			AdversarialRunId,
			AdversarialSeed,
			AdversarialPartySize);
		return;
	}

	if (!bAdversarialActionArmed)
	{
		GetWorldTimerManager().ClearTimer(AdversarialClientTimerHandle);
		return;
	}

	if (!bAdversarialActionReleased)
	{
		ASPCharacter* RequestedPawn = AdversarialAction == TEXT("OwnershipSpoof")
			? AdversarialActionPawn.Get()
			: LocalCharacter;
		if (!RequestedPawn)
		{
			return;
		}

		if ((AdversarialAction == TEXT("Pickup") || AdversarialAction == TEXT("OwnershipSpoof")) &&
			!bAdversarialAllowMissingTarget &&
			!AdversarialTargetPickup.IsValid())
		{
			for (TActorIterator<ASPScrollPickup> Iterator(World); Iterator; ++Iterator)
			{
				if (Iterator->GetScrollInstance().InstanceId == AdversarialTargetInstanceId)
				{
					AdversarialTargetPickup = *Iterator;
					break;
				}
			}
			if (!AdversarialTargetPickup.IsValid())
			{
				return;
			}
		}

		if (!bAdversarialActionDispatched)
		{
			bAdversarialActionDispatched = true;
			ServerReportAdversarialActionArmed(AdversarialScenarioId, AdversarialClientIndex);
		}
		return;
	}

	if (!bAdversarialActionDispatched)
	{
		bAdversarialActionDispatched = true;
		bool bDispatched = false;
		if (AdversarialAction == TEXT("Pickup"))
		{
			bDispatched = LocalCharacter && LocalCharacter->DevelopmentRequestPickup(
				AdversarialTargetPickup.Get(),
				AdversarialTargetInstanceId,
				AdversarialRequestId);
		}
		else if (AdversarialAction == TEXT("OwnershipSpoof"))
		{
			ASPCharacter* RequestedPawn = AdversarialActionPawn.Get();
			bDispatched = RequestedPawn && RequestedPawn->DevelopmentRequestPickup(
				AdversarialTargetPickup.Get(),
				AdversarialTargetInstanceId,
				AdversarialRequestId);
			ReportAdversarialActionResult(
				bDispatched ? FName(TEXT("Dispatched")) : FName(TEXT("HarnessRejected")),
				bDispatched);
			return;
		}
		else if (AdversarialAction == TEXT("Use"))
		{
			bDispatched = LocalCharacter && LocalCharacter->DevelopmentRequestUseScroll(
				AdversarialTargetInstanceId,
				AdversarialRequestId);
		}
		else if (AdversarialAction == TEXT("Extract"))
		{
			if (LocalCharacter)
			{
				LocalCharacter->ServerRequestAutoExtract();
				bDispatched = true;
			}
		}

		if (!bDispatched)
		{
			ReportAdversarialActionResult(FName(TEXT("HarnessRejected")), false);
			return;
		}
	}

	if (AdversarialAction == TEXT("Pickup") && LocalCharacter &&
		LocalCharacter->GetPickupCompletionSerial() != AdversarialPickupCompletionSerialBaseline &&
		LocalCharacter->GetLastCompletedPickupRequestId() == AdversarialRequestId)
	{
		const FString ResultName = StaticEnum<ESPPickupResultCode>()->GetNameStringByValue(
			static_cast<int64>(LocalCharacter->GetLastPickupResult()));
		ReportAdversarialActionResult(FName(*ResultName), true);
		return;
	}
	if (AdversarialAction == TEXT("Use") && LocalCharacter &&
		LocalCharacter->GetScrollUseCompletionSerial() != AdversarialUseCompletionSerialBaseline &&
		LocalCharacter->GetLastCompletedUseRequestId() == AdversarialRequestId)
	{
		const FString ResultName = StaticEnum<ESPScrollUseResultCode>()->GetNameStringByValue(
			static_cast<int64>(LocalCharacter->GetLastScrollUseResult()));
		ReportAdversarialActionResult(FName(*ResultName), true);
		return;
	}
	if (AdversarialAction == TEXT("Extract"))
	{
		const ASPPlayerState* ScrollPlayerState = GetPlayerState<ASPPlayerState>();
		if (ScrollPlayerState && ScrollPlayerState->IsExtracted())
		{
			ReportAdversarialActionResult(FName(TEXT("Extracted")), true);
			return;
		}
	}

	if (World->GetTimeSeconds() >= AdversarialActionDeadlineSeconds)
	{
		ReportAdversarialActionResult(FName(TEXT("ClientTimeout")), true);
	}
#endif
}

void ASPPlayerController::ResetAdversarialClientAction()
{
	bAdversarialActionArmed = false;
	bAdversarialActionReleased = false;
	bAdversarialActionDispatched = false;
	bAdversarialAllowMissingTarget = false;
	AdversarialActionDeadlineSeconds = 0.0;
	AdversarialScenarioId = NAME_None;
	AdversarialAction = NAME_None;
	AdversarialTargetInstanceId.Invalidate();
	AdversarialRequestId = 0;
	AdversarialPickupCompletionSerialBaseline = 0;
	AdversarialUseCompletionSerialBaseline = 0;
	AdversarialActionPawn.Reset();
	AdversarialTargetPickup.Reset();
}

void ASPPlayerController::ReportAdversarialActionResult(
	const FName ResultCode,
	const bool bDispatched)
{
	ServerReportAdversarialActionResult(
		AdversarialScenarioId,
		AdversarialClientIndex,
		AdversarialRequestId,
		ResultCode,
		bDispatched,
		BuildAdversarialClientStateToken());
	ResetAdversarialClientAction();
	GetWorldTimerManager().ClearTimer(AdversarialClientTimerHandle);
}

FString ASPPlayerController::BuildAdversarialClientStateToken() const
{
	const ASPCharacter* ScrollCharacter = Cast<ASPCharacter>(GetPawn());
	const ASPPlayerState* ScrollPlayerState = GetPlayerState<ASPPlayerState>();
	return FString::Printf(
		TEXT("inventory:%d|picked:%d|consumed:%d|extracted:%d"),
		ScrollCharacter ? ScrollCharacter->GetInventory().GetItemCount() : -1,
		ScrollPlayerState ? ScrollPlayerState->GetPickedUpCount() : -1,
		ScrollPlayerState ? ScrollPlayerState->GetConsumedScrollCount() : -1,
		ScrollPlayerState && ScrollPlayerState->IsExtracted() ? 1 : 0);
}
#endif

void ASPPlayerController::ServerRegisterAdversarialClient_Implementation(
	const int32 ClientIndex,
	const FString& RunId,
	const int32 Seed,
	const int32 PartySize)
{
#if !UE_BUILD_SHIPPING
	if (ASPGameMode* ScrollGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASPGameMode>() : nullptr)
	{
		ScrollGameMode->RegisterAdversarialClient(this, ClientIndex, RunId, Seed, PartySize);
	}
#endif
}

void ASPPlayerController::ClientArmAdversarialAction_Implementation(
	const FName ScenarioId,
	const FName Action,
	ASPCharacter* ActionPawn,
	ASPScrollPickup* TargetPickup,
	const FGuid TargetInstanceId,
	const uint32 RequestId,
	const bool bAllowMissingTarget)
{
#if !UE_BUILD_SHIPPING
	if (!IsLocalController() ||
		!FParse::Param(FCommandLine::Get(), TEXT("SPAdversarialSuite")) ||
		!GetWorld())
	{
		return;
	}

	ResetAdversarialClientAction();
	AdversarialScenarioId = ScenarioId;
	AdversarialAction = Action;
	AdversarialActionPawn = ActionPawn;
	AdversarialTargetPickup = TargetPickup;
	AdversarialTargetInstanceId = TargetInstanceId;
	AdversarialRequestId = RequestId;
	if (const ASPCharacter* LocalCharacter = Cast<ASPCharacter>(GetPawn()))
	{
		AdversarialPickupCompletionSerialBaseline = LocalCharacter->GetPickupCompletionSerial();
		AdversarialUseCompletionSerialBaseline = LocalCharacter->GetScrollUseCompletionSerial();
	}
	bAdversarialAllowMissingTarget = bAllowMissingTarget;
	bAdversarialActionArmed = true;
	AdversarialActionDeadlineSeconds = GetWorld()->GetTimeSeconds() + 15.0;
	GetWorldTimerManager().SetTimer(
		AdversarialClientTimerHandle,
		this,
		&ASPPlayerController::PollAdversarialLocalState,
		0.05f,
		true,
		0.0f);
#endif
}

void ASPPlayerController::ServerReportAdversarialActionArmed_Implementation(
	const FName ScenarioId,
	const int32 ClientIndex)
{
#if !UE_BUILD_SHIPPING
	if (ASPGameMode* ScrollGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASPGameMode>() : nullptr)
	{
		ScrollGameMode->ReportAdversarialActionArmed(this, ScenarioId, ClientIndex);
	}
#endif
}

void ASPPlayerController::ClientReleaseAdversarialAction_Implementation(const FName ScenarioId)
{
#if !UE_BUILD_SHIPPING
	if (bAdversarialActionArmed && AdversarialScenarioId == ScenarioId)
	{
		bAdversarialActionReleased = true;
		bAdversarialActionDispatched = false;
		PollAdversarialLocalState();
	}
#endif
}

void ASPPlayerController::ServerReportAdversarialActionResult_Implementation(
	const FName ScenarioId,
	const int32 ClientIndex,
	const uint32 RequestId,
	const FName ResultCode,
	const bool bDispatched,
	const FString& ClientState)
{
#if !UE_BUILD_SHIPPING
	if (ASPGameMode* ScrollGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASPGameMode>() : nullptr)
	{
		ScrollGameMode->ReportAdversarialActionResult(
			this,
			ScenarioId,
			ClientIndex,
			RequestId,
			ResultCode,
			bDispatched,
			ClientState);
	}
#endif
}

void ASPPlayerController::ClientFinishAdversarialSuite_Implementation(const bool bPassed)
{
#if !UE_BUILD_SHIPPING
	GetWorldTimerManager().ClearTimer(AdversarialClientTimerHandle);
	ResetAdversarialClientAction();
	UE_LOG(LogScrollPeddler, Display,
		TEXT("SP_ADV_CLIENT_FINISHED passed=%d run_id=%s client_index=%d"),
		bPassed ? 1 : 0,
		*AdversarialRunId,
		AdversarialClientIndex);
	ScheduleAutoQuitIfRequested();
#endif
}
