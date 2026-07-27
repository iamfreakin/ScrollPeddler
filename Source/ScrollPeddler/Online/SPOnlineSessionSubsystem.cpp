#include "Online/SPOnlineSessionSubsystem.h"

#include "Engine/World.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogSPOnlineSession, Log, All);

const FName USPOnlineSessionSubsystem::SessionName(NAME_GameSession);
const FName USPOnlineSessionSubsystem::BuildUniqueIdSettingKey(TEXT("SP_BUILD_UNIQUE_ID"));
const FName USPOnlineSessionSubsystem::RulesVersionSettingKey(TEXT("SP_RULES_VERSION"));
const FName USPOnlineSessionSubsystem::LobbyStateSettingKey(TEXT("SP_LOBBY_STATE"));
const FName USPOnlineSessionSubsystem::OpenSlotsSettingKey(TEXT("SP_OPEN_SLOTS"));
const FName USPOnlineSessionSubsystem::JoinableSettingKey(TEXT("SP_JOINABLE"));
const FName USPOnlineSessionSubsystem::InvitesSettingKey(TEXT("SP_INVITES"));

namespace
{
constexpr int32 MaxLobbySearchResults = 100;

void ApplyAdvertisedMetadata(
	FOnlineSessionSettings& Settings,
	const ESPOnlineLobbyState LobbyState,
	const int32 OpenPublicConnections,
	const bool bJoinable,
	const bool bAllowInvites)
{
	const EOnlineDataAdvertisementType::Type Advertisement =
		EOnlineDataAdvertisementType::ViaOnlineService;
	Settings.Set(
		USPOnlineSessionSubsystem::BuildUniqueIdSettingKey,
		Settings.BuildUniqueId,
		Advertisement);
	Settings.Set(
		USPOnlineSessionSubsystem::RulesVersionSettingKey,
		USPOnlineSessionSubsystem::CurrentRulesVersion,
		Advertisement);
	Settings.Set(
		USPOnlineSessionSubsystem::LobbyStateSettingKey,
		static_cast<int32>(LobbyState),
		Advertisement);
	Settings.Set(
		USPOnlineSessionSubsystem::OpenSlotsSettingKey,
		FMath::Max(0, OpenPublicConnections),
		Advertisement);
	Settings.Set(
		USPOnlineSessionSubsystem::JoinableSettingKey,
		bJoinable,
		Advertisement);
	Settings.Set(
		USPOnlineSessionSubsystem::InvitesSettingKey,
		bAllowInvites,
		Advertisement);
	Settings.Set(
		SETTING_MAPNAME,
		LobbyState == ESPOnlineLobbyState::Hub
			? FString(TEXT("Hub"))
			: FString(TEXT("Expedition")),
		Advertisement);
}

ESPOnlineSessionResult MapJoinResult(
	const EOnJoinSessionCompleteResult::Type JoinResult)
{
	switch (JoinResult)
	{
	case EOnJoinSessionCompleteResult::Success:
	case EOnJoinSessionCompleteResult::AlreadyInSession:
		return ESPOnlineSessionResult::Success;
	case EOnJoinSessionCompleteResult::SessionIsFull:
		return ESPOnlineSessionResult::SessionFull;
	case EOnJoinSessionCompleteResult::SessionDoesNotExist:
		return ESPOnlineSessionResult::NotFound;
	case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:
		return ESPOnlineSessionResult::ConnectStringUnavailable;
	case EOnJoinSessionCompleteResult::UnknownError:
	default:
		return ESPOnlineSessionResult::UnknownError;
	}
}

FSPOnlineLobbySummary BuildLobbySummary(
	const FOnlineSessionSearchResult& SearchResult,
	const int32 SearchResultIndex)
{
	const FOnlineSession& Session = SearchResult.Session;
	const FOnlineSessionSettings& Settings = Session.SessionSettings;

	FSPOnlineLobbySummary Summary;
	Summary.SearchResultIndex = SearchResultIndex;
	Summary.HostDisplayName = Session.OwningUserName;
	Summary.MaxPlayers = Settings.NumPublicConnections;
	Summary.PingMs = FMath::Max(0, SearchResult.PingInMs);

	Summary.BuildUniqueId = Settings.BuildUniqueId;
	Settings.Get(
		USPOnlineSessionSubsystem::BuildUniqueIdSettingKey,
		Summary.BuildUniqueId);
	Settings.Get(
		USPOnlineSessionSubsystem::RulesVersionSettingKey,
		Summary.RulesVersion);

	int32 LobbyStateValue =
		static_cast<int32>(ESPOnlineLobbyState::Expedition);
	if (Settings.Get(
			USPOnlineSessionSubsystem::LobbyStateSettingKey,
			LobbyStateValue)
		&& LobbyStateValue == static_cast<int32>(ESPOnlineLobbyState::Hub))
	{
		Summary.LobbyState = ESPOnlineLobbyState::Hub;
	}
	else
	{
		Summary.LobbyState = ESPOnlineLobbyState::Expedition;
	}

	int32 AdvertisedOpenSlots = Session.NumOpenPublicConnections;
	Settings.Get(
		USPOnlineSessionSubsystem::OpenSlotsSettingKey,
		AdvertisedOpenSlots);
	Summary.OpenPublicConnections = FMath::Max(
		0,
		FMath::Min(
			Session.NumOpenPublicConnections,
			AdvertisedOpenSlots));

	bool bMetadataJoinable = false;
	Settings.Get(
		USPOnlineSessionSubsystem::JoinableSettingKey,
		bMetadataJoinable);
	Summary.bJoinable = bMetadataJoinable
		&& Settings.bShouldAdvertise
		&& Settings.bAllowJoinInProgress
		&& Summary.OpenPublicConnections > 0;

	bool bMetadataInvites = false;
	Settings.Get(
		USPOnlineSessionSubsystem::InvitesSettingKey,
		bMetadataInvites);
	Summary.bAllowsInvites =
		bMetadataInvites && Settings.bAllowInvites;
	return Summary;
}
}

