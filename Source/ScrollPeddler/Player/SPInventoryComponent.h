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
	bool HasCapacity() const { return Items.Num() < Capacity; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Inventory")
	FGuid GetFirstInstanceId() const;

	const TArray<FSPScrollInstance>& GetItems() const { return Items; }
	const FSPScrollInstance* FindItemByInstanceId(const FGuid& InstanceId) const;

	/** Server-only mutation. Returns false for invalid, duplicate, or over-capacity items. */
	bool TryAddItem(const FSPScrollInstance& Item);

	/** Server-only mutation. OutRemovedItem is assigned only when one item is committed. */
	bool RemoveItemByInstanceId(const FGuid& InstanceId, FSPScrollInstance& OutRemovedItem);

private:
	UFUNCTION()
	void OnRep_Items();

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	int32 Capacity = 4;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_Items, Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	TArray<FSPScrollInstance> Items;
};
