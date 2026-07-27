#pragma once

#include "CoreMinimal.h"
#include "Core/SPItemTypes.h"
#include "UObject/PrimaryAssetId.h"
#include "SPEquipmentTypes.generated.h"

/**
 * Vertical-slice equipment slots. These represent runtime activation slots,
 * not inventory storage; an equipped entry keeps referring to the exact
 * authoritative inventory instance.
 */
UENUM(BlueprintType)
enum class ESPEquipmentSlot : uint8
{
	Tool,
	Protection,
	Utility,
	MAX UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ESPEquipmentOperation : uint8
{
	None,
	Equip,
	Unequip,
	Damage,
	Repair
};

UENUM(BlueprintType)
enum class ESPEquipmentMutationResult : uint8
{
	Success,
	InvalidRequest,
	InvalidState,
	StaleRevision,
	RequestConflict,
	InvalidSlot,
	InvalidItem,
	ItemReferenceMismatch,
	SlotOccupied,
	InstanceAlreadyEquipped,
	NotEquipped,
	AlreadyBroken,
	AlreadyFullyRepaired
};

/** Identity-only link back to an authoritative inventory item. */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPEquipmentItemReference
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FPrimaryAssetId DefinitionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FGuid InstanceId;

	bool IsValid() const;
	bool Matches(const FSPItemInstance& Item) const;

	friend bool operator==(
		const FSPEquipmentItemReference& First,
		const FSPEquipmentItemReference& Second)
	{
		return First.DefinitionId == Second.DefinitionId
			&& First.InstanceId == Second.InstanceId;
	}
};

/**
 * A server-resolved equipment rule. MaximumDurability and slot compatibility
 * must come from trusted definition data, never from the client request.
 */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPEquipmentRuntimeSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FPrimaryAssetId DefinitionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ESPEquipmentSlot Slot = ESPEquipmentSlot::Tool;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1"))
	int32 MaximumDurability = 100;

	bool IsValid() const;
	bool Matches(const FSPItemInstance& Item) const;
};

/**
 * Client/server intent identity. The effective durability maximum, damage
 * amount, and repair amount are intentionally absent and are supplied only by
 * authoritative callers.
 */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPEquipmentMutationRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 RequestId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ExpectedRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ESPEquipmentOperation Operation = ESPEquipmentOperation::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ESPEquipmentSlot Slot = ESPEquipmentSlot::Tool;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FSPEquipmentItemReference Item;

	bool IsValid() const;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPEquipmentSlotState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	ESPEquipmentSlot Slot = ESPEquipmentSlot::Tool;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	bool bOccupied = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FSPEquipmentItemReference Item;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	int32 CurrentDurability = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	int32 MaximumDurability = 0;

	bool IsStructurallyValid() const;
	bool IsUsable() const { return bOccupied && CurrentDurability > 0; }
	ESPEquipmentCondition GetCondition() const;

	/**
	 * Copies the runtime condition to a matching inventory snapshot. This does
	 * not mutate equipment state and rejects a different definition/instance.
	 */
	bool TryApplyConditionToItemSnapshot(FSPItemInstance& InOutItem) const;

	void Reset(ESPEquipmentSlot InSlot);
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPEquipmentMutationOutcome
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPEquipmentMutationResult Result = ESPEquipmentMutationResult::InvalidRequest;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 AuthoritativeRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bStateChanged = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bReplay = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPEquipmentSlot Slot = ESPEquipmentSlot::Tool;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FSPEquipmentItemReference Item;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 CurrentDurability = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 MaximumDurability = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPEquipmentCondition Condition = ESPEquipmentCondition::Good;

	bool IsSuccess() const { return Result == ESPEquipmentMutationResult::Success; }
};

USTRUCT()
struct SCROLLPEDDLER_API FSPEquipmentProcessedRequest
{
	GENERATED_BODY()

	UPROPERTY()
	FString Fingerprint;

	UPROPERTY()
	FSPEquipmentMutationOutcome Outcome;
};

/**
 * Pure authoritative equipment state.
 *
 * This type cannot inspect network roles, so only server-owned code may call
 * the Authority* mutation methods. Exact RequestId replays return the original
 * outcome without applying the mutation twice. Reusing a RequestId with any
 * changed payload or changed server-resolved amount is rejected as a conflict.
 */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPEquipmentState
{
	GENERATED_BODY()

	FSPEquipmentState();

	static constexpr int32 ReplayCapacity = 128;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	TArray<FSPEquipmentSlotState> Slots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	int32 Revision = 1;

	bool IsStructurallyValid() const;
	const FSPEquipmentSlotState* GetSlot(ESPEquipmentSlot Slot) const;
	const FSPEquipmentSlotState* FindSlotForInstance(const FGuid& InstanceId) const;

	FSPEquipmentMutationOutcome AuthorityEquip(
		const FSPEquipmentMutationRequest& Request,
		const FSPItemInstance& AuthoritativeInventoryItem,
		const FSPEquipmentRuntimeSpec& ServerSpec);

	FSPEquipmentMutationOutcome AuthorityUnequip(
		const FSPEquipmentMutationRequest& Request);

	FSPEquipmentMutationOutcome AuthorityApplyDamage(
		const FSPEquipmentMutationRequest& Request,
		int32 ServerResolvedDamage);

	FSPEquipmentMutationOutcome AuthorityApplyRepair(
		const FSPEquipmentMutationRequest& Request,
		int32 ServerResolvedRepair);

	void ResetReplayLedger();

private:
	enum class EReplayCheck : uint8
	{
		NewRequest,
		ExactReplay,
		Conflict
	};

	FSPEquipmentSlotState* GetMutableSlot(ESPEquipmentSlot Slot);
	FSPEquipmentMutationOutcome MakeOutcome(
		ESPEquipmentMutationResult Result,
		const FSPEquipmentMutationRequest& Request,
		bool bStateChanged) const;
	EReplayCheck CheckReplay(
		const FSPEquipmentMutationRequest& Request,
		const FString& Fingerprint,
		FSPEquipmentMutationOutcome& OutOutcome) const;
	void RecordOutcome(
		const FSPEquipmentMutationRequest& Request,
		const FString& Fingerprint,
		const FSPEquipmentMutationOutcome& Outcome);
	void AdvanceRevision();

	UPROPERTY()
	TMap<int64, FSPEquipmentProcessedRequest> ProcessedRequests;

	UPROPERTY()
	TArray<int64> RequestInsertionOrder;
};