ESPOnlineSessionResult SPEvaluateQuickPlayCompatibility(
	const FSPOnlineLobbySummary& Candidate,
	const int32 RequiredBuildUniqueId,
	const int32 RequiredRulesVersion)
{
	if (Candidate.MaxPlayers
			< USPOnlineSessionSubsystem::MinPublicPlayers
		|| Candidate.MaxPlayers
			> USPOnlineSessionSubsystem::MaxPublicPlayers
		|| Candidate.OpenPublicConnections < 0
		|| Candidate.OpenPublicConnections > Candidate.MaxPlayers)
	{
		return ESPOnlineSessionResult::InvalidRequest;
	}

	if (Candidate.BuildUniqueId != RequiredBuildUniqueId)
	{
		return ESPOnlineSessionResult::BuildMismatch;
	}

	if (Candidate.RulesVersion != RequiredRulesVersion)
	{
		return ESPOnlineSessionResult::RulesMismatch;
	}

	if (Candidate.LobbyState != ESPOnlineLobbyState::Hub)
	{
		return ESPOnlineSessionResult::RunInProgress;
	}

	if (!Candidate.bJoinable)
	{
		return ESPOnlineSessionResult::NotJoinable;
	}

	if (Candidate.OpenPublicConnections < 1)
	{
		return ESPOnlineSessionResult::SessionFull;
	}

	return ESPOnlineSessionResult::Success;
}

ESPOnlineSessionResult SPEvaluateInviteCompatibility(
	const FSPOnlineLobbySummary& Candidate,
	const int32 RequiredBuildUniqueId,
	const int32 RequiredRulesVersion)
{
	const ESPOnlineSessionResult JoinCompatibility =
		SPEvaluateQuickPlayCompatibility(
			Candidate,
			RequiredBuildUniqueId,
			RequiredRulesVersion);
	if (JoinCompatibility != ESPOnlineSessionResult::Success)
	{
		return JoinCompatibility;
	}

	return Candidate.bAllowsInvites
		? ESPOnlineSessionResult::Success
		: ESPOnlineSessionResult::InvitesDisabled;
}

void USPOnlineSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bDeinitializing = false;
	ResolveSessionInterface();
}

void USPOnlineSessionSubsystem::Deinitialize()
{
	bDeinitializing = true;
	ClearDelegateHandles();
	PendingInviteResult.Reset();
	PendingSessionSettings.Reset();
	LastSearch.Reset();
	SessionInterface.Reset();
	LobbyResults.Reset();
	LastConnectString.Reset();
	ActiveOperation = ESPOnlineSessionOperation::None;
	Super::Deinitialize();
}

