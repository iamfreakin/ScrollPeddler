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

	DOREPLIFETIME_CONDITION(USPInventoryComponent, Capacity, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USPInventoryComponent, Items, COND_OwnerOnly);
}

FGuid USPInventoryComponent::GetFirstInstanceId() const
{
	return Items.IsEmpty() ? FGuid() : Items[0].InstanceId;
}

const FSPScrollInstance* USPInventoryComponent::FindItemByInstanceId(const FGuid& InstanceId) const
{
	if (!InstanceId.IsValid())
	{
		return nullptr;
	}

	return Items.FindByPredicate(
		[&InstanceId](const FSPScrollInstance& Candidate)
		{
			return Candidate.InstanceId == InstanceId;
		});
}

bool USPInventoryComponent::TryAddItem(const FSPScrollInstance& Item)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || !Item.IsValid() || !HasCapacity() || FindItemByInstanceId(Item.InstanceId))
	{
		return false;
	}

	Items.Add(Item);
	OwnerActor->ForceNetUpdate();
	UE_LOG(LogScrollPeddler, Log, TEXT("[SP_TECH_SPIKE_INVENTORY_ADD] Owner=%s InstanceId=%s Count=%d"),
		*GetNameSafe(OwnerActor), *Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower), Items.Num());
	return true;
}

bool USPInventoryComponent::RemoveItemByInstanceId(const FGuid& InstanceId, FSPScrollInstance& OutRemovedItem)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || !InstanceId.IsValid())
	{
		return false;
	}

	const int32 Index = Items.IndexOfByPredicate(
		[&InstanceId](const FSPScrollInstance& Candidate)
		{
			return Candidate.InstanceId == InstanceId;
		});
	if (Index == INDEX_NONE)
	{
		return false;
	}

	OutRemovedItem = Items[Index];
	Items.RemoveAt(Index, 1, EAllowShrinking::No);
	OwnerActor->ForceNetUpdate();
	UE_LOG(LogScrollPeddler, Log, TEXT("[SP_TECH_SPIKE_INVENTORY_REMOVE] Owner=%s InstanceId=%s Count=%d"),
		*GetNameSafe(OwnerActor), *InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower), Items.Num());
	return true;
}

void USPInventoryComponent::OnRep_Items()
{
	UE_LOG(LogScrollPeddler, Verbose, TEXT("[SP_TECH_SPIKE_INVENTORY_REPLICATED] Owner=%s Count=%d"),
		*GetNameSafe(GetOwner()), Items.Num());
}
