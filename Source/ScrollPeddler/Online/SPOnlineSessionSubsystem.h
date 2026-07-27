#pragma once

#include "CoreMinimal.h"
#include "Online/CoreOnlineFwd.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SPOnlineSessionSubsystem.generated.h"

class FOnlineSessionSearch;
class FOnlineSessionSearchResult;
class FOnlineSessionSettings;
class IOnlineSession;

UENUM(BlueprintType)
enum class ESPOnlineLobbyState : uint8
{
	Hub,
	Expedition
};

UENUM(BlueprintType)
enum class ESPOnlineSessionOperation : uint8
{
	None,
	Create,
	Find,
	Join,
	QuickPlay,
	Update,
	Destroy,
	AcceptInvite
};

/** Blueprint-safe result shared by synchronous rejection and async completion. */
UENUM(BlueprintType)
enum class ESPOnlineSessionResult : uint8
{
	Success,
	Busy,
	InvalidRequest,
	OnlineUnavailable,
	RequestRejected,
	NotFound,
	NoCompatibleLobby,
	BuildMismatch,
	RulesMismatch,
	RunInProgress,
	NotJoinable,
	SessionFull,
	ConnectStringUnavailable,
	UnknownError,
	InviteRejected,
	InvitesDisabled,
	AlreadyHosting
};

/** Blueprint-safe projection of a legacy FOnlineSessionSearchResult. */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPOnlineLobbySummary
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scroll Peddler|Online")
	int32 SearchResultIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scroll Peddler|Online")
	FString HostDisplayName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scroll Peddler|Online")
	int32 MaxPlayers = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scroll Peddler|Online")
	int32 OpenPublicConnections = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scroll Peddler|Online")
	int32 PingMs = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scroll Peddler|Online")
	int32 BuildUniqueId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scroll Peddler|Online")
	int32 RulesVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scroll Peddler|Online")
	ESPOnlineLobbyState LobbyState = ESPOnlineLobbyState::Hub;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scroll Peddler|Online")
	bool bJoinable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scroll Peddler|Online")
	bool bAllowsInvites = false;
};

/**
 * Pure Quick Play compatibility policy. This does not trust lobby metadata as
 * authority; JoinSession and the listen host still make the final decision.
 */
SCROLLPEDDLER_API ESPOnlineSessionResult SPEvaluateQuickPlayCompatibility(
	const FSPOnlineLobbySummary& Candidate,
	int32 RequiredBuildUniqueId,
	int32 RequiredRulesVersion);

/**
 * Pure friend-invite policy. Accepted invites still obey the same build,
 * rules, phase, capacity, and joinability gates as search joins, and also
 * require the host to be advertising invitations.
 */
SCROLLPEDDLER_API ESPOnlineSessionResult SPEvaluateInviteCompatibility(
	const FSPOnlineLobbySummary& Candidate,
	int32 RequiredBuildUniqueId,
	int32 RequiredRulesVersion);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FSPOnlineOperationCompleteSignature,
	ESPOnlineSessionOperation,
	Operation,
	ESPOnlineSessionResult,
	Result);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FSPOnlineSearchResultsSignature,
	const TArray<FSPOnlineLobbySummary>&,
	Results);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FSPOnlineConnectStringSignature,
	const FString&,
	ConnectString);

/**
 * Legacy Online Subsystem session coordinator for a public 1-4 player listen
 * lobby. It intentionally does not implement host migration, voice, Steam
 * sockets, or UE Online Services.
 */