bool USPOnlineSessionSubsystem::CreatePublicLobby(
	const int32 MaxPlayers,
	const int32 LocalUserNum)
{
	if (!BeginOperation(ESPOnlineSessionOperation::Create))
	{
		return false;
	}

	if (MaxPlayers < MinPublicPlayers || MaxPlayers > MaxPublicPlayers
		|| LocalUserNum < 0)
	{
		FinishOperation(
			ESPOnlineSessionOperation::Create,
			ESPOnlineSessionResult::InvalidRequest);
		return false;
	}

	const TSharedPtr<IOnlineSession, ESPMode::ThreadSafe> Sessions =
		ResolveSessionInterface();
	if (!Sessions.IsValid())
	{
		FinishOperation(
			ESPOnlineSessionOperation::Create,
			ESPOnlineSessionResult::OnlineUnavailable);
		return false;
	}

	if (Sessions->GetNamedSession(SessionName))
	{
		FinishOperation(
			ESPOnlineSessionOperation::Create,
			ESPOnlineSessionResult::InvalidRequest);
		return false;
	}

	PendingLocalUserNum = LocalUserNum;
	PendingSessionSettings = MakeShared<FOnlineSessionSettings>();
	FOnlineSessionSettings& Settings = *PendingSessionSettings;
	Settings.NumPublicConnections = MaxPlayers;
	Settings.NumPrivateConnections = 0;
	Settings.bIsLANMatch = false;
	Settings.bIsDedicated = false;
	Settings.bShouldAdvertise = MaxPlayers > 1;
	Settings.bAllowJoinInProgress = MaxPlayers > 1;
	Settings.bAllowInvites = MaxPlayers > 1;
	Settings.bUsesPresence = true;
	Settings.bAllowJoinViaPresence = MaxPlayers > 1;
	Settings.bAllowJoinViaPresenceFriendsOnly = false;
	Settings.bUseLobbiesIfAvailable = true;
	Settings.bUseLobbiesVoiceChatIfAvailable = false;
	Settings.BuildUniqueId = GetRequiredBuildUniqueId();
	ApplyAdvertisedMetadata(
		Settings,
		ESPOnlineLobbyState::Hub,
		MaxPlayers - 1,
		MaxPlayers > 1,
		MaxPlayers > 1);

	CreateDelegateHandle =
		Sessions->AddOnCreateSessionCompleteDelegate_Handle(
			FOnCreateSessionCompleteDelegate::CreateUObject(
				this,
				&USPOnlineSessionSubsystem::HandleCreateSessionComplete));
	if (!Sessions->CreateSession(LocalUserNum, SessionName, Settings))
	{
		ClearCreateDelegate();
		PendingSessionSettings.Reset();
		FinishOperation(
			ESPOnlineSessionOperation::Create,
			ESPOnlineSessionResult::RequestRejected);
		return false;
	}

	return true;
}

bool USPOnlineSessionSubsystem::FindPublicLobbies(
	const int32 MaxResults,
	const int32 LocalUserNum)
{
	return BeginFind(MaxResults, LocalUserNum, false);
}

bool USPOnlineSessionSubsystem::JoinLobbyByIndex(
	const int32 SearchResultIndex,
	const int32 LocalUserNum)
{
	if (!BeginOperation(ESPOnlineSessionOperation::Join))
	{
		return false;
	}
	return BeginJoin(SearchResultIndex, LocalUserNum);
}

bool USPOnlineSessionSubsystem::QuickPlay(
	const int32 MaxResults,
	const int32 LocalUserNum)
{
	return BeginFind(MaxResults, LocalUserNum, true);
}

bool USPOnlineSessionSubsystem::UpdateLobby(
	const ESPOnlineLobbyState NewLobbyState,
	const bool bJoinable,
	const bool bAllowInvites,
	const int32 OpenPublicConnections)
{
	if (!BeginOperation(ESPOnlineSessionOperation::Update))
	{
		return false;
	}

	const TSharedPtr<IOnlineSession, ESPMode::ThreadSafe> Sessions =
		ResolveSessionInterface();
	FNamedOnlineSession* NamedSession = Sessions.IsValid()
		? Sessions->GetNamedSession(SessionName)
		: nullptr;
	if (!Sessions.IsValid())
	{
		FinishOperation(
			ESPOnlineSessionOperation::Update,
			ESPOnlineSessionResult::OnlineUnavailable);
		return false;
	}

	if (!NamedSession)
	{
		FinishOperation(
			ESPOnlineSessionOperation::Update,
			ESPOnlineSessionResult::NotFound);
		return false;
	}

	if (!bOwnsLobby && !NamedSession->bHosting)
	{
		FinishOperation(
			ESPOnlineSessionOperation::Update,
			ESPOnlineSessionResult::InvalidRequest);
		return false;
	}

	const int32 OpenSlots = OpenPublicConnections >= 0
		? FMath::Clamp(
			OpenPublicConnections,
			0,
			NamedSession->SessionSettings.NumPublicConnections)
		: FMath::Clamp(
			NamedSession->NumOpenPublicConnections,
			0,
			NamedSession->SessionSettings.NumPublicConnections);
	const bool bHub = NewLobbyState == ESPOnlineLobbyState::Hub;
	const bool bEffectiveJoinable = bHub && bJoinable && OpenSlots > 0;
	const bool bEffectiveInvites = bHub && bAllowInvites && OpenSlots > 0;

	PendingLobbyState = NewLobbyState;
	PendingSessionSettings =
		MakeShared<FOnlineSessionSettings>(NamedSession->SessionSettings);
	FOnlineSessionSettings& Settings = *PendingSessionSettings;
	Settings.bShouldAdvertise = bEffectiveJoinable;
	Settings.bAllowJoinInProgress = bEffectiveJoinable;
	Settings.bAllowInvites = bEffectiveInvites;
	Settings.bUsesPresence = true;
	Settings.bAllowJoinViaPresence = bEffectiveJoinable;
	Settings.bAllowJoinViaPresenceFriendsOnly = false;
	Settings.bUseLobbiesIfAvailable = true;
	Settings.bUseLobbiesVoiceChatIfAvailable = false;
	ApplyAdvertisedMetadata(
		Settings,
		NewLobbyState,
		OpenSlots,
		bEffectiveJoinable,
		bEffectiveInvites);

	UpdateDelegateHandle =
		Sessions->AddOnUpdateSessionCompleteDelegate_Handle(
			FOnUpdateSessionCompleteDelegate::CreateUObject(
				this,
				&USPOnlineSessionSubsystem::HandleUpdateSessionComplete));
	if (!Sessions->UpdateSession(SessionName, Settings, true))
	{
		ClearUpdateDelegate();
		PendingSessionSettings.Reset();
		FinishOperation(
			ESPOnlineSessionOperation::Update,
			ESPOnlineSessionResult::RequestRejected);
		return false;
	}

	return true;
}

