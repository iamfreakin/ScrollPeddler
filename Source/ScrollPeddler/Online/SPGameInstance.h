#pragma once

#include "CoreMinimal.h"
#include "Core/SPTypes.h"
#include "Engine/GameInstance.h"
#include "SPGameInstance.generated.h"

class USPSaveGame;

UCLASS()
class SCROLLPEDDLER_API USPGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	static const FString SaveSlotName;
	static constexpr int32 SaveUserIndex = 0;
	static FString GetSaveSlotName();

	/**
	 * Writes a result to the local profile and reloads it to verify persistence.
	 * Exact replays succeed idempotently; conflicting SessionIds are rejected.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scroll Peddler|Persistence")
	bool CommitSessionResult(const FSPSessionResult& Result);

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Persistence")
	const USPSaveGame* GetLastVerifiedSave() const { return LastVerifiedSave; }

private:
	USPSaveGame* LoadOrCreateLocalSave() const;

	UPROPERTY(Transient)
	TObjectPtr<USPSaveGame> LastVerifiedSave;
};
