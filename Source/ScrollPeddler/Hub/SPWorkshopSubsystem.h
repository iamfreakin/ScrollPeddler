#pragma once

#include "CoreMinimal.h"
#include "Data/SPVerticalSliceDefinitions.h"
#include "Online/SPCampaignSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SPWorkshopSubsystem.generated.h"

UENUM(BlueprintType)
enum class ESPWorkshopOperationResult : uint8
{
	Success,
	AlreadyProcessed,
	NotAuthority,
	NoCampaignLoaded,
	InvalidRequest,
	InvalidDefinition,
	TransactionConflict,
	RevisionMismatch,
	ActiveContractExists,
	NoActiveContract,
	ContractMismatch,
	DangerLocked,
	VoteRequired,
	InsufficientGold,
	MissingIngredients,
	InvalidOutput,
	CampaignRejected,
	PersistenceFailed,
	ArithmeticOverflow
};

UENUM(BlueprintType)
enum class ESPWorkshopIngredientSource : uint8
{
	StashOnly,
	SettlementInboxOnly,
	SettlementInboxThenStash
};

UENUM(BlueprintType)
enum class ESPWorkshopOutputDestination : uint8
{
	PreferStash,
	SettlementInbox
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPContractSelectionRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGuid TransactionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 ExpectedWorkshopRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 ExpectedCampaignRevision = 0;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPContractClearRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGuid TransactionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 ExpectedWorkshopRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 ExpectedCampaignRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FPrimaryAssetId ExpectedActiveContractId;
};

/**
 * Session-scoped server state for the contract selected for the next run.
 * The authoritative GameState may mirror this state for client presentation.
 */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPWorkshopContractState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 Revision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FPrimaryAssetId ActiveContractId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName ActiveContractStableId;

	bool HasActiveContract() const { return ActiveContractId.IsValid(); }

	ESPWorkshopOperationResult TrySelectContract(
		const USPContractDefinition& Contract,
		const FSPContractSelectionRequest& Request,
		int64 CurrentCampaignRevision,
		int32 CurrentGuildRank);

	ESPWorkshopOperationResult TryClearContract(
		const FSPContractClearRequest& Request,
		int64 CurrentCampaignRevision);

private:
	UPROPERTY()
	TMap<FGuid, FString> ProcessedRequestFingerprints;
};

/**
 * A quote resolved exclusively by server-side crafting/minigame code.
 * Client RPCs must never be allowed to author GoldCost or output instances.
 */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPWorkshopCraftQuote
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGuid TransactionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 ExpectedCampaignRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 GoldCost = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPWorkshopIngredientSource IngredientSource =
		ESPWorkshopIngredientSource::StashOnly;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPWorkshopOutputDestination RequestedDestination =
		ESPWorkshopOutputDestination::PreferStash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bCampaignVoteApproved = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FSPItemInstance> ServerGeneratedOutputs;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPWorkshopCraftPlan
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FSPCampaignTransaction CampaignTransaction;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPWorkshopOutputDestination ActualDestination =
		ESPWorkshopOutputDestination::SettlementInbox;
};

/**
 * Server-authoritative hub contract and formal-crafting boundary.
 *
 * Contract selection has an independent session revision because selecting a
 * run does not mutate persistent campaign economy. Crafting is committed using
 * the persistent campaign's revision and idempotency ledger.
 */
UCLASS()
class SCROLLPEDDLER_API USPWorkshopSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static bool IsContractDefinitionUsable(const USPContractDefinition* Contract);
	static bool IsRecipeDefinitionUsable(const USPCraftingRecipeDefinition* Recipe);

	static ESPWorkshopOperationResult BuildCraftPlan(
		const USPCampaignSaveGame& Campaign,
		const USPCraftingRecipeDefinition& Recipe,
		const FSPWorkshopCraftQuote& Quote,
		FSPWorkshopCraftPlan& OutPlan);

	ESPWorkshopOperationResult SelectContract(
		const USPContractDefinition* Contract,
		const FSPContractSelectionRequest& Request);

	ESPWorkshopOperationResult ClearActiveContract(
		const FSPContractClearRequest& Request);

	ESPWorkshopOperationResult CraftRecipe(
		const USPCraftingRecipeDefinition* Recipe,
		const FSPWorkshopCraftQuote& Quote,
		ESPCampaignApplyResult& OutApplyResult,
		ESPCampaignPersistenceResult& OutPersistenceResult,
		ESPWorkshopOutputDestination& OutActualDestination);

	const FSPWorkshopContractState& GetContractState() const { return ContractState; }

#if WITH_DEV_AUTOMATION_TESTS
	void ConfigureAuthorityForTesting(
		USPCampaignSubsystem* InCampaignSubsystem,
		bool bInHasAuthority);
#endif

private:
	bool HasServerAuthority() const;
	USPCampaignSubsystem* ResolveCampaignSubsystem() const;

	UPROPERTY(Transient)
	FSPWorkshopContractState ContractState;

#if WITH_DEV_AUTOMATION_TESTS
	TWeakObjectPtr<USPCampaignSubsystem> CampaignSubsystemForTesting;

	TOptional<bool> AuthorityForTesting;
#endif
};