bool USPOnlineSessionSubsystem::DestroyLobby()
{
	if (!BeginOperation(ESPOnlineSessionOperation::Destroy))
	{
		return false;
	}

	const TSharedPtr<IOnlineSession, ESPMode::ThreadSafe> Sessions =
		ResolveSessionInterface();
	if (!Sessions.IsValid())
	{
		FinishOperation(
			ESPOnlineSessionOperation::Destroy,
			ESPOnlineSessionResult::OnlineUnavailable);
		return false;
	}

	if (!Sessions->GetNamedSession(SessionName))
	{
		FinishOperation(
			ESPOnlineSessionOperation::Destroy,
			ESPOnlineSessionResult::NotFound);
		return false;
	}

	DestroyDelegateHandle =
		Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(
				this,
				&USPOnlineSessionSubsystem::HandleDestroySessionComplete));
	if (!Sessions->DestroySession(SessionName))
	{
		ClearDestroyDelegate();
		FinishOperation(
			ESPOnlineSessionOperation::Destroy,
			ESPOnlineSessionResult::RequestRejected);
		return false;
	}

	return true;
}

int32 USPOnlineSessionSubsystem::GetRequiredBuildUniqueId()
{
	return GetBuildUniqueId();
}

bool USPOnlineSessionSubsystem::BeginOperation(
	const ESPOnlineSessionOperation RequestedOperation)
{
	if (bDeinitializing || RequestedOperation == ESPOnlineSessionOperation::None)
	{
		return false;
	}

	if (ActiveOperation != ESPOnlineSessionOperation::None)
	{
		LastResult = ESPOnlineSessionResult::Busy;
		OnOperationComplete.Broadcast(
			RequestedOperation,
			ESPOnlineSessionResult::Busy);
		return false;
	}

	ActiveOperation = RequestedOperation;
	return true;
}

void USPOnlineSessionSubsystem::FinishOperation(
	const ESPOnlineSessionOperation CompletedOperation,
	const ESPOnlineSessionResult Result)
{
	if (bDeinitializing || ActiveOperation != CompletedOperation)
	{
		return;
	}

	LastResult = Result;
	ActiveOperation = ESPOnlineSessionOperation::None;
	OnOperationComplete.Broadcast(CompletedOperation, Result);
}

