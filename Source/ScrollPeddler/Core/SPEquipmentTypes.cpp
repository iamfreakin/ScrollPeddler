#include "Core/SPEquipmentTypes.h"

namespace
{
bool SPIsKnownEquipmentSlot(const ESPEquipmentSlot Slot)
{
	return static_cast<uint8>(Slot) < static_cast<uint8>(ESPEquipmentSlot::MAX);
}

bool SPIsKnownEquipmentOperation(const ESPEquipmentOperation Operation)
{
	return Operation > ESPEquipmentOperation::None
		&& StaticEnum<ESPEquipmentOperation>()->IsValidEnumValue(static_cast<int64>(Operation));
}

int32 SPInitialDurability(
	const ESPEquipmentCondition Condition,
	const int32 MaximumDurability)
{
	switch (Condition)
	{
	case ESPEquipmentCondition::Good:
		return MaximumDurability;
	case ESPEquipmentCondition::Worn:
		return FMath::Max(1, MaximumDurability / 2);
	case ESPEquipmentCondition::Broken:
		return 0;
	default:
		return INDEX_NONE;
	}
}

FString SPBuildRequestFingerprint(
	const TCHAR* Method,
	const FSPEquipmentMutationRequest& Request,
	const int32 ServerResolvedValue = INDEX_NONE,
	const FString& ServerContext = FString())
{
	return FString::Printf(
		TEXT("%s|%d|%d|%d|%s|%s|%d|%s"),
		Method,
		Request.ExpectedRevision,
		static_cast<int32>(Request.Operation),
		static_cast<int32>(Request.Slot),
		*Request.Item.DefinitionId.ToString(),
		*Request.Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower),
		ServerResolvedValue,
		*ServerContext);
}
}

bool FSPEquipmentItemReference::IsValid() const
{
	return DefinitionId.IsValid() && InstanceId.IsValid();
}

bool FSPEquipmentItemReference::Matches(const FSPItemInstance& Item) const
{
	return IsValid()
		&& Item.IsValid()
		&& Item.Kind == ESPItemKind::Equipment
		&& Item.Quantity == 1
		&& DefinitionId == Item.DefinitionId
		&& InstanceId == Item.InstanceId;
}

bool FSPEquipmentRuntimeSpec::IsValid() const
{
	return DefinitionId.IsValid()
		&& SPIsKnownEquipmentSlot(Slot)
		&& MaximumDurability > 0;
}

bool FSPEquipmentRuntimeSpec::Matches(const FSPItemInstance& Item) const
{
	return IsValid()
		&& Item.IsValid()
		&& Item.Kind == ESPItemKind::Equipment
		&& Item.Quantity == 1
		&& DefinitionId == Item.DefinitionId;
}

bool FSPEquipmentMutationRequest::IsValid() const
{
	return RequestId > 0
		&& ExpectedRevision > 0
		&& SPIsKnownEquipmentOperation(Operation)
		&& SPIsKnownEquipmentSlot(Slot)
		&& Item.IsValid();
}

bool FSPEquipmentSlotState::IsStructurallyValid() const
{
	if (!SPIsKnownEquipmentSlot(Slot))
	{
		return false;
	}

	if (!bOccupied)
	{
		return !Item.IsValid()
			&& CurrentDurability == 0
			&& MaximumDurability == 0;
	}

	return Item.IsValid()
		&& MaximumDurability > 0
		&& CurrentDurability >= 0
		&& CurrentDurability <= MaximumDurability;
}

ESPEquipmentCondition FSPEquipmentSlotState::GetCondition() const
{
	if (!bOccupied || CurrentDurability <= 0)
	{
		return ESPEquipmentCondition::Broken;
	}

	return CurrentDurability < MaximumDurability
		? ESPEquipmentCondition::Worn
		: ESPEquipmentCondition::Good;
}

bool FSPEquipmentSlotState::TryApplyConditionToItemSnapshot(
	FSPItemInstance& InOutItem) const
{
	if (!bOccupied || !Item.Matches(InOutItem))
	{
		return false;
	}

	InOutItem.EquipmentCondition = GetCondition();
	return true;
}

void FSPEquipmentSlotState::Reset(const ESPEquipmentSlot InSlot)
{
	Slot = InSlot;
	bOccupied = false;
	Item = FSPEquipmentItemReference();
	CurrentDurability = 0;
	MaximumDurability = 0;
}

FSPEquipmentState::FSPEquipmentState()
{
	const int32 SlotCount = static_cast<int32>(ESPEquipmentSlot::MAX);
	Slots.SetNum(SlotCount);
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		Slots[SlotIndex].Reset(static_cast<ESPEquipmentSlot>(SlotIndex));
	}
}

