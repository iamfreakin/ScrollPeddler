#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/SPItemTypes.h"
#include "SPInventoryComponent.generated.h"

UCLASS(ClassGroup = (ScrollPeddler), meta = (BlueprintSpawnableComponent))
class SCROLLPEDDLER_API USPInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USPInventoryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Inventory")
	int32 GetItemCount() const { return InventoryState.GetOccupiedSlotCount(); }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Inventory")
	int32 GetCapacity() const { return FSPInventoryState::TotalSlotCapacity; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Inventory")
	int32 GetBagCapacity() const { return FSPInventoryState::BagCapacity; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Inventory")
	bool HasCapacity() const { return InventoryState.HasEmptySlot(); }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Inventory")
	FGuid GetFirstInstanceId() const;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Inventory")
	int32 GetInventoryRevision() const { return InventoryState.Revision; }

	const FSPInventoryState& GetInventoryState() const { return InventoryState; }
	const FSPItemInstance* FindItemInstanceById(const FGuid& InstanceId) const;

	/** Compatibility projection for legacy scroll-only callers. */
	const TArray<FSPScrollInstance>& GetItems() const { return LegacyScrollItems; }
	const FSPScrollInstance* FindItemByInstanceId(const FGuid& InstanceId) const;

	/** Server-only generic mutation with stale-revision validation. */
	bool TryAddItem(
		const FSPItemInstance& Item,
		int32 ExpectedRevision,
		bool bAllowHandSwap,
		FSPItemInstance& OutDisplacedItem,
		ESPInventoryMutationResult& OutResult);

	/** Server-only exact-instance removal with stale-revision validation. */
	bool RemoveItemByInstanceId(
		const FGuid& InstanceId,
		int32 ExpectedRevision,
		FSPItemInstance& OutRemovedItem,
		ESPInventoryMutationResult& OutResult);

	/** Server-only atomic hand/bag exchange. */
	bool SwapHandWithBag(
		int32 BagIndex,
		int32 ExpectedRevision,
		ESPInventoryMutationResult& OutResult);

	/** Restores an already validated host-owned reconnect snapshot. */
	bool AuthorityRestoreState(const FSPInventoryState& Snapshot);

	/** Legacy server-only scroll mutation adapter. */
	bool TryAddItem(const FSPScrollInstance& Item);

	/** Legacy server-only scroll removal adapter. */
	bool RemoveItemByInstanceId(const FGuid& InstanceId, FSPScrollInstance& OutRemovedItem);

private:
	UFUNCTION()
	void OnRep_InventoryState();

	void RebuildLegacyScrollProjection();
	void NotifyInventoryMutation(const TCHAR* Operation, const FGuid& InstanceId);

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_InventoryState, Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	FSPInventoryState InventoryState;

	UPROPERTY(Transient)
	TArray<FSPScrollInstance> LegacyScrollItems;
};