bool USPOnlineSessionSubsystem::BeginFind(
	const int32 MaxResults,
	const int32 LocalUserNum,
	const bool bQuickPlay)
{
	const ESPOnlineSessionOperation Operation = bQuickPlay
		? ESPOnlineSessionOperation::QuickPlay
		: ESPOnlineSessionOperation::Find;
	if (!BeginOperation(Operation))
	{
		return false;
	}

	if (MaxResults < 1 || LocalUserNum < 0)
	{
		FinishOperation(Operation, ESPOnlineSessionResult::InvalidRequest);
		return false;
	}

	const TSharedPtr<IOnlineSession, ESPMode::ThreadSafe> Sessions =
		ResolveSessionInterface();
	if (!Sessions.IsValid())
	{
		FinishOperation(Operation, ESPOnlineSessionResult::OnlineUnavailable);
		return false;
	}

	PendingLocalUserNum = LocalUserNum;
	LastSearch = MakeShared<FOnlineSessionSearch>();
	LastSearch->MaxSearchResults = FMath::Clamp(
		MaxResults, 1, MaxLobbySearchResults);
	LastSearch->bIsLanQuery = false;
	LastSearch->QuerySettings.Set(
		SEARCH_LOBBIES,
		true,
		EOnlineComparisonOp::Equals);
	LastSearch->QuerySettings.Set(
		SEARCH_MINSLOTSAVAILABLE,
		1,
		EOnlineComparisonOp::GreaterThanEquals);
	LastSearch->QuerySettings.Set(
		BuildUniqueIdSettingKey,
		GetRequiredBuildUniqueId(),
		EOnlineComparisonOp::Equals);
	LastSearch->QuerySettings.Set(
		RulesVersionSettingKey,
		CurrentRulesVersion,
		EOnlineComparisonOp::Equals);
	LastSearch->QuerySettings.Set(
		LobbyStateSettingKey,
		static_cast<int32>(ESPOnlineLobbyState::Hub),
		EOnlineComparisonOp::Equals);
	LastSearch->QuerySettings.Set(
		JoinableSettingKey,
		true,
		EOnlineComparisonOp::Equals);

	FindDelegateHandle =
		Sessions->AddOnFindSessionsCompleteDelegate_Handle(
			FOnFindSessionsCompleteDelegate::CreateUObject(
				this,
				&USPOnlineSessionSubsystem::HandleFindSessionsComplete));
	if (!Sessions->FindSessions(LocalUserNum, LastSearch.ToSharedRef()))
	{
		ClearFindDelegate();
		LastSearch.Reset();
		FinishOperation(Operation, ESPOnlineSessionResult::RequestRejected);
		return false;
	}

	return true;
}

bool USPOnlineSessionSubsystem::BeginJoin(
	const int32 SearchResultIndex,
	const int32 LocalUserNum)
{
	const ESPOnlineSessionOperation Operation = ActiveOperation;
	if ((Operation != ESPOnlineSessionOperation::Join
			&& Operation != ESPOnlineSessionOperation::QuickPlay)
		|| LocalUserNum < 0
		|| !LastSearch.IsValid()
		|| !LastSearch->SearchResults.IsValidIndex(SearchResultIndex))
	{
		FinishOperation(Operation, ESPOnlineSessionResult::InvalidRequest);
		return false;
	}

	const TSharedPtr<IOnlineSession, ESPMode::ThreadSafe> Sessions =
		ResolveSessionInterface();
	if (!Sessions.IsValid())
	{
		FinishOperation(Operation, ESPOnlineSessionResult::OnlineUnavailable);
		return false;
	}

	if (Sessions->GetNamedSession(SessionName))
	{
		FinishOperation(Operation, ESPOnlineSessionResult::InvalidRequest);
		return false;
	}

	const FSPOnlineLobbySummary* Summary = LobbyResults.FindByPredicate(
		[SearchResultIndex](const FSPOnlineLobbySummary& Candidate)
		{
			return Candidate.SearchResultIndex == SearchResultIndex;
		});
	if (!Summary)
	{
		FinishOperation(Operation, ESPOnlineSessionResult::NotFound);
		return false;
	}

	const ESPOnlineSessionResult Compatibility =
		SPEvaluateQuickPlayCompatibility(
			*Summary,
			GetRequiredBuildUniqueId(),
			CurrentRulesVersion);
	if (Compatibility != ESPOnlineSessionResult::Success)
	{
		FinishOperation(Operation, Compatibility);
		return false;
	}

	PendingLocalUserNum = LocalUserNum;
	LastConnectString.Reset();
	const TWeakObjectPtr<USPOnlineSessionSubsystem> WeakThis(this);
	JoinDelegateHandle =
		Sessions->AddOnJoinSessionCompleteDelegate_Handle(
			FOnJoinSessionCompleteDelegate::CreateLambda(
				[WeakThis](
					const FName CompletedSessionName,
					const EOnJoinSessionCompleteResult::Type Result)
				{
					if (WeakThis.IsValid())
					{
						WeakThis->HandleJoinSessionComplete(
							CompletedSessionName,
							static_cast<uint32>(Result));
					}
				}));
	if (!Sessions->JoinSession(
			LocalUserNum,
			SessionName,
			LastSearch->SearchResults[SearchResultIndex]))
	{
		ClearJoinDelegate();
		FinishOperation(Operation, ESPOnlineSessionResult::RequestRejected);
		return false;
	}

	return true;
}