UCLASS()
class SCROLLPEDDLER_API USPOnlineSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 MinPublicPlayers = 1;
	static constexpr int32 MaxPublicPlayers = 4;
	static constexpr int32 CurrentRulesVersion = 1;

	static const FName SessionName;
	static const FName BuildUniqueIdSettingKey;
	static const FName RulesVersionSettingKey;
	static const FName LobbyStateSettingKey;
	static const FName OpenSlotsSettingKey;
	static const FName JoinableSettingKey;
	static const FName InvitesSettingKey;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Scroll Peddler|Online")
	bool CreatePublicLobby(int32 MaxPlayers = 4, int32 LocalUserNum = 0);

	UFUNCTION(BlueprintCallable, Category = "Scroll Peddler|Online")
	bool FindPublicLobbies(int32 MaxResults = 50, int32 LocalUserNum = 0);

	UFUNCTION(BlueprintCallable, Category = "Scroll Peddler|Online")
	bool JoinLobbyByIndex(int32 SearchResultIndex, int32 LocalUserNum = 0);

	UFUNCTION(BlueprintCallable, Category = "Scroll Peddler|Online")
	bool QuickPlay(int32 MaxResults = 50, int32 LocalUserNum = 0);

	/**
	 * Host-only lobby update. Expedition always forces joinability and invites
	 * off, regardless of the supplied toggles.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scroll Peddler|Online")
	bool UpdateLobby(
		ESPOnlineLobbyState NewLobbyState,
		bool bJoinable,
		bool bAllowInvites,
		int32 OpenPublicConnections = -1);

	UFUNCTION(BlueprintCallable, Category = "Scroll Peddler|Online")
	bool DestroyLobby();

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Online")
	ESPOnlineSessionOperation GetActiveOperation() const { return ActiveOperation; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Online")
	ESPOnlineSessionResult GetLastResult() const { return LastResult; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Online")
	ESPOnlineLobbyState GetLobbyState() const { return LobbyState; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Online")
	bool IsLobbyHost() const { return bOwnsLobby; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Online")
	TArray<FSPOnlineLobbySummary> GetLastSearchResults() const { return LobbyResults; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Online")
	FString GetLastConnectString() const { return LastConnectString; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Online")
	static int32 GetRequiredRulesVersion() { return CurrentRulesVersion; }

	static int32 GetRequiredBuildUniqueId();

	UPROPERTY(BlueprintAssignable, Category = "Scroll Peddler|Online")
	FSPOnlineOperationCompleteSignature OnOperationComplete;

	UPROPERTY(BlueprintAssignable, Category = "Scroll Peddler|Online")
	FSPOnlineSearchResultsSignature OnSearchResultsUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Scroll Peddler|Online")
	FSPOnlineConnectStringSignature OnConnectStringReady;

private:
	bool BeginOperation(ESPOnlineSessionOperation RequestedOperation);
	void FinishOperation(
		ESPOnlineSessionOperation CompletedOperation,
		ESPOnlineSessionResult Result);
	bool BeginFind(int32 MaxResults, int32 LocalUserNum, bool bQuickPlay);
	bool BeginJoin(int32 SearchResultIndex, int32 LocalUserNum);
	bool BeginInviteJoin();

	TSharedPtr<IOnlineSession, ESPMode::ThreadSafe> ResolveSessionInterface();
	void RegisterInviteAcceptedDelegate();
	void ClearDelegateHandles();
	void ClearCreateDelegate();
	void ClearFindDelegate();
	void ClearJoinDelegate();
	void ClearUpdateDelegate();
	void ClearDestroyDelegate();
	void ClearInviteAcceptedDelegate();

	void HandleCreateSessionComplete(FName CompletedSessionName, bool bWasSuccessful);
	void HandleFindSessionsComplete(bool bWasSuccessful);
	void HandleJoinSessionComplete(FName CompletedSessionName, uint32 JoinResult);
	void HandleUpdateSessionComplete(FName CompletedSessionName, bool bWasSuccessful);
	void HandleDestroySessionComplete(FName CompletedSessionName, bool bWasSuccessful);
	void HandleSessionUserInviteAccepted(
		bool bWasSuccessful,
		int32 ControllerId,
		FUniqueNetIdPtr UserId,
		const FOnlineSessionSearchResult& InviteResult);

	void RebuildLobbySummaries();
	bool TryResolveConnectString(FString& OutConnectString) const;

	TSharedPtr<IOnlineSession, ESPMode::ThreadSafe> SessionInterface;
	TSharedPtr<FOnlineSessionSearch> LastSearch;
	TSharedPtr<FOnlineSessionSearchResult> PendingInviteResult;
	TSharedPtr<FOnlineSessionSettings> PendingSessionSettings;

	FDelegateHandle CreateDelegateHandle;
	FDelegateHandle FindDelegateHandle;
	FDelegateHandle JoinDelegateHandle;
	FDelegateHandle UpdateDelegateHandle;
	FDelegateHandle DestroyDelegateHandle;
	FDelegateHandle InviteAcceptedDelegateHandle;

	UPROPERTY(Transient)
	TArray<FSPOnlineLobbySummary> LobbyResults;

	UPROPERTY(Transient)
	FString LastConnectString;

	ESPOnlineSessionOperation ActiveOperation = ESPOnlineSessionOperation::None;
	ESPOnlineSessionResult LastResult = ESPOnlineSessionResult::Success;
	ESPOnlineLobbyState LobbyState = ESPOnlineLobbyState::Hub;

	int32 PendingLocalUserNum = 0;
	ESPOnlineLobbyState PendingLobbyState = ESPOnlineLobbyState::Hub;
	bool bOwnsLobby = false;
	bool bDeinitializing = false;
};