bool FSPEquipmentState::IsStructurallyValid() const
{
	const int32 SlotCount = static_cast<int32>(ESPEquipmentSlot::MAX);
	if (Revision <= 0 || Slots.Num() != SlotCount)
	{
		return false;
	}

	TSet<FGuid> SeenInstances;
	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		const FSPEquipmentSlotState& SlotState = Slots[SlotIndex];
		if (SlotState.Slot != static_cast<ESPEquipmentSlot>(SlotIndex)
			|| !SlotState.IsStructurallyValid())
		{
			return false;
		}

		if (SlotState.bOccupied
			&& SeenInstances.Contains(SlotState.Item.InstanceId))
		{
			return false;
		}
		if (SlotState.bOccupied)
		{
			SeenInstances.Add(SlotState.Item.InstanceId);
		}
	}
	return true;
}

const FSPEquipmentSlotState* FSPEquipmentState::GetSlot(
	const ESPEquipmentSlot Slot) const
{
	if (!SPIsKnownEquipmentSlot(Slot))
	{
		return nullptr;
	}

	const int32 SlotIndex = static_cast<int32>(Slot);
	return Slots.IsValidIndex(SlotIndex) ? &Slots[SlotIndex] : nullptr;
}

const FSPEquipmentSlotState* FSPEquipmentState::FindSlotForInstance(
	const FGuid& InstanceId) const
{
	if (!InstanceId.IsValid())
	{
		return nullptr;
	}

	return Slots.FindByPredicate(
		[&InstanceId](const FSPEquipmentSlotState& SlotState)
		{
			return SlotState.bOccupied && SlotState.Item.InstanceId == InstanceId;
		});
}

FSPEquipmentMutationOutcome FSPEquipmentState::AuthorityEquip(
	const FSPEquipmentMutationRequest& Request,
	const FSPItemInstance& AuthoritativeInventoryItem,
	const FSPEquipmentRuntimeSpec& ServerSpec)
{
	const FString ServerContext = FString::Printf(
		TEXT("%s|%s|%d|%d|%d|%d|%d"),
		*ServerSpec.DefinitionId.ToString(),
		*AuthoritativeInventoryItem.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower),
		static_cast<int32>(ServerSpec.Slot),
		ServerSpec.MaximumDurability,
		static_cast<int32>(AuthoritativeInventoryItem.Kind),
		AuthoritativeInventoryItem.Quantity,
		static_cast<int32>(AuthoritativeInventoryItem.EquipmentCondition));
	const FString Fingerprint = SPBuildRequestFingerprint(
		TEXT("Equip"),
		Request,
		INDEX_NONE,
		ServerContext);

	FSPEquipmentMutationOutcome Outcome;
	switch (CheckReplay(Request, Fingerprint, Outcome))
	{
	case EReplayCheck::ExactReplay:
	case EReplayCheck::Conflict:
		return Outcome;
	default:
		break;
	}

	auto Finish =
		[this, &Request, &Fingerprint](FSPEquipmentMutationOutcome Result)
		{
			RecordOutcome(Request, Fingerprint, Result);
			return Result;
		};

	if (!Request.IsValid() || Request.Operation != ESPEquipmentOperation::Equip)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::InvalidRequest, Request, false));
	}
	if (!IsStructurallyValid())
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::InvalidState, Request, false));
	}
	if (Request.ExpectedRevision != Revision)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::StaleRevision, Request, false));
	}
	if (!AuthoritativeInventoryItem.IsValid()
		|| AuthoritativeInventoryItem.Kind != ESPItemKind::Equipment
		|| AuthoritativeInventoryItem.Quantity != 1
		|| !ServerSpec.IsValid())
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::InvalidItem, Request, false));
	}
	if (!Request.Item.Matches(AuthoritativeInventoryItem)
		|| !ServerSpec.Matches(AuthoritativeInventoryItem)
		|| Request.Slot != ServerSpec.Slot)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::ItemReferenceMismatch, Request, false));
	}
	if (FindSlotForInstance(Request.Item.InstanceId))
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::InstanceAlreadyEquipped, Request, false));
	}

	FSPEquipmentSlotState* SlotState = GetMutableSlot(Request.Slot);
	if (!SlotState)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::InvalidSlot, Request, false));
	}
	if (SlotState->bOccupied)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::SlotOccupied, Request, false));
	}

	const int32 InitialDurability = SPInitialDurability(
		AuthoritativeInventoryItem.EquipmentCondition,
		ServerSpec.MaximumDurability);
	if (InitialDurability == INDEX_NONE)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::InvalidItem, Request, false));
	}

	SlotState->bOccupied = true;
	SlotState->Item = Request.Item;
	SlotState->CurrentDurability = InitialDurability;
	SlotState->MaximumDurability = ServerSpec.MaximumDurability;
	AdvanceRevision();
	return Finish(MakeOutcome(
		ESPEquipmentMutationResult::Success, Request, true));
}

