#include "Player/SPInventoryComponent.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "ScrollPeddler.h"

USPInventoryComponent::USPInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USPInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(USPInventoryComponent, InventoryState, COND_OwnerOnly);
}

FGuid USPInventoryComponent::GetFirstInstanceId() const
{
	return LegacyScrollItems.IsEmpty() ? FGuid() : LegacyScrollItems[0].InstanceId;
}

const FSPItemInstance* USPInventoryComponent::FindItemInstanceById(const FGuid& InstanceId) const
{
	return InventoryState.FindItemByInstanceId(InstanceId);
}

const FSPScrollInstance* USPInventoryComponent::FindItemByInstanceId(const FGuid& InstanceId) const
{
	if (!InstanceId.IsValid())
	{
		return nullptr;
	}

	return LegacyScrollItems.FindByPredicate(
		[&InstanceId](const FSPScrollInstance& Candidate)
		{
			return Candidate.InstanceId == InstanceId;
		});
}

bool USPInventoryComponent::TryAddItem(
	const FSPItemInstance& Item,
	const int32 ExpectedRevision,
	const bool bAllowHandSwap,
	FSPItemInstance& OutDisplacedItem,
	ESPInventoryMutationResult& OutResult)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		OutResult = ESPInventoryMutationResult::InvalidItem;
		return false;
	}

	if (!InventoryState.TryAddItem(
		Item,
		ExpectedRevision,
		bAllowHandSwap,
		OutDisplacedItem,
		OutResult))
	{
		return false;
	}

	RebuildLegacyScrollProjection();
	NotifyInventoryMutation(TEXT("ADD"), Item.InstanceId);
	return true;
}

bool USPInventoryComponent::RemoveItemByInstanceId(
	const FGuid& InstanceId,
	const int32 ExpectedRevision,
	FSPItemInstance& OutRemovedItem,
	ESPInventoryMutationResult& OutResult)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		OutResult = ESPInventoryMutationResult::InvalidItem;
		return false;
	}

	if (!InventoryState.TryRemoveItemByInstanceId(
		InstanceId,
		ExpectedRevision,
		OutRemovedItem,
		OutResult))
	{
		return false;
	}

	RebuildLegacyScrollProjection();
	NotifyInventoryMutation(TEXT("REMOVE"), InstanceId);
	return true;
}

bool USPInventoryComponent::SwapHandWithBag(
	const int32 BagIndex,
	const int32 ExpectedRevision,
	ESPInventoryMutationResult& OutResult)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		OutResult = ESPInventoryMutationResult::InvalidItem;
		return false;
	}

	if (!InventoryState.TrySwapHandWithBag(BagIndex, ExpectedRevision, OutResult))
	{
		return false;
	}

	RebuildLegacyScrollProjection();
	NotifyInventoryMutation(TEXT("SWAP"), FGuid());
	return true;
}

bool USPInventoryComponent::AuthorityRestoreState(
	const FSPInventoryState& Snapshot)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority()
		|| !Snapshot.IsStructurallyValid())
	{
		return false;
	}

	InventoryState = Snapshot;
	RebuildLegacyScrollProjection();
	NotifyInventoryMutation(TEXT("RESTORE"), FGuid());
	return true;
}

bool USPInventoryComponent::TryAddItem(const FSPScrollInstance& Item)
{
	FSPItemInstance DisplacedItem;
	ESPInventoryMutationResult Result = ESPInventoryMutationResult::InvalidItem;
	return TryAddItem(
		FSPItemInstance::FromLegacyScroll(Item),
		InventoryState.Revision,
		false,
		DisplacedItem,
		Result);
}

bool USPInventoryComponent::RemoveItemByInstanceId(
	const FGuid& InstanceId,
	FSPScrollInstance& OutRemovedItem)
{
	const FSPItemInstance* ExistingItem = InventoryState.FindItemByInstanceId(InstanceId);
	if (!ExistingItem || ExistingItem->Kind != ESPItemKind::Scroll)
	{
		return false;
	}

	FSPItemInstance RemovedItem;
	ESPInventoryMutationResult Result = ESPInventoryMutationResult::InvalidItem;
	if (!RemoveItemByInstanceId(
		InstanceId,
		InventoryState.Revision,
		RemovedItem,
		Result))
	{
		return false;
	}

	const bool bConverted = RemovedItem.TryToLegacyScroll(OutRemovedItem);
	check(bConverted);
	return bConverted;
}

void USPInventoryComponent::OnRep_InventoryState()
{
	RebuildLegacyScrollProjection();
	UE_LOG(LogScrollPeddler, Verbose, TEXT("[SP_INVENTORY_REPLICATED] Owner=%s Slots=%d Revision=%d"),
		*GetNameSafe(GetOwner()), InventoryState.GetOccupiedSlotCount(), InventoryState.Revision);
}

void USPInventoryComponent::RebuildLegacyScrollProjection()
{
	LegacyScrollItems.Reset();
	auto AddScrollIfPresent =
		[this](const FSPInventorySlot& Slot)
		{
			if (!Slot.bOccupied)
			{
				return;
			}

			FSPScrollInstance LegacyScroll;
			if (Slot.Item.TryToLegacyScroll(LegacyScroll))
			{
				LegacyScrollItems.Add(MoveTemp(LegacyScroll));
			}
		};

	AddScrollIfPresent(InventoryState.HandSlot);
	for (const FSPInventorySlot& BagSlot : InventoryState.BagSlots)
	{
		AddScrollIfPresent(BagSlot);
	}
}

void USPInventoryComponent::NotifyInventoryMutation(
	const TCHAR* Operation,
	const FGuid& InstanceId)
{
	AActor* OwnerActor = GetOwner();
	check(OwnerActor && OwnerActor->HasAuthority());
	OwnerActor->ForceNetUpdate();
	UE_LOG(LogScrollPeddler, Log, TEXT("[SP_INVENTORY_%s] Owner=%s InstanceId=%s Slots=%d Revision=%d"),
		Operation,
		*GetNameSafe(OwnerActor),
		InstanceId.IsValid()
			? *InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower)
			: TEXT("None"),
		InventoryState.GetOccupiedSlotCount(),
		InventoryState.Revision);
}
