#pragma once

#include "CoreMinimal.h"
#include "Core/SPItemTypes.h"
#include "Data/SPVerticalSliceDefinitions.h"
#include "SPContractEvaluator.generated.h"

/**
 * Server-resolved item evidence. Stable IDs are resolved from authoritative
 * definitions before evaluation, so the evaluator remains pure and asset-load
 * independent.
 */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPContractItemEvidence
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FSPItemInstance Item;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName DefinitionStableId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName EngravingStableId;

	bool IsValid() const;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPContractPlayerExtraction
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FString PlayerId;

	/** False covers Missing and every other non-extracted outcome. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bExtracted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FSPContractItemEvidence> Items;
};

/**
 * Server-authored proof that one post-run production output was committed.
 * ReceiptId and SubmittedItemInstanceId are separate idempotency identities.
 */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPContractProductionReceipt
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGuid ReceiptId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGuid RunId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGuid SubmittedItemInstanceId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName SubmittedScrollStableId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	double CompletedAtSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bCompleted = false;

	bool IsValid() const;
};

/** Complete authoritative evidence presented at contract settlement. */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPContractRunEvidence
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGuid RunId;

	/**
	 * Monotonic server time at which the post-run production window opened.
	 * Ignored by non-production contracts.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	double ProductionWindowOpenedAtSeconds = -1.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FSPContractPlayerExtraction> Players;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FSPContractProductionReceipt> ProductionReceipts;
};

UENUM(BlueprintType)
enum class ESPContractEvaluationOutcome : uint8
{
	InvalidDefinition,
	InvalidEvidence,
	Failed,
	Succeeded
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPContractEvaluationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPContractEvaluationOutcome Outcome = ESPContractEvaluationOutcome::InvalidDefinition;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 MatchedQuantity = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FGuid> CountedItemInstanceIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FGuid> CountedReceiptIds;

	/** Always zero unless Outcome is Succeeded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 GoldReward = 0;

	/** Always zero unless Outcome is Succeeded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 GuildXpReward = 0;

	bool IsSuccess() const { return Outcome == ESPContractEvaluationOutcome::Succeeded; }
};

/** Stateless server contract evaluator shared by GameMode and Workshop flows. */
class SCROLLPEDDLER_API FSPContractEvaluator
{
public:
	static bool IsDefinitionUsable(const USPContractDefinition& Definition);

	static FSPContractEvaluationResult Evaluate(
		const USPContractDefinition& Definition,
		const FSPContractRunEvidence& Evidence);

private:
	static bool IsRunEvidenceShapeValid(const FSPContractRunEvidence& Evidence);
	static FSPContractEvaluationResult EvaluateScrollDelivery(
		const USPContractDefinition& Definition,
		const FSPContractRunEvidence& Evidence);
	static FSPContractEvaluationResult EvaluateLargeCargoRecovery(
		const USPContractDefinition& Definition,
		const FSPContractRunEvidence& Evidence);
	static FSPContractEvaluationResult EvaluatePostRunProduction(
		const USPContractDefinition& Definition,
		const FSPContractRunEvidence& Evidence);
	static void ApplyRewardOnSuccess(
		const USPContractDefinition& Definition,
		FSPContractEvaluationResult& Result);
};
