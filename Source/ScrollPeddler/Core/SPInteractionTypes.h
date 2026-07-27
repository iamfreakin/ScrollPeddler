#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "SPInteractionTypes.generated.h"

UENUM(BlueprintType)
enum class ESPInteractionAction : uint8
{
	None,
	Pickup,
	Drop,
	Use,
	Swap,
	Treat,
	Stabilize,
	Extract,
	Deliver,
	Craft,
	Vote
};

UENUM(BlueprintType)
enum class ESPInteractionResultCode : uint8
{
	Success,
	InvalidRequest,
	InvalidState,
	OutOfRange,
	Obstructed,
	NotOwner,
	InventoryFull,
	Unavailable,
	Contested,
	StaleRevision,
	RequestConflict,
	SaveFailed,
	ServerError
};

/**
 * Client intent shared by authoritative interactions.
 *
 * RequestId is scoped to one owning player. ExpectedRevision is the last
 * authoritative revision observed by that player; INDEX_NONE means the action
 * has no revisioned container.
 */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPInteractionRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 RequestId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ESPInteractionAction Action = ESPInteractionAction::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGuid TargetInstanceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ExpectedRevision = INDEX_NONE;

	bool IsValid() const
	{
		return RequestId > 0 && Action != ESPInteractionAction::None;
	}
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPInteractionResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 RequestId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPInteractionAction Action = ESPInteractionAction::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPInteractionResultCode Code = ESPInteractionResultCode::InvalidRequest;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 AuthoritativeRevision = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGameplayTag FeedbackTag;

	bool IsSuccess() const
	{
		return Code == ESPInteractionResultCode::Success;
	}
};

SCROLLPEDDLER_API bool SPAreSameInteractionRequest(
	const FSPInteractionRequest& A,
	const FSPInteractionRequest& B);

/**
 * Small server-side replay ledger. Exact request replays return the recorded
 * terminal result; reusing an id for different intent is a conflict.
 */
class SCROLLPEDDLER_API FSPInteractionReplayLedger
{
public:
	enum class ELookup : uint8
	{
		NotFound,
		ExactReplay,
		Conflict
	};

	explicit FSPInteractionReplayLedger(int32 InCapacity = 64);

	ELookup Find(
		const FSPInteractionRequest& Request,
		FSPInteractionResult& OutResult) const;

	bool Record(
		const FSPInteractionRequest& Request,
		const FSPInteractionResult& Result);

	void Reset();
	int32 Num() const { return Records.Num(); }

private:
	struct FRecord
	{
		FSPInteractionRequest Request;
		FSPInteractionResult Result;
	};

	void TrimToCapacity();

	int32 Capacity = 64;
	TMap<int64, FRecord> Records;
	TArray<int64> InsertionOrder;
};
