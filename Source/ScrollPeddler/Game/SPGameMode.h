#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
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

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void StartPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	/** Routes an authority request through the extraction zone's distance validation. */
	bool TryExtractCharacter(ASPCharacter* Character);

	/** Called only by ASPExtractionZone after its authority and distance checks pass. */
	void HandlePlayerReachedExtraction(ASPCharacter* Character);

	/** Completes settlement only after each owning client verifies its local SaveGame write. */
	void HandleSettlementAck(
		ASPPlayerController* PlayerController,
		const FGuid& AckSessionId,
		const FString& ResultHash,
		bool bSaved);

	ASPExtractionZone* GetExtractionZone() const { return ExtractionZone; }
	int32 GetExpectedPlayers() const { return ExpectedPlayers; }

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
};
