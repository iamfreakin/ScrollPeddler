#pragma once

#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Core/SPTypes.h"
#include "TimerManager.h"

class AActor;
class ASPCharacter;
class ASPGameMode;
class ASPPlayerController;
class ASPPlayerState;
class ASPScrollPickup;

/**
 * 패키지 적대적 멀티플레이 suite를 조율하는 권위 전용 coordinator다.
 *
 * gameplay actor 밖에서 동작하며 Shipping에서는 컴파일하지 않는다.
 * owning client는 실제 Character RPC 경로를 실행하고, 이 클래스는
 * 결정적 fixture 생성·barrier 동기화·권위 상태 전이 검증만 담당한다.
 */
class FSPAdversarialTestCoordinator
{
public:
	explicit FSPAdversarialTestCoordinator(ASPGameMode& InGameMode);
	~FSPAdversarialTestCoordinator();

	/** roster polling을 시작하고 command line의 run metadata를 기준으로 suite를 연다. */
	void Start();
	/** owning client를 ClientIndex에 등록하며 RunId·Seed·PartySize 일치를 검증한다. */
	void RegisterClient(
		ASPPlayerController* PlayerController,
		int32 ClientIndex,
		const FString& ClientRunId,
		int32 ClientSeed,
		int32 ClientPartySize);
	/** client가 현재 시나리오 action을 실행할 준비가 됐음을 barrier에 기록한다. */
	void ReportActionArmed(
		ASPPlayerController* PlayerController,
		FName ScenarioId,
		int32 ClientIndex);
	/** client 관측 결과와 서버 snapshot을 연결해 현재 시나리오 판정 자료로 기록한다. */
	void ReportActionResult(
		ASPPlayerController* PlayerController,
		FName ScenarioId,
		int32 ClientIndex,
		uint32 RequestId,
		FName ResultCode,
		bool bDispatched,
		const FString& ClientState);
	void HandleLogout(ASPPlayerController* PlayerController);

private:
	enum class EScenario : uint8
	{
		OutOfRange,
		Obstructed,
		InventoryFull,
		OwnershipSpoof,
		ConcurrentClaim,
		PickupReplay,
		PickupRequestIdConflict,
		UseNotOwned,
		UseReplay,
		PositiveSettlement,
		InactivePlayer
	};

	enum class ERunState : uint8
	{
		WaitingForRoster,
		PreparingScenario,
		WaitingForArmed,
		WaitingForResults,
		WaitingForAbsenceWindow,
		WaitingForSettlement,
		Finished
	};

	struct FActionSpec
	{
		int32 ClientIndex = INDEX_NONE;
		FName Action = NAME_None;
		TWeakObjectPtr<ASPCharacter> ActionPawn;
		TWeakObjectPtr<ASPScrollPickup> TargetPickup;
		FGuid TargetInstanceId;
		uint32 RequestId = 0;
		bool bAllowMissingTarget = false;
	};

	struct FClientReport
	{
		uint32 RequestId = 0;
		FName ResultCode = NAME_None;
		bool bDispatched = false;
		FString ClientState;
	};

	struct FAuthoritySnapshot
	{
		int32 InventoryCount = 0;
		int32 PickedUpCount = 0;
		int32 ConsumedCount = 0;
		int32 ExtractedCount = 0;
		int32 ExtractedScrollCount = 0;
		int32 GoldDelta = 0;
		float SilenceEndServerTimeTotal = 0.0f;

		FString ToToken() const;
	};

	void Tick();
	bool IsRosterReady() const;
	void BeginNextScenario();
	void BeginScenario(EScenario Scenario);
	void OnActionsCompleted();
	void EvaluateCurrentScenario();
	void FinishCurrentScenario(bool bPassed, FName ResultCode, const FString& Detail, bool bSkipped = false);
	void FinishSuite();
	void FailRemainingScenarios(const FString& Detail);

	void ArmActions(const TArray<FActionSpec>& Actions);
	void ReleaseArmedActions();
	bool HaveAllActionsArmed() const;
	bool HaveAllActionResults() const;
	int32 CountReportedResult(FName ResultCode) const;
	const FClientReport* FindReport(int32 ClientIndex) const;

	void PrepareOutOfRange();
	void PrepareObstructed();
	void PrepareInventoryFull();
	void PrepareOwnershipSpoof();
	void PrepareConcurrentClaim();
	void PreparePickupReplay();
	void PreparePickupRequestIdConflict();
	void PrepareUseNotOwned();
	void PrepareUseReplay();
	void PreparePositiveSettlement();
	void PrepareInactivePlayer();
	void BeginPositiveExtraction();

	void PlaceParticipants();
	ASPCharacter* GetCharacter(int32 ClientIndex) const;
	ASPPlayerState* GetPlayerState(int32 ClientIndex) const;
	ASPScrollPickup* SpawnPickup(const FVector& Location);
	AActor* SpawnLineOfSightBlocker(const FVector& Location);
	FSPScrollInstance MakeFixtureScroll();
	void CleanupScenarioFixtures();
	void FillInventoryForCapacityCase(int32 ClientIndex);
	void RemoveTemporaryInventoryItems();
	FAuthoritySnapshot CaptureAuthoritySnapshot() const;
	bool IsPrimaryPickupAvailable() const;
	bool AllPlayersHaveInventory() const;
	bool AllPlayersExtractedWithPositiveResult() const;
	int32 CountInventoryOwners(const FGuid& InstanceId, int32& OutOwnerClientIndex) const;

	void LogScenarioBegin(EScenario Scenario);
	void LogActionResult(const FActionSpec& Action, const FClientReport& Report) const;
	void LogRosterReady();
	static const TCHAR* GetScenarioName(EScenario Scenario);
	static FString SanitizeToken(const FString& Value);
	FString GetSessionIdToken() const;
	uint32 AllocateRequestId();
	double GetWorldTimeSeconds() const;

	TWeakObjectPtr<ASPGameMode> GameMode;
	TMap<int32, TWeakObjectPtr<ASPPlayerController>> Clients;
	TArray<EScenario> Scenarios;
	TArray<FActionSpec> CurrentActions;
	TSet<int32> ArmedClients;
	TMap<int32, FClientReport> ClientReports;
	TArray<TWeakObjectPtr<AActor>> ScenarioFixtures;
	TMap<int32, TArray<FGuid>> TemporaryInventoryItems;

	TWeakObjectPtr<ASPScrollPickup> PrimaryPickup;
	TWeakObjectPtr<ASPScrollPickup> SecondaryPickup;
	FAuthoritySnapshot BeforeSnapshot;
	FAuthoritySnapshot MiddleSnapshot;

	FTimerHandle TickTimerHandle;
	ERunState RunState = ERunState::WaitingForRoster;
	int32 ScenarioIndex = INDEX_NONE;
	int32 CurrentStage = 0;
	int32 PassedCases = 0;
	int32 FailedCases = 0;
	int32 SkippedCases = 0;
	int32 FixtureSerial = 0;
	uint32 NextRequestId = 0xA0000000u;
	uint32 CurrentRequestId = 0;
	double StateDeadlineSeconds = 0.0;
	double CurrentCaseDeadlineSeconds = 0.0;
	double AbsenceWindowDeadlineSeconds = 0.0;
	bool bStarted = false;
	bool bRosterLogged = false;
	bool bCurrentCaseLogged = false;

	FString RunId;
	FString BuildVersion;
	FString CommitSha;
	int32 Seed = 0;
	int32 PartySize = 1;
	int32 RttMs = 0;
	int32 LossPct = 0;
};

#endif
