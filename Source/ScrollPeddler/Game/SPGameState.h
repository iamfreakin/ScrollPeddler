#pragma once

#include "CoreMinimal.h"
#include "Core/SPTypes.h"
#include "GameFramework/GameStateBase.h"
#include "SPGameState.generated.h"

UCLASS()
class SCROLLPEDDLER_API ASPGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ASPGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void AuthorityInitializeSession(const FGuid& InSessionId, int32 InExpectedPlayers);
	void AuthoritySetPhase(ESPSessionPhase InPhase);
	void AuthoritySetExtractedPlayerCount(int32 InExtractedPlayerCount);
	void AuthorityMarkSettlementCommitted();

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	FGuid GetSessionId() const { return SessionId; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	ESPSessionPhase GetSessionPhase() const { return SessionPhase; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	int32 GetExpectedPlayers() const { return ExpectedPlayers; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	int32 GetExtractedPlayerCount() const { return ExtractedPlayerCount; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	bool IsSettlementCommitted() const { return bSettlementCommitted; }

private:
	UFUNCTION()
	void OnRep_SessionMetadata();

	UFUNCTION()
	void OnRep_SessionPhase();

	UFUNCTION()
	void OnRep_ExtractionProgress();

	UPROPERTY(ReplicatedUsing = OnRep_SessionMetadata)
	FGuid SessionId;

	UPROPERTY(ReplicatedUsing = OnRep_SessionMetadata)
	int32 ExpectedPlayers = 1;

	UPROPERTY(ReplicatedUsing = OnRep_SessionPhase)
	ESPSessionPhase SessionPhase = ESPSessionPhase::LobbyCreated;

	UPROPERTY(ReplicatedUsing = OnRep_ExtractionProgress)
	int32 ExtractedPlayerCount = 0;

	UPROPERTY(ReplicatedUsing = OnRep_SessionPhase)
	bool bSettlementCommitted = false;
};
