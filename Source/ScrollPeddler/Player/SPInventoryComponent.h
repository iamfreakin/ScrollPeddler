#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/SPTypes.h"
#include "SPInventoryComponent.generated.h"

UCLASS(ClassGroup = (ScrollPeddler), meta = (BlueprintSpawnableComponent))
class SCROLLPEDDLER_API USPInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USPInventoryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Inventory")
	int32 GetItemCount() const { return Items.Num(); }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Inventory")
	int32 GetCapacity() const { return Capacity; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Inventory")
	bool HasCapacity() const { return HasCapacityForCount(Items.Num(), Capacity); }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Inventory")
	FGuid GetFirstInstanceId() const;

	const TArray<FSPScrollInstance>& GetItems() const { return Items; }
	const FSPScrollInstance* FindItemByInstanceId(const FGuid& InstanceId) const;

	/** 서버 전용 변경이며 invalid·duplicate·over-capacity 항목은 false를 반환한다. */
	bool TryAddItem(const FSPScrollInstance& Item);

	/** 서버 전용 변경이며 실제 제거가 확정됐을 때만 OutRemovedItem을 채운다. */
	bool RemoveItemByInstanceId(const FGuid& InstanceId, FSPScrollInstance& OutRemovedItem);

private:
	static bool HasCapacityForCount(int32 ItemCount, int32 CapacityLimit)
	{
		return ItemCount < CapacityLimit;
	}

	UFUNCTION()
	void OnRep_Items();

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	int32 Capacity = 4;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_Items, Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	TArray<FSPScrollInstance> Items;

	friend class FSPInventoryCapacityBoundaryTest;
};
