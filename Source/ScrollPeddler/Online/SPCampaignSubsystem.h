#pragma once

#include "CoreMinimal.h"
#include "Persistence/SPCampaignSaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SPCampaignSubsystem.generated.h"

class USPPlayerProfileSaveGame;

UENUM(BlueprintType)
enum class ESPCampaignPersistenceResult : uint8
{
	Success,
	ApplyRejected,
	NoCampaignLoaded,
	NoProfileLoaded,
	InvalidSlot,
	InvalidOwner,
	SlotEmpty,
	SlotOccupied,
	CorruptData,
	VersionMismatch,
	OwnerMismatch,
	SerializeFailed,
	WriteFailed,
	VerificationFailed,
	RollbackFailed
};

/**
 * Owns the host campaign and the local, non-economic player profile.
 *
 * Campaign saves use three fixed slots which are deliberately isolated from
 * the legacy ScrollPeddler_LocalProfile slot.
 */
UCLASS()
class SCROLLPEDDLER_API USPCampaignSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 CampaignSlotCount = 3;
	static constexpr int32 SaveUserIndex = 0;

	static bool IsValidCampaignSlot(int32 SlotIndex);
	static FString GetCampaignSlotName(int32 SlotIndex);
	static FString GetPlayerProfileSlotName();

	ESPCampaignPersistenceResult CreateCampaignSlot(
		int32 SlotIndex,
		const FString& OwnerId);
	ESPCampaignPersistenceResult LoadCampaignSlot(
		int32 SlotIndex,
		const FString& ExpectedOwnerId);
	ESPCampaignPersistenceResult SaveCurrentCampaign();

	/**
	 * Applies and persists a revision-checked transaction. Any persistence
	 * failure restores the exact pre-transaction in-memory snapshot.
	 */
	ESPCampaignPersistenceResult CommitCampaignTransaction(
		const FSPCampaignTransaction& Transaction,
		ESPCampaignApplyResult& OutApplyResult);

	ESPCampaignPersistenceResult CreatePlayerProfile(const FString& PlayerId);
	ESPCampaignPersistenceResult LoadPlayerProfile(const FString& ExpectedPlayerId);
	ESPCampaignPersistenceResult SaveCurrentPlayerProfile();

	const USPCampaignSaveGame* GetCurrentCampaign() const { return CurrentCampaign; }
	USPPlayerProfileSaveGame* GetMutablePlayerProfile() const { return CurrentPlayerProfile; }
	int32 GetCurrentCampaignSlotIndex() const { return CurrentCampaignSlotIndex; }

#if WITH_DEV_AUTOMATION_TESTS
	void AdoptCampaignForTesting(const USPCampaignSaveGame* Campaign, int32 SlotIndex);
	void FailNextCampaignPersistenceForTesting();
#endif

private:
	static FString GetCampaignFilePath(int32 SlotIndex);
	static FString GetPlayerProfileFilePath();

	ESPCampaignPersistenceResult PersistCampaign(
		USPCampaignSaveGame* Campaign,
		int32 SlotIndex,
		USPCampaignSaveGame*& OutVerifiedCampaign);
	ESPCampaignPersistenceResult PersistPlayerProfile(
		USPPlayerProfileSaveGame* Profile,
		USPPlayerProfileSaveGame*& OutVerifiedProfile);

	UPROPERTY(Transient)
	TObjectPtr<USPCampaignSaveGame> CurrentCampaign;

	UPROPERTY(Transient)
	TObjectPtr<USPPlayerProfileSaveGame> CurrentPlayerProfile;

	int32 CurrentCampaignSlotIndex = INDEX_NONE;

#if WITH_DEV_AUTOMATION_TESTS
	bool bFailNextCampaignPersistence = false;
#endif
};
