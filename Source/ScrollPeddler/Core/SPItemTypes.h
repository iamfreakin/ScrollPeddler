#pragma once

#include "CoreMinimal.h"
#include "Core/SPTypes.h"
#include "UObject/PrimaryAssetId.h"
#include "SPItemTypes.generated.h"

UENUM(BlueprintType)
enum class ESPItemKind : uint8
{
	Material,
	Scroll,
	Equipment,
	LargeCargo
};

UENUM(BlueprintType)
enum class ESPMaterialQuality : uint8
{
	D,
	C,
	B,
	A,
	S
};

UENUM(BlueprintType)
enum class ESPContaminationTier : uint8
{
	Clean,
	Trace,
	Contaminated,
	HighlyContaminated
};

UENUM(BlueprintType)
enum class ESPEquipmentCondition : uint8
{
	Good,
	Worn,
	Broken
};

UENUM(BlueprintType)
enum class ESPInventorySlotKind : uint8
{
	Hand,
	Bag
};

UENUM(BlueprintType)
enum class ESPInventoryRouteKind : uint8
{
	None,
	MergeStack,
	EmptyHand,
	EmptyBag,
	SwapHand
};

UENUM(BlueprintType)
enum class ESPInventoryMutationResult : uint8
{
	Success,
	InvalidItem,
	DuplicateInstance,
	StaleRevision,
	InventoryFull,
	InvalidSlot,
	NotFound,
	SlotRestricted
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPScrollRoll
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FPrimaryAssetId EngravingDefinitionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	ESPScrollQuality Quality = ESPScrollQuality::B;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float Contamination = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	ESPMisfireType Misfire = ESPMisfireType::None;

	bool IsValid() const;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPItemInstance
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FGuid InstanceId;

	/** Stable primary-asset identity. Scrolls retain their legacy BaseDefinitionId here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FPrimaryAssetId DefinitionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	ESPItemKind Kind = ESPItemKind::Material;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, meta = (ClampMin = "1", ClampMax = "10"))
	int32 Quantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FSPScrollRoll ScrollRoll;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	ESPMaterialQuality MaterialQuality = ESPMaterialQuality::B;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	ESPContaminationTier MaterialContamination = ESPContaminationTier::Clean;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	ESPEquipmentCondition EquipmentCondition = ESPEquipmentCondition::Good;

	/**
	 * Identities completely absorbed into this stack. They prevent a retried
	 * pickup from adding the same source stack twice.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	TArray<FGuid> MergedInstanceIds;

	bool IsValid() const;
	bool IsUnique() const;
	int32 GetMaxStackSize() const;
	bool CanStackWith(const FSPItemInstance& Other) const;
	bool ContainsAcceptedInstanceId(const FGuid& CandidateId) const;
	bool HasAnyIdentityInCommon(const FSPItemInstance& Other) const;

	static FSPItemInstance FromLegacyScroll(const FSPScrollInstance& LegacyScroll);
	bool TryToLegacyScroll(FSPScrollInstance& OutLegacyScroll) const;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPInventorySlotRef
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPInventorySlotKind Kind = ESPInventorySlotKind::Hand;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 BagIndex = INDEX_NONE;

	static FSPInventorySlotRef Hand();
	static FSPInventorySlotRef Bag(int32 InBagIndex);
	bool IsValid(int32 BagCapacity) const;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPInventoryRoute
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPInventoryRouteKind Kind = ESPInventoryRouteKind::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FSPInventorySlotRef Destination;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPInventoryMutationResult Failure = ESPInventoryMutationResult::InventoryFull;

	bool IsSuccess() const { return Kind != ESPInventoryRouteKind::None; }
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPInventorySlot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	bool bOccupied = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FSPItemInstance Item;

	bool IsValid() const { return !bOccupied || Item.IsValid(); }
	void Reset();
};

/**
 * Pure authoritative inventory state. Component RPC/authority checks wrap these
 * operations; keeping the state pure makes routing and stale-revision behavior
 * deterministic and independently testable.
 */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPInventoryState
{
	GENERATED_BODY()

	static constexpr int32 BagCapacity = 4;
	static constexpr int32 TotalSlotCapacity = BagCapacity + 1;

	FSPInventoryState();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FSPInventorySlot HandSlot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	TArray<FSPInventorySlot> BagSlots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	int32 Revision = 1;

	bool IsStructurallyValid() const;
	int32 GetOccupiedSlotCount() const;
	bool HasEmptySlot() const;
	const FSPItemInstance* FindItemByInstanceId(const FGuid& InstanceId) const;
	const FSPInventorySlot* GetSlot(const FSPInventorySlotRef& SlotRef) const;

	FSPInventoryRoute CalculatePickupRoute(
		const FSPItemInstance& Item,
		bool bAllowHandSwap = false) const;

	bool TryAddItem(
		const FSPItemInstance& Item,
		int32 ExpectedRevision,
		bool bAllowHandSwap,
		FSPItemInstance& OutDisplacedItem,
		ESPInventoryMutationResult& OutResult);

	bool TryRemoveItemByInstanceId(
		const FGuid& InstanceId,
		int32 ExpectedRevision,
		FSPItemInstance& OutRemovedItem,
		ESPInventoryMutationResult& OutResult);

	bool TrySwapHandWithBag(
		int32 BagIndex,
		int32 ExpectedRevision,
		ESPInventoryMutationResult& OutResult);

private:
	FSPInventorySlot* GetMutableSlot(const FSPInventorySlotRef& SlotRef);
	bool ContainsAnyAcceptedIdentity(const FSPItemInstance& Item) const;
	void AdvanceRevision();
};