FSPEquipmentMutationOutcome FSPEquipmentState::AuthorityUnequip(
	const FSPEquipmentMutationRequest& Request)
{
	const FString Fingerprint = SPBuildRequestFingerprint(TEXT("Unequip"), Request);
	FSPEquipmentMutationOutcome Outcome;
	switch (CheckReplay(Request, Fingerprint, Outcome))
	{
	case EReplayCheck::ExactReplay:
	case EReplayCheck::Conflict:
		return Outcome;
	default:
		break;
	}

	auto Finish =
		[this, &Request, &Fingerprint](FSPEquipmentMutationOutcome Result)
		{
			RecordOutcome(Request, Fingerprint, Result);
			return Result;
		};

	if (!Request.IsValid() || Request.Operation != ESPEquipmentOperation::Unequip)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::InvalidRequest, Request, false));
	}
	if (!IsStructurallyValid())
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::InvalidState, Request, false));
	}
	if (Request.ExpectedRevision != Revision)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::StaleRevision, Request, false));
	}

	FSPEquipmentSlotState* SlotState = GetMutableSlot(Request.Slot);
	if (!SlotState)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::InvalidSlot, Request, false));
	}
	if (!SlotState->bOccupied)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::NotEquipped, Request, false));
	}
	if (!(SlotState->Item == Request.Item))
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::ItemReferenceMismatch, Request, false));
	}

	Outcome = MakeOutcome(ESPEquipmentMutationResult::Success, Request, true);
	Outcome.CurrentDurability = SlotState->CurrentDurability;
	Outcome.MaximumDurability = SlotState->MaximumDurability;
	Outcome.Condition = SlotState->GetCondition();
	SlotState->Reset(Request.Slot);
	AdvanceRevision();
	Outcome.AuthoritativeRevision = Revision;
	return Finish(Outcome);
}

FSPEquipmentMutationOutcome FSPEquipmentState::AuthorityApplyDamage(
	const FSPEquipmentMutationRequest& Request,
	const int32 ServerResolvedDamage)
{
	const FString Fingerprint = SPBuildRequestFingerprint(
		TEXT("Damage"), Request, ServerResolvedDamage);
	FSPEquipmentMutationOutcome Outcome;
	switch (CheckReplay(Request, Fingerprint, Outcome))
	{
	case EReplayCheck::ExactReplay:
	case EReplayCheck::Conflict:
		return Outcome;
	default:
		break;
	}

	auto Finish =
		[this, &Request, &Fingerprint](FSPEquipmentMutationOutcome Result)
		{
			RecordOutcome(Request, Fingerprint, Result);
			return Result;
		};

	if (!Request.IsValid()
		|| Request.Operation != ESPEquipmentOperation::Damage
		|| ServerResolvedDamage <= 0)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::InvalidRequest, Request, false));
	}
	if (!IsStructurallyValid())
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::InvalidState, Request, false));
	}
	if (Request.ExpectedRevision != Revision)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::StaleRevision, Request, false));
	}

	FSPEquipmentSlotState* SlotState = GetMutableSlot(Request.Slot);
	if (!SlotState || !SlotState->bOccupied)
	{
		return Finish(MakeOutcome(
			SlotState
				? ESPEquipmentMutationResult::NotEquipped
				: ESPEquipmentMutationResult::InvalidSlot,
			Request,
			false));
	}
	if (!(SlotState->Item == Request.Item))
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::ItemReferenceMismatch, Request, false));
	}
	if (SlotState->CurrentDurability == 0)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::AlreadyBroken, Request, false));
	}

	SlotState->CurrentDurability = FMath::Max(
		0,
		SlotState->CurrentDurability - FMath::Min(
			SlotState->CurrentDurability,
			ServerResolvedDamage));
	AdvanceRevision();
	return Finish(MakeOutcome(
		ESPEquipmentMutationResult::Success, Request, true));
}