bool USPOnlineSessionSubsystem::BeginInviteJoin()
{
	const ESPOnlineSessionOperation Operation = ActiveOperation;
	if (Operation != ESPOnlineSessionOperation::AcceptInvite
		|| PendingLocalUserNum < 0
		|| !PendingInviteResult.IsValid()
		|| !PendingInviteResult->IsValid())
	{
		PendingInviteResult.Reset();
		FinishOperation(Operation, ESPOnlineSessionResult::InvalidRequest);
		return false;
	}

	const TSharedPtr<IOnlineSession, ESPMode::ThreadSafe> Sessions =
		ResolveSessionInterface();
	if (!Sessions.IsValid())
	{
		PendingInviteResult.Reset();
		FinishOperation(
			Operation,
			ESPOnlineSessionResult::OnlineUnavailable);
		return false;
	}

	if (Sessions->GetNamedSession(SessionName))
	{
		PendingInviteResult.Reset();
		FinishOperation(Operation, ESPOnlineSessionResult::InvalidRequest);
		return false;
	}

	const TWeakObjectPtr<USPOnlineSessionSubsystem> WeakThis(this);
	JoinDelegateHandle =
		Sessions->AddOnJoinSessionCompleteDelegate_Handle(
			FOnJoinSessionCompleteDelegate::CreateLambda(
				[WeakThis](
					const FName CompletedSessionName,
					const EOnJoinSessionCompleteResult::Type Result)
				{
					if (WeakThis.IsValid())
					{
						WeakThis->HandleJoinSessionComplete(
							CompletedSessionName,
							static_cast<uint32>(Result));
					}
				}));
	if (!Sessions->JoinSession(
			PendingLocalUserNum,
			SessionName,
			*PendingInviteResult))
	{
		ClearJoinDelegate();
		PendingInviteResult.Reset();
		FinishOperation(
			Operation,
			ESPOnlineSessionResult::RequestRejected);
		return false;
	}

	return true;
}

TSharedPtr<IOnlineSession, ESPMode::ThreadSafe>
USPOnlineSessionSubsystem::ResolveSessionInterface()
{
	if (SessionInterface.IsValid())
	{
		RegisterInviteAcceptedDelegate();
		return SessionInterface;
	}

	const UWorld* World = GetWorld();
	if (World)
	{
		SessionInterface = Online::GetSessionInterface(World);
	}
	RegisterInviteAcceptedDelegate();
	return SessionInterface;
}

void USPOnlineSessionSubsystem::RegisterInviteAcceptedDelegate()
{
	if (bDeinitializing
		|| !SessionInterface.IsValid()
		|| InviteAcceptedDelegateHandle.IsValid())
	{
		return;
	}

	InviteAcceptedDelegateHandle =
		SessionInterface->AddOnSessionUserInviteAcceptedDelegate_Handle(
			FOnSessionUserInviteAcceptedDelegate::CreateUObject(
				this,
				&USPOnlineSessionSubsystem::HandleSessionUserInviteAccepted));
}

void USPOnlineSessionSubsystem::ClearDelegateHandles()
{
	ClearCreateDelegate();
	ClearFindDelegate();
	ClearJoinDelegate();
	ClearUpdateDelegate();
	ClearDestroyDelegate();
	ClearInviteAcceptedDelegate();
}

void USPOnlineSessionSubsystem::ClearCreateDelegate()
{
	if (SessionInterface.IsValid() && CreateDelegateHandle.IsValid())
	{
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(
			CreateDelegateHandle);
	}
	CreateDelegateHandle.Reset();
}

void USPOnlineSessionSubsystem::ClearFindDelegate()
{
	if (SessionInterface.IsValid() && FindDelegateHandle.IsValid())
	{
		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(
			FindDelegateHandle);
	}
	FindDelegateHandle.Reset();
}

void USPOnlineSessionSubsystem::ClearJoinDelegate()
{
	if (SessionInterface.IsValid() && JoinDelegateHandle.IsValid())
	{
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(
			JoinDelegateHandle);
	}
	JoinDelegateHandle.Reset();
}

void USPOnlineSessionSubsystem::ClearUpdateDelegate()
{
	if (SessionInterface.IsValid() && UpdateDelegateHandle.IsValid())
	{
		SessionInterface->ClearOnUpdateSessionCompleteDelegate_Handle(
			UpdateDelegateHandle);
	}
	UpdateDelegateHandle.Reset();
}

void USPOnlineSessionSubsystem::ClearDestroyDelegate()
{
	if (SessionInterface.IsValid() && DestroyDelegateHandle.IsValid())
	{
		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(
			DestroyDelegateHandle);
	}
	DestroyDelegateHandle.Reset();
}

void USPOnlineSessionSubsystem::ClearInviteAcceptedDelegate()
{
	if (SessionInterface.IsValid() && InviteAcceptedDelegateHandle.IsValid())
	{
		SessionInterface
			->ClearOnSessionUserInviteAcceptedDelegate_Handle(
				InviteAcceptedDelegateHandle);
	}
	InviteAcceptedDelegateHandle.Reset();
}

