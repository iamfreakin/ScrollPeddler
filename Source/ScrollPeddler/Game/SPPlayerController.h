#pragma once

#include "CoreMinimal.h"
#include "Core/SPTypes.h"
#include "GameFramework/PlayerController.h"
#include "SPPlayerController.generated.h"

class ASPCharacter;
class ASPScrollPickup;
#if !UE_BUILD_SHIPPING
class FSPAdversarialTestCoordinator;
#endif

UCLASS()
class SCROLLPEDDLER_API ASPPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** 온라인 서비스 없이 기술 스파이크 맵을 raw listen server로 연다. */
	UFUNCTION(Exec)
	void SPHost(int32 InExpectedPlayers = 2);

	/** IP 주소 또는 Unreal travel URL로 직접 이동한다. */
	UFUNCTION(Exec)
	void SPJoin(const FString& Address);

	UFUNCTION(Client, Reliable)
	void ClientCommitSessionResult(const FSPSessionResult& Result);

	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeSessionResult(FGuid SessionId, const FString& ResultHash, bool bSaved);

protected:
	virtual void BeginPlay() override;

private:
#if !UE_BUILD_SHIPPING
	friend class FSPAdversarialTestCoordinator;
#endif

	enum class EAutoSpikeStep : uint8
	{
		WaitingForPawn,
		WaitingForPickup,
		WaitingForInventory,
		WaitingForConsumption,
		WaitingForSettlement,
		Finished
	};

	void StartAutoSpikeIfRequested();
	void RunAutoSpikeStep();
	void ScheduleAutoQuitIfRequested();
	void RequestAutoQuit();

#if !UE_BUILD_SHIPPING
	/**
	 * 권위 coordinator와 owning client, Character의 exact-request RPC seam을
	 * 연결하는 Development 전용 브리지다.
	 */
	void StartAdversarialSuiteIfRequested();
	void PollAdversarialLocalState();
	void ResetAdversarialClientAction();
	void ReportAdversarialActionResult(FName ResultCode, bool bDispatched);
	FString BuildAdversarialClientStateToken() const;
#endif

	/** Development 빌드에서 coordinator와 owning client 사이의 barrier/action 메시지를 전달한다. */
	UFUNCTION(Server, Reliable)
	void ServerRegisterAdversarialClient(
		int32 ClientIndex,
		const FString& RunId,
		int32 Seed,
		int32 PartySize);

	UFUNCTION(Client, Reliable)
	void ClientArmAdversarialAction(
		FName ScenarioId,
		FName Action,
		ASPCharacter* ActionPawn,
		ASPScrollPickup* TargetPickup,
		FGuid TargetInstanceId,
		uint32 RequestId,
		bool bAllowMissingTarget);

	UFUNCTION(Server, Reliable)
	void ServerReportAdversarialActionArmed(FName ScenarioId, int32 ClientIndex);

	UFUNCTION(Client, Reliable)
	void ClientReleaseAdversarialAction(FName ScenarioId);

	UFUNCTION(Server, Reliable)
	void ServerReportAdversarialActionResult(
		FName ScenarioId,
		int32 ClientIndex,
		uint32 RequestId,
		FName ResultCode,
		bool bDispatched,
		const FString& ClientState);

	UFUNCTION(Client, Reliable)
	void ClientFinishAdversarialSuite(bool bPassed);

	FTimerHandle AutoSpikeTimerHandle;
	FTimerHandle AutoQuitTimerHandle;
	EAutoSpikeStep AutoSpikeStep = EAutoSpikeStep::WaitingForPawn;
	double AutoStepStartedAtSeconds = 0.0;
	double AutoContestedBarrierStartedAtSeconds = 0.0;
	bool bAutoContestedAttemptMade = false;

#if !UE_BUILD_SHIPPING
	/** 클라이언트 로컬 barrier/action 상태이며 최종 판정은 권위 coordinator가 소유한다. */
	FTimerHandle AdversarialClientTimerHandle;
	bool bAdversarialClientRegistered = false;
	bool bAdversarialActionArmed = false;
	bool bAdversarialActionReleased = false;
	bool bAdversarialActionDispatched = false;
	bool bAdversarialAllowMissingTarget = false;
	int32 AdversarialClientIndex = INDEX_NONE;
	int32 AdversarialPartySize = 0;
	int32 AdversarialSeed = 0;
	double AdversarialActionDeadlineSeconds = 0.0;
	FString AdversarialRunId;
	FName AdversarialScenarioId = NAME_None;
	FName AdversarialAction = NAME_None;
	FGuid AdversarialTargetInstanceId;
	uint32 AdversarialRequestId = 0;
	uint32 AdversarialPickupCompletionSerialBaseline = 0;
	uint32 AdversarialUseCompletionSerialBaseline = 0;
	TWeakObjectPtr<ASPCharacter> AdversarialActionPawn;
	TWeakObjectPtr<ASPScrollPickup> AdversarialTargetPickup;
#endif
};
