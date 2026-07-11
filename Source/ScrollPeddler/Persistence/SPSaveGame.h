#pragma once

#include "CoreMinimal.h"
#include "Core/SPTypes.h"
#include "GameFramework/SaveGame.h"
#include "SPSaveGame.generated.h"

UCLASS()
class SCROLLPEDDLER_API USPSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/**
	 * Applies a valid result once. An exact replay is an idempotent success;
	 * invalid results and SessionId conflicts are rejected.
	 */
	bool ApplyResultIfNew(const FSPSessionResult& Result);

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Persistence")
	int32 GetGold() const { return Gold; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Persistence")
	int32 GetSessionsPlayed() const { return SessionsPlayed; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Persistence")
	bool HasCommittedSession(const FGuid& SessionId) const;

	const FSPSessionResult* FindCommittedResult(const FGuid& SessionId) const;

private:
	UPROPERTY(SaveGame)
	int32 SaveVersion = 1;

	UPROPERTY(SaveGame)
	int32 Gold = 0;

	UPROPERTY(SaveGame)
	int32 SessionsPlayed = 0;

	UPROPERTY(SaveGame)
	TArray<FSPSessionResult> CommittedResults;
};