void USPOnlineSessionSubsystem::HandleCreateSessionComplete(
	const FName CompletedSessionName,
	const bool bWasSuccessful)
{
	if (CompletedSessionName != SessionName
		|| ActiveOperation != ESPOnlineSessionOperation::Create)
	{
		return;
	}

	ClearCreateDelegate();
	PendingSessionSettings.Reset();
	if (bWasSuccessful)
	{
		bOwnsLobby = true;
		LobbyState = ESPOnlineLobbyState::Hub;
	}
	FinishOperation(
		ESPOnlineSessionOperation::Create,
		bWasSuccessful
			? ESPOnlineSessionResult::Success
			: ESPOnlineSessionResult::RequestRejected);
}

void USPOnlineSessionSubsystem::HandleSessionUserInviteAccepted(
	const bool bWasSuccessful,
	const int32 ControllerId,
	FUniqueNetIdPtr UserId,
	const FOnlineSessionSearchResult& InviteResult)
{
	if (!BeginOperation(ESPOnlineSessionOperation::AcceptInvite))
	{
		return;
	}

	if (!bWasSuccessful
		|| ControllerId < 0
		|| !UserId.IsValid()
		|| !InviteResult.IsValid())
	{
		FinishOperation(
			ESPOnlineSessionOperation::AcceptInvite,
			ESPOnlineSessionResult::InviteRejected);
		return;
	}

	const FSPOnlineLobbySummary InviteSummary =
		BuildLobbySummary(InviteResult, INDEX_NONE);
	const ESPOnlineSessionResult Compatibility =
		SPEvaluateInviteCompatibility(
			InviteSummary,
			GetRequiredBuildUniqueId(),
			CurrentRulesVersion);
	if (Compatibility != ESPOnlineSessionResult::Success)
	{
		FinishOperation(
			ESPOnlineSessionOperation::AcceptInvite,
			Compatibility);
		return;
	}

	const TSharedPtr<IOnlineSession, ESPMode::ThreadSafe> Sessions =
		ResolveSessionInterface();
	if (!Sessions.IsValid())
	{
		FinishOperation(
			ESPOnlineSessionOperation::AcceptInvite,
			ESPOnlineSessionResult::OnlineUnavailable);
		return;
	}

	PendingLocalUserNum = ControllerId;
	LastConnectString.Reset();
	PendingInviteResult =
		MakeShared<FOnlineSessionSearchResult>(InviteResult);

	FNamedOnlineSession* ExistingSession =
		Sessions->GetNamedSession(SessionName);
	if (!ExistingSession)
	{
		BeginInviteJoin();
		return;
	}

	if (bOwnsLobby || ExistingSession->bHosting)
	{
		PendingInviteResult.Reset();
		FinishOperation(
			ESPOnlineSessionOperation::AcceptInvite,
			ESPOnlineSessionResult::AlreadyHosting);
		return;
	}

	DestroyDelegateHandle =
		Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(
				this,
				&USPOnlineSessionSubsystem::HandleDestroySessionComplete));
	if (!Sessions->DestroySession(SessionName))
	{
		ClearDestroyDelegate();
		PendingInviteResult.Reset();
		FinishOperation(
			ESPOnlineSessionOperation::AcceptInvite,
			ESPOnlineSessionResult::RequestRejected);
	}
}

void USPOnlineSessionSubsystem::HandleFindSessionsComplete(
	const bool bWasSuccessful)
{
	const ESPOnlineSessionOperation Operation = ActiveOperation;
	if (Operation != ESPOnlineSessionOperation::Find
		&& Operation != ESPOnlineSessionOperation::QuickPlay)
	{
		return;
	}

	ClearFindDelegate();
	if (!bWasSuccessful || !LastSearch.IsValid())
	{
		LobbyResults.Reset();
		OnSearchResultsUpdated.Broadcast(LobbyResults);
		FinishOperation(Operation, ESPOnlineSessionResult::RequestRejected);
		return;
	}

	RebuildLobbySummaries();
	OnSearchResultsUpdated.Broadcast(LobbyResults);
	if (Operation == ESPOnlineSessionOperation::Find)
	{
		FinishOperation(Operation, ESPOnlineSessionResult::Success);
		return;
	}

	int32 BestSearchResultIndex = INDEX_NONE;
	int32 BestPing = MAX_int32;
	for (const FSPOnlineLobbySummary& Candidate : LobbyResults)
	{
		if (SPEvaluateQuickPlayCompatibility(
				Candidate,
				GetRequiredBuildUniqueId(),
				CurrentRulesVersion) == ESPOnlineSessionResult::Success
			&& Candidate.PingMs < BestPing)
		{
			BestPing = Candidate.PingMs;
			BestSearchResultIndex = Candidate.SearchResultIndex;
		}
	}

	if (BestSearchResultIndex == INDEX_NONE)
	{
		FinishOperation(
			ESPOnlineSessionOperation::QuickPlay,
			ESPOnlineSessionResult::NoCompatibleLobby);
		return;
	}

	BeginJoin(BestSearchResultIndex, PendingLocalUserNum);
}