FSPEquipmentMutationOutcome FSPEquipmentState::AuthorityApplyRepair(
	const FSPEquipmentMutationRequest& Request,
	const int32 ServerResolvedRepair)
{
	const FString Fingerprint = SPBuildRequestFingerprint(
		TEXT("Repair"), Request, ServerResolvedRepair);
	FSPEquipmentMutationOutcome Outcome;
	switch (CheckReplay(Request, Fingerprint, Outcome))
	{
	case EReplayCheck::ExactReplay:
	case EReplayCheck::Conflict:
		return Outcome;
	default:
		break;
	}

	auto Finish =
		[this, &Request, &Fingerprint](FSPEquipmentMutationOutcome Result)
		{
			RecordOutcome(Request, Fingerprint, Result);
			return Result;
		};

	if (!Request.IsValid()
		|| Request.Operation != ESPEquipmentOperation::Repair
		|| ServerResolvedRepair <= 0)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::InvalidRequest, Request, false));
	}
	if (!IsStructurallyValid())
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::InvalidState, Request, false));
	}
	if (Request.ExpectedRevision != Revision)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::StaleRevision, Request, false));
	}

	FSPEquipmentSlotState* SlotState = GetMutableSlot(Request.Slot);
	if (!SlotState || !SlotState->bOccupied)
	{
		return Finish(MakeOutcome(
			SlotState
				? ESPEquipmentMutationResult::NotEquipped
				: ESPEquipmentMutationResult::InvalidSlot,
			Request,
			false));
	}
	if (!(SlotState->Item == Request.Item))
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::ItemReferenceMismatch, Request, false));
	}
	if (SlotState->CurrentDurability >= SlotState->MaximumDurability)
	{
		return Finish(MakeOutcome(
			ESPEquipmentMutationResult::AlreadyFullyRepaired, Request, false));
	}

	const int32 MissingDurability =
		SlotState->MaximumDurability - SlotState->CurrentDurability;
	SlotState->CurrentDurability += FMath::Min(
		MissingDurability,
		ServerResolvedRepair);
	AdvanceRevision();
	return Finish(MakeOutcome(
		ESPEquipmentMutationResult::Success, Request, true));
}

void FSPEquipmentState::ResetReplayLedger()
{
	ProcessedRequests.Reset();
	RequestInsertionOrder.Reset();
}

FSPEquipmentSlotState* FSPEquipmentState::GetMutableSlot(
	const ESPEquipmentSlot Slot)
{
	if (!SPIsKnownEquipmentSlot(Slot))
	{
		return nullptr;
	}

	const int32 SlotIndex = static_cast<int32>(Slot);
	return Slots.IsValidIndex(SlotIndex) ? &Slots[SlotIndex] : nullptr;
}

FSPEquipmentMutationOutcome FSPEquipmentState::MakeOutcome(
	const ESPEquipmentMutationResult Result,
	const FSPEquipmentMutationRequest& Request,
	const bool bStateChanged) const
{
	FSPEquipmentMutationOutcome Outcome;
	Outcome.Result = Result;
	Outcome.AuthoritativeRevision = Revision;
	Outcome.bStateChanged = bStateChanged;
	Outcome.Slot = Request.Slot;
	Outcome.Item = Request.Item;

	if (const FSPEquipmentSlotState* SlotState = GetSlot(Request.Slot);
		SlotState && SlotState->bOccupied && SlotState->Item == Request.Item)
	{
		Outcome.CurrentDurability = SlotState->CurrentDurability;
		Outcome.MaximumDurability = SlotState->MaximumDurability;
		Outcome.Condition = SlotState->GetCondition();
	}
	return Outcome;
}

FSPEquipmentState::EReplayCheck FSPEquipmentState::CheckReplay(
	const FSPEquipmentMutationRequest& Request,
	const FString& Fingerprint,
	FSPEquipmentMutationOutcome& OutOutcome) const
{
	if (Request.RequestId <= 0)
	{
		return EReplayCheck::NewRequest;
	}

	const FSPEquipmentProcessedRequest* Existing =
		ProcessedRequests.Find(Request.RequestId);
	if (!Existing)
	{
		return EReplayCheck::NewRequest;
	}

	if (Existing->Fingerprint != Fingerprint)
	{
		OutOutcome = MakeOutcome(
			ESPEquipmentMutationResult::RequestConflict,
			Request,
			false);
		return EReplayCheck::Conflict;
	}

	OutOutcome = Existing->Outcome;
	OutOutcome.bReplay = true;
	OutOutcome.bStateChanged = false;
	return EReplayCheck::ExactReplay;
}

void FSPEquipmentState::RecordOutcome(
	const FSPEquipmentMutationRequest& Request,
	const FString& Fingerprint,
	const FSPEquipmentMutationOutcome& Outcome)
{
	if (Request.RequestId <= 0 || ProcessedRequests.Contains(Request.RequestId))
	{
		return;
	}

	FSPEquipmentProcessedRequest& Record =
		ProcessedRequests.Add(Request.RequestId);
	Record.Fingerprint = Fingerprint;
	Record.Outcome = Outcome;
	RequestInsertionOrder.Add(Request.RequestId);

	while (RequestInsertionOrder.Num() > ReplayCapacity)
	{
		const int64 OldestRequestId = RequestInsertionOrder[0];
		RequestInsertionOrder.RemoveAt(0, 1, EAllowShrinking::No);
		ProcessedRequests.Remove(OldestRequestId);
	}
}

void FSPEquipmentState::AdvanceRevision()
{
	Revision = Revision >= MAX_int32 ? 1 : Revision + 1;
}
