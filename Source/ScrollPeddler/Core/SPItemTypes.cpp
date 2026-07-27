#include "Core/SPItemTypes.h"

namespace
{
bool IsKnownItemKind(const ESPItemKind Kind)
{
	return StaticEnum<ESPItemKind>()->IsValidEnumValue(static_cast<int64>(Kind));
}
}

bool FSPScrollRoll::IsValid() const
{
	return EngravingDefinitionId.IsValid()
		&& StaticEnum<ESPScrollQuality>()->IsValidEnumValue(static_cast<int64>(Quality))
		&& StaticEnum<ESPMisfireType>()->IsValidEnumValue(static_cast<int64>(Misfire))
		&& FMath::IsFinite(Contamination)
		&& Contamination >= 0.0f
		&& Contamination <= 100.0f;
}

bool FSPItemInstance::IsValid() const
{
	if (!InstanceId.IsValid()
		|| !DefinitionId.IsValid()
		|| !IsKnownItemKind(Kind)
		|| Quantity < 1
		|| Quantity > GetMaxStackSize())
	{
		return false;
	}

	if (Kind == ESPItemKind::Scroll && !ScrollRoll.IsValid())
	{
		return false;
	}
	if (Kind == ESPItemKind::Material
		&& (!StaticEnum<ESPMaterialQuality>()->IsValidEnumValue(static_cast<int64>(MaterialQuality))
			|| !StaticEnum<ESPContaminationTier>()->IsValidEnumValue(
				static_cast<int64>(MaterialContamination))))
	{
		return false;
	}
	if (Kind == ESPItemKind::Equipment
		&& !StaticEnum<ESPEquipmentCondition>()->IsValidEnumValue(
			static_cast<int64>(EquipmentCondition)))
	{
		return false;
	}

	TSet<FGuid> SeenIds;
	SeenIds.Add(InstanceId);
	for (const FGuid& MergedId : MergedInstanceIds)
	{
		if (!MergedId.IsValid() || SeenIds.Contains(MergedId))
		{
			return false;
		}
		SeenIds.Add(MergedId);
	}

	return true;
}

bool FSPItemInstance::IsUnique() const
{
	return Kind != ESPItemKind::Material;
}

int32 FSPItemInstance::GetMaxStackSize() const
{
	return Kind == ESPItemKind::Material ? 10 : 1;
}

bool FSPItemInstance::CanStackWith(const FSPItemInstance& Other) const
{
	return IsValid()
		&& Other.IsValid()
		&& Kind == ESPItemKind::Material
		&& Other.Kind == ESPItemKind::Material
		&& DefinitionId == Other.DefinitionId
		&& MaterialQuality == Other.MaterialQuality
		&& MaterialContamination == Other.MaterialContamination;
}

bool FSPItemInstance::ContainsAcceptedInstanceId(const FGuid& CandidateId) const
{
	return CandidateId.IsValid()
		&& (InstanceId == CandidateId || MergedInstanceIds.Contains(CandidateId));
}

bool FSPItemInstance::HasAnyIdentityInCommon(const FSPItemInstance& Other) const
{
	if (ContainsAcceptedInstanceId(Other.InstanceId))
	{
		return true;
	}

	for (const FGuid& OtherMergedId : Other.MergedInstanceIds)
	{
		if (ContainsAcceptedInstanceId(OtherMergedId))
		{
			return true;
		}
	}
	return false;
}

FSPItemInstance FSPItemInstance::FromLegacyScroll(const FSPScrollInstance& LegacyScroll)
{
	FSPItemInstance Item;
	Item.InstanceId = LegacyScroll.InstanceId;
	Item.DefinitionId = LegacyScroll.BaseDefinitionId;
	Item.Kind = ESPItemKind::Scroll;
	Item.Quantity = 1;
	Item.ScrollRoll.EngravingDefinitionId = LegacyScroll.EngravingDefinitionId;
	Item.ScrollRoll.Quality = LegacyScroll.Quality;
	Item.ScrollRoll.Contamination = LegacyScroll.Contamination;
	Item.ScrollRoll.Misfire = LegacyScroll.Misfire;
	return Item;
}

