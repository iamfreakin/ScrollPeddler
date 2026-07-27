#pragma once

#include "CoreMinimal.h"
#include "Core/SPItemTypes.h"
#include "GameFramework/SaveGame.h"
#include "SPCampaignSaveGame.generated.h"

UENUM(BlueprintType)
enum class ESPCampaignApplyResult : uint8
{
	Applied,
	AlreadyProcessed,
	InvalidCampaign,
	InvalidTransaction,
	RevisionMismatch,
	InsufficientGold,
	ArithmeticOverflow,
	StashCapacityExceeded,
	ItemConflict
};

/**
 * A single authoritative mutation of the host-owned campaign.
 *
 * TransactionId makes retries idempotent. RunId is optional, but when supplied
 * it prevents the same expedition settlement from being committed through a
 * different transaction id.
 */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPCampaignTransaction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FGuid TransactionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FGuid RunId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int64 ExpectedRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 GoldDelta = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 GuildXPDelta = 0;

	/** Zero leaves the rank unchanged; valid overrides are 1 through 5. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 GuildRankOverride = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TArray<FGuid> StashItemIdsToRemove;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TArray<FSPItemInstance> StashItemsToAdd;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TArray<FGuid> SettlementItemIdsToRemove;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TArray<FSPItemInstance> SettlementItemsToAdd;
};

UCLASS()
class SCROLLPEDDLER_API USPCampaignSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentSaveVersion = 1;
	static constexpr int32 StashCapacity = 40;

	bool InitializeNewCampaign(const FString& InOwnerId);
	bool IsVersionCompatible() const;
	bool IsStructurallyValid() const;

	/**
	 * Applies a transaction only after validating the complete resulting state.
	 * Rejected operations leave this object untouched.
	 */
	ESPCampaignApplyResult ApplyTransaction(const FSPCampaignTransaction& Transaction);

	bool HasProcessedTransaction(const FGuid& TransactionId) const;
	bool HasProcessedRun(const FGuid& RunId) const;

	int32 GetSaveVersion() const { return SaveVersion; }
	const FGuid& GetCampaignId() const { return CampaignId; }
	const FString& GetOwnerId() const { return OwnerId; }
	int64 GetRevision() const { return Revision; }
	int32 GetGold() const { return Gold; }
	int32 GetGuildXP() const { return GuildXP; }
	int32 GetGuildRank() const { return GuildRank; }
	const TArray<FSPItemInstance>& GetStash() const { return Stash; }
	const TArray<FSPItemInstance>& GetSettlementInbox() const { return SettlementInbox; }
	const TSet<FGuid>& GetProcessedTransactionIds() const { return ProcessedTransactionIds; }
	const TSet<FGuid>& GetProcessedRunIds() const { return ProcessedRunIds; }

private:
	UPROPERTY(SaveGame)
	int32 SaveVersion = CurrentSaveVersion;

	UPROPERTY(SaveGame)
	FGuid CampaignId;

	UPROPERTY(SaveGame)
	FString OwnerId;

	UPROPERTY(SaveGame)
	int64 Revision = 0;

	UPROPERTY(SaveGame)
	int32 Gold = 0;

	UPROPERTY(SaveGame)
	int32 GuildXP = 0;

	UPROPERTY(SaveGame)
	int32 GuildRank = 1;

	UPROPERTY(SaveGame)
	TArray<FSPItemInstance> Stash;

	UPROPERTY(SaveGame)
	TArray<FSPItemInstance> SettlementInbox;

	UPROPERTY(SaveGame)
	TSet<FGuid> ProcessedTransactionIds;

	UPROPERTY(SaveGame)
	TSet<FGuid> ProcessedRunIds;
};