void USPOnlineSessionSubsystem::HandleJoinSessionComplete(
	const FName CompletedSessionName,
	const uint32 JoinResult)
{
	const ESPOnlineSessionOperation Operation = ActiveOperation;
	if (CompletedSessionName != SessionName
		|| (Operation != ESPOnlineSessionOperation::Join
			&& Operation != ESPOnlineSessionOperation::QuickPlay
			&& Operation != ESPOnlineSessionOperation::AcceptInvite))
	{
		return;
	}

	ClearJoinDelegate();
	const ESPOnlineSessionResult MappedResult = MapJoinResult(
		static_cast<EOnJoinSessionCompleteResult::Type>(JoinResult));
	if (MappedResult != ESPOnlineSessionResult::Success)
	{
		PendingInviteResult.Reset();
		FinishOperation(Operation, MappedResult);
		return;
	}

	FString ConnectString;
	if (!TryResolveConnectString(ConnectString))
	{
		PendingInviteResult.Reset();
		FinishOperation(
			Operation,
			ESPOnlineSessionResult::ConnectStringUnavailable);
		return;
	}

	bOwnsLobby = false;
	LobbyState = ESPOnlineLobbyState::Hub;
	LastConnectString = ConnectString;
	PendingInviteResult.Reset();
	OnConnectStringReady.Broadcast(LastConnectString);
	FinishOperation(Operation, ESPOnlineSessionResult::Success);
}

void USPOnlineSessionSubsystem::HandleUpdateSessionComplete(
	const FName CompletedSessionName,
	const bool bWasSuccessful)
{
	if (CompletedSessionName != SessionName
		|| ActiveOperation != ESPOnlineSessionOperation::Update)
	{
		return;
	}

	ClearUpdateDelegate();
	PendingSessionSettings.Reset();
	if (bWasSuccessful)
	{
		LobbyState = PendingLobbyState;
	}
	FinishOperation(
		ESPOnlineSessionOperation::Update,
		bWasSuccessful
			? ESPOnlineSessionResult::Success
			: ESPOnlineSessionResult::RequestRejected);
}

void USPOnlineSessionSubsystem::HandleDestroySessionComplete(
	const FName CompletedSessionName,
	const bool bWasSuccessful)
{
	const ESPOnlineSessionOperation Operation = ActiveOperation;
	if (CompletedSessionName != SessionName
		|| (Operation != ESPOnlineSessionOperation::Destroy
			&& Operation != ESPOnlineSessionOperation::AcceptInvite))
	{
		return;
	}

	ClearDestroyDelegate();
	if (Operation == ESPOnlineSessionOperation::AcceptInvite)
	{
		if (!bWasSuccessful)
		{
			PendingInviteResult.Reset();
			FinishOperation(
				ESPOnlineSessionOperation::AcceptInvite,
				ESPOnlineSessionResult::RequestRejected);
			return;
		}

		bOwnsLobby = false;
		LobbyState = ESPOnlineLobbyState::Hub;
		LastConnectString.Reset();
		LobbyResults.Reset();
		LastSearch.Reset();
		BeginInviteJoin();
		return;
	}

	if (bWasSuccessful)
	{
		bOwnsLobby = false;
		LobbyState = ESPOnlineLobbyState::Hub;
		LastConnectString.Reset();
		LobbyResults.Reset();
		LastSearch.Reset();
	}
	FinishOperation(
		Operation,
		bWasSuccessful
			? ESPOnlineSessionResult::Success
			: ESPOnlineSessionResult::RequestRejected);
}

void USPOnlineSessionSubsystem::RebuildLobbySummaries()
{
	LobbyResults.Reset();
	if (!LastSearch.IsValid())
	{
		return;
	}

	LobbyResults.Reserve(LastSearch->SearchResults.Num());
	for (int32 Index = 0; Index < LastSearch->SearchResults.Num(); ++Index)
	{
		const FOnlineSessionSearchResult& SearchResult =
			LastSearch->SearchResults[Index];
		LobbyResults.Add(BuildLobbySummary(SearchResult, Index));
	}
}

bool USPOnlineSessionSubsystem::TryResolveConnectString(
	FString& OutConnectString) const
{
	OutConnectString.Reset();
	return SessionInterface.IsValid()
		&& SessionInterface->GetResolvedConnectString(
			SessionName,
			OutConnectString)
		&& !OutConnectString.IsEmpty();
}