bool FSPItemInstance::TryToLegacyScroll(FSPScrollInstance& OutLegacyScroll) const
{
	if (Kind != ESPItemKind::Scroll || !IsValid())
	{
		return false;
	}

	FSPScrollInstance LegacyScroll;
	LegacyScroll.InstanceId = InstanceId;
	LegacyScroll.BaseDefinitionId = DefinitionId;
	LegacyScroll.EngravingDefinitionId = ScrollRoll.EngravingDefinitionId;
	LegacyScroll.Quality = ScrollRoll.Quality;
	LegacyScroll.Contamination = ScrollRoll.Contamination;
	LegacyScroll.Misfire = ScrollRoll.Misfire;
	OutLegacyScroll = MoveTemp(LegacyScroll);
	return true;
}

FSPInventorySlotRef FSPInventorySlotRef::Hand()
{
	FSPInventorySlotRef SlotRef;
	SlotRef.Kind = ESPInventorySlotKind::Hand;
	SlotRef.BagIndex = INDEX_NONE;
	return SlotRef;
}

FSPInventorySlotRef FSPInventorySlotRef::Bag(const int32 InBagIndex)
{
	FSPInventorySlotRef SlotRef;
	SlotRef.Kind = ESPInventorySlotKind::Bag;
	SlotRef.BagIndex = InBagIndex;
	return SlotRef;
}

bool FSPInventorySlotRef::IsValid(const int32 BagCapacity) const
{
	return Kind == ESPInventorySlotKind::Hand
		? BagIndex == INDEX_NONE
		: Kind == ESPInventorySlotKind::Bag && BagIndex >= 0 && BagIndex < BagCapacity;
}

void FSPInventorySlot::Reset()
{
	bOccupied = false;
	Item = FSPItemInstance();
}

FSPInventoryState::FSPInventoryState()
{
	BagSlots.SetNum(BagCapacity);
}

bool FSPInventoryState::IsStructurallyValid() const
{
	if (BagSlots.Num() != BagCapacity || Revision <= 0 || !HandSlot.IsValid())
	{
		return false;
	}

	TArray<const FSPItemInstance*> OccupiedItems;
	if (HandSlot.bOccupied)
	{
		OccupiedItems.Add(&HandSlot.Item);
	}

	for (const FSPInventorySlot& BagSlot : BagSlots)
	{
		if (!BagSlot.IsValid())
		{
			return false;
		}
		if (BagSlot.bOccupied)
		{
			if (BagSlot.Item.Kind == ESPItemKind::LargeCargo)
			{
				return false;
			}
			OccupiedItems.Add(&BagSlot.Item);
		}
	}

	for (int32 FirstIndex = 0; FirstIndex < OccupiedItems.Num(); ++FirstIndex)
	{
		for (int32 SecondIndex = FirstIndex + 1; SecondIndex < OccupiedItems.Num(); ++SecondIndex)
		{
			if (OccupiedItems[FirstIndex]->HasAnyIdentityInCommon(*OccupiedItems[SecondIndex]))
			{
				return false;
			}
		}
	}

	return true;
}

int32 FSPInventoryState::GetOccupiedSlotCount() const
{
	int32 Count = HandSlot.bOccupied ? 1 : 0;
	for (const FSPInventorySlot& BagSlot : BagSlots)
	{
		Count += BagSlot.bOccupied ? 1 : 0;
	}
	return Count;
}

bool FSPInventoryState::HasEmptySlot() const
{
	if (!HandSlot.bOccupied)
	{
		return true;
	}

	return BagSlots.ContainsByPredicate(
		[](const FSPInventorySlot& Slot)
		{
			return !Slot.bOccupied;
		});
}

const FSPItemInstance* FSPInventoryState::FindItemByInstanceId(const FGuid& InstanceId) const
{
	if (!InstanceId.IsValid())
	{
		return nullptr;
	}

	if (HandSlot.bOccupied && HandSlot.Item.InstanceId == InstanceId)
	{
		return &HandSlot.Item;
	}

	for (const FSPInventorySlot& BagSlot : BagSlots)
	{
		if (BagSlot.bOccupied && BagSlot.Item.InstanceId == InstanceId)
		{
			return &BagSlot.Item;
		}
	}
	return nullptr;
}

