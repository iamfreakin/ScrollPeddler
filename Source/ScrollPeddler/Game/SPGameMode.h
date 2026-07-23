#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#if !UE_BUILD_SHIPPING
#include "Tests/SPAdversarialTestCoordinator.h"
#endif
#include "SPGameMode.generated.h"

class ASPCharacter;
class ASPExtractionZone;
class ASPGameState;
class ASPPlayerController;
class ASPPlayerState;
class USPScrollDefinition;
class USPScrollEngravingDefinition;

UCLASS()
class SCROLLPEDDLER_API ASPGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASPGameMode();
	virtual ~ASPGameMode() override;

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void StartPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 권위 요청을 extraction zone의 거리 검증 경로로 전달한다. */
	bool TryExtractCharacter(ASPCharacter* Character);

	/** ASPExtractionZone의 권위·거리 검증이 통과했을 때만 호출한다. */
	void HandlePlayerReachedExtraction(ASPCharacter* Character);

	/** 각 owning client가 로컬 SaveGame 기록을 확인한 뒤에만 정산을 완료한다. */
	void HandleSettlementAck(
		ASPPlayerController* PlayerController,
		const FGuid& AckSessionId,
		const FString& ResultHash,
		bool bSaved);

	ASPExtractionZone* GetExtractionZone() const { return ExtractionZone; }
	int32 GetExpectedPlayers() const { return ExpectedPlayers; }

#if !UE_BUILD_SHIPPING
	/** 검증된 클라이언트 등록을 권위 전용 패키지 테스트 coordinator로 전달한다. */
	void RegisterAdversarialClient(
		ASPPlayerController* PlayerController,
		int32 ClientIndex,
		const FString& RunId,
		int32 Seed,
		int32 PartySize);
	/** owning client의 준비 상태를 coordinator의 시나리오 barrier에 기록한다. */
	void ReportAdversarialActionArmed(
		ASPPlayerController* PlayerController,
		FName ScenarioId,
		int32 ClientIndex);
	/** 클라이언트가 관측한 Character RPC 결과를 권위 coordinator의 판정 경로로 보낸다. */
	void ReportAdversarialActionResult(
		ASPPlayerController* PlayerController,
		FName ScenarioId,
		int32 ClientIndex,
		uint32 RequestId,
		FName ResultCode,
		bool bDispatched,
		const FString& ClientState);
#endif

private:
	void SpawnSpikeWorld();
	void SpawnPlayerStarts();
	void SpawnGrayboxLighting();
	void SpawnGrayboxBlocks();
	void SpawnSpikePickups();
	void SpawnExtractionZone();
	void RefreshSessionPhase();
	void TryCommitSettlement();
	FString BuildLocalPlayerId(const ASPPlayerState* PlayerState) const;

	FGuid SessionId;
	int32 ExpectedPlayers = 2;
	bool bSettlementStarted = false;
	bool bSpikeWorldSpawned = false;
	TMap<TWeakObjectPtr<ASPPlayerController>, FString> PendingSettlementHashes;
	TSet<TWeakObjectPtr<ASPPlayerController>> SuccessfulSettlementAcks;

	UPROPERTY(Transient)
	TObjectPtr<ASPExtractionZone> ExtractionZone;

	UPROPERTY()
	TObjectPtr<USPScrollDefinition> SpikeScrollDefinition;

	UPROPERTY()
	TObjectPtr<USPScrollEngravingDefinition> AmplifiedEngravingDefinition;

	UPROPERTY()
	TObjectPtr<USPScrollEngravingDefinition> StableEngravingDefinition;

#if !UE_BUILD_SHIPPING
	TUniquePtr<FSPAdversarialTestCoordinator> AdversarialTestCoordinator;
#endif
};