const FSPInventorySlot* FSPInventoryState::GetSlot(const FSPInventorySlotRef& SlotRef) const
{
	if (!SlotRef.IsValid(BagCapacity))
	{
		return nullptr;
	}
	return SlotRef.Kind == ESPInventorySlotKind::Hand
		? &HandSlot
		: &BagSlots[SlotRef.BagIndex];
}

FSPInventoryRoute FSPInventoryState::CalculatePickupRoute(
	const FSPItemInstance& Item,
	const bool bAllowHandSwap) const
{
	FSPInventoryRoute Route;
	if (!Item.IsValid() || !IsStructurallyValid())
	{
		Route.Failure = ESPInventoryMutationResult::InvalidItem;
		return Route;
	}
	if (ContainsAnyAcceptedIdentity(Item))
	{
		Route.Failure = ESPInventoryMutationResult::DuplicateInstance;
		return Route;
	}

	if (!HandSlot.bOccupied)
	{
		Route.Kind = ESPInventoryRouteKind::EmptyHand;
		Route.Destination = FSPInventorySlotRef::Hand();
		Route.Failure = ESPInventoryMutationResult::Success;
		return Route;
	}

	if (Item.Kind == ESPItemKind::Material)
	{
		if (HandSlot.Item.CanStackWith(Item)
			&& HandSlot.Item.Quantity + Item.Quantity <= HandSlot.Item.GetMaxStackSize())
		{
			Route.Kind = ESPInventoryRouteKind::MergeStack;
			Route.Destination = FSPInventorySlotRef::Hand();
			Route.Failure = ESPInventoryMutationResult::Success;
			return Route;
		}

		for (int32 BagIndex = 0; BagIndex < BagSlots.Num(); ++BagIndex)
		{
			const FSPInventorySlot& Slot = BagSlots[BagIndex];
			if (Slot.bOccupied
				&& Slot.Item.CanStackWith(Item)
				&& Slot.Item.Quantity + Item.Quantity <= Slot.Item.GetMaxStackSize())
			{
				Route.Kind = ESPInventoryRouteKind::MergeStack;
				Route.Destination = FSPInventorySlotRef::Bag(BagIndex);
				Route.Failure = ESPInventoryMutationResult::Success;
				return Route;
			}
		}
	}

	if (Item.Kind != ESPItemKind::LargeCargo)
	{
		for (int32 BagIndex = 0; BagIndex < BagSlots.Num(); ++BagIndex)
		{
			if (!BagSlots[BagIndex].bOccupied)
			{
				Route.Kind = ESPInventoryRouteKind::EmptyBag;
				Route.Destination = FSPInventorySlotRef::Bag(BagIndex);
				Route.Failure = ESPInventoryMutationResult::Success;
				return Route;
			}
		}
	}

	if (bAllowHandSwap)
	{
		Route.Kind = ESPInventoryRouteKind::SwapHand;
		Route.Destination = FSPInventorySlotRef::Hand();
		Route.Failure = ESPInventoryMutationResult::Success;
		return Route;
	}

	Route.Failure = Item.Kind == ESPItemKind::LargeCargo
		? ESPInventoryMutationResult::SlotRestricted
		: ESPInventoryMutationResult::InventoryFull;
	return Route;
}

bool FSPInventoryState::TryAddItem(
	const FSPItemInstance& Item,
	const int32 ExpectedRevision,
	const bool bAllowHandSwap,
	FSPItemInstance& OutDisplacedItem,
	ESPInventoryMutationResult& OutResult)
{
	if (ExpectedRevision != Revision)
	{
		OutResult = ESPInventoryMutationResult::StaleRevision;
		return false;
	}

	const FSPInventoryRoute Route = CalculatePickupRoute(Item, bAllowHandSwap);
	if (!Route.IsSuccess())
	{
		OutResult = Route.Failure;
		return false;
	}

	FSPInventorySlot* Destination = GetMutableSlot(Route.Destination);
	if (!Destination)
	{
		OutResult = ESPInventoryMutationResult::InvalidSlot;
		return false;
	}

	OutDisplacedItem = FSPItemInstance();
	switch (Route.Kind)
	{
	case ESPInventoryRouteKind::MergeStack:
		Destination->Item.Quantity += Item.Quantity;
		Destination->Item.MergedInstanceIds.Add(Item.InstanceId);
		Destination->Item.MergedInstanceIds.Append(Item.MergedInstanceIds);
		break;

	case ESPInventoryRouteKind::SwapHand:
		check(Destination->bOccupied);
		OutDisplacedItem = Destination->Item;
		Destination->Item = Item;
		Destination->bOccupied = true;
		break;

	case ESPInventoryRouteKind::EmptyHand:
	case ESPInventoryRouteKind::EmptyBag:
		check(!Destination->bOccupied);
		Destination->Item = Item;
		Destination->bOccupied = true;
		break;

	default:
		OutResult = ESPInventoryMutationResult::InvalidSlot;
		return false;
	}

	AdvanceRevision();
	OutResult = ESPInventoryMutationResult::Success;
	return true;
}

bool FSPInventoryState::TryRemoveItemByInstanceId(
	const FGuid& InstanceId,
	const int32 ExpectedRevision,
	FSPItemInstance& OutRemovedItem,
	ESPInventoryMutationResult& OutResult)
{
	if (ExpectedRevision != Revision)
	{
		OutResult = ESPInventoryMutationResult::StaleRevision;
		return false;
	}
	if (!IsStructurallyValid())
	{
		OutResult = ESPInventoryMutationResult::InvalidItem;
		return false;
	}
	if (!InstanceId.IsValid())
	{
		OutResult = ESPInventoryMutationResult::InvalidItem;
		return false;
	}

	FSPInventorySlot* FoundSlot = nullptr;
	if (HandSlot.bOccupied && HandSlot.Item.InstanceId == InstanceId)
	{
		FoundSlot = &HandSlot;
	}
	else
	{
		for (FSPInventorySlot& BagSlot : BagSlots)
		{
			if (BagSlot.bOccupied && BagSlot.Item.InstanceId == InstanceId)
			{
				FoundSlot = &BagSlot;
				break;
			}
		}
	}

	if (!FoundSlot)
	{
		OutResult = ESPInventoryMutationResult::NotFound;
		return false;
	}

	OutRemovedItem = FoundSlot->Item;
	FoundSlot->Reset();
	AdvanceRevision();
	OutResult = ESPInventoryMutationResult::Success;
	return true;
}

bool FSPInventoryState::TrySwapHandWithBag(
	const int32 BagIndex,
	const int32 ExpectedRevision,
	ESPInventoryMutationResult& OutResult)
{
	if (ExpectedRevision != Revision)
	{
		OutResult = ESPInventoryMutationResult::StaleRevision;
		return false;
	}
	if (!IsStructurallyValid())
	{
		OutResult = ESPInventoryMutationResult::InvalidItem;
		return false;
	}
	if (!BagSlots.IsValidIndex(BagIndex))
	{
		OutResult = ESPInventoryMutationResult::InvalidSlot;
		return false;
	}
	if (!HandSlot.bOccupied && !BagSlots[BagIndex].bOccupied)
	{
		OutResult = ESPInventoryMutationResult::NotFound;
		return false;
	}
	if (HandSlot.bOccupied && HandSlot.Item.Kind == ESPItemKind::LargeCargo)
	{
		OutResult = ESPInventoryMutationResult::SlotRestricted;
		return false;
	}

	Swap(HandSlot, BagSlots[BagIndex]);
	AdvanceRevision();
	OutResult = ESPInventoryMutationResult::Success;
	return true;
}

FSPInventorySlot* FSPInventoryState::GetMutableSlot(const FSPInventorySlotRef& SlotRef)
{
	if (!SlotRef.IsValid(BagCapacity))
	{
		return nullptr;
	}
	return SlotRef.Kind == ESPInventorySlotKind::Hand
		? &HandSlot
		: &BagSlots[SlotRef.BagIndex];
}

bool FSPInventoryState::ContainsAnyAcceptedIdentity(const FSPItemInstance& Item) const
{
	if (HandSlot.bOccupied && HandSlot.Item.HasAnyIdentityInCommon(Item))
	{
		return true;
	}
	for (const FSPInventorySlot& BagSlot : BagSlots)
	{
		if (BagSlot.bOccupied && BagSlot.Item.HasAnyIdentityInCommon(Item))
		{
			return true;
		}
	}
	return false;
}

void FSPInventoryState::AdvanceRevision()
{
	Revision = Revision >= MAX_int32 ? 1 : Revision + 1;
}
