#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/SPEquipmentTypes.h"

namespace
{
FPrimaryAssetId MakeEquipmentDefinitionId(const TCHAR* Name)
{
	return FPrimaryAssetId(FPrimaryAssetType(TEXT("SPItem")), Name);
}

FSPItemInstance MakeEquipmentItem(
	const TCHAR* Name,
	const ESPEquipmentCondition Condition = ESPEquipmentCondition::Good)
{
	FSPItemInstance Item;
	Item.InstanceId = FGuid::NewGuid();
	Item.DefinitionId = MakeEquipmentDefinitionId(Name);
	Item.Kind = ESPItemKind::Equipment;
	Item.Quantity = 1;
	Item.EquipmentCondition = Condition;
	return Item;
}

FSPEquipmentRuntimeSpec MakeSpec(
	const FSPItemInstance& Item,
	const ESPEquipmentSlot Slot,
	const int32 MaximumDurability)
{
	FSPEquipmentRuntimeSpec Spec;
	Spec.DefinitionId = Item.DefinitionId;
	Spec.Slot = Slot;
	Spec.MaximumDurability = MaximumDurability;
	return Spec;
}

FSPEquipmentMutationRequest MakeRequest(
	const int64 RequestId,
	const int32 Revision,
	const ESPEquipmentOperation Operation,
	const ESPEquipmentSlot Slot,
	const FSPItemInstance& Item)
{
	FSPEquipmentMutationRequest Request;
	Request.RequestId = RequestId;
	Request.ExpectedRevision = Revision;
	Request.Operation = Operation;
	Request.Slot = Slot;
	Request.Item.DefinitionId = Item.DefinitionId;
	Request.Item.InstanceId = Item.InstanceId;
	return Request;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPEquipmentSlotAndReferenceTest,
	"ScrollPeddler.Equipment.Runtime.SlotAndInventoryReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPEquipmentSlotAndReferenceTest::RunTest(const FString& Parameters)
{
	FSPEquipmentState State;
	TestTrue(TEXT("Fresh equipment state is structurally valid"),
		State.IsStructurallyValid());
	TestEqual(TEXT("Vertical slice exposes three equipment slots"),
		State.Slots.Num(), 3);

	const FSPItemInstance Lantern = MakeEquipmentItem(TEXT("Lantern"));
	const FSPEquipmentRuntimeSpec Spec =
		MakeSpec(Lantern, ESPEquipmentSlot::Tool, 120);
	const FSPEquipmentMutationRequest EquipRequest = MakeRequest(
		1, State.Revision, ESPEquipmentOperation::Equip,
		ESPEquipmentSlot::Tool, Lantern);

	const FSPEquipmentMutationOutcome Outcome =
		State.AuthorityEquip(EquipRequest, Lantern, Spec);
	TestEqual(TEXT("Authoritative item equips"), Outcome.Result,
		ESPEquipmentMutationResult::Success);
	TestTrue(TEXT("Equip mutates state"), Outcome.bStateChanged);
	TestEqual(TEXT("Equip advances one revision"), State.Revision, 2);

	const FSPEquipmentSlotState* ToolSlot =
		State.GetSlot(ESPEquipmentSlot::Tool);
	TestNotNull(TEXT("Tool slot remains addressable"), ToolSlot);
	if (!ToolSlot)
	{
		return false;
	}

	TestEqual(TEXT("Slot preserves stable definition identity"),
		ToolSlot->Item.DefinitionId, Lantern.DefinitionId);
	TestEqual(TEXT("Slot preserves exact instance identity"),
		ToolSlot->Item.InstanceId, Lantern.InstanceId);
	TestEqual(TEXT("Good equipment starts at server maximum"),
		ToolSlot->CurrentDurability, 120);
	TestEqual(TEXT("Full durability reports good condition"),
		ToolSlot->GetCondition(), ESPEquipmentCondition::Good);

	FSPItemInstance MatchingSnapshot = Lantern;
	MatchingSnapshot.EquipmentCondition = ESPEquipmentCondition::Broken;
	TestTrue(TEXT("Condition projects onto only a matching inventory snapshot"),
		ToolSlot->TryApplyConditionToItemSnapshot(MatchingSnapshot));
	TestEqual(TEXT("Projected inventory condition is authoritative"),
		MatchingSnapshot.EquipmentCondition, ESPEquipmentCondition::Good);

	FSPItemInstance DifferentInstance = Lantern;
	DifferentInstance.InstanceId = FGuid::NewGuid();
	TestFalse(TEXT("A different instance cannot receive equipment state"),
		ToolSlot->TryApplyConditionToItemSnapshot(DifferentInstance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPEquipmentRevisionReplayTest,
	"ScrollPeddler.Equipment.Runtime.RevisionAndRequestIdempotency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPEquipmentRevisionReplayTest::RunTest(const FString& Parameters)
{
	FSPEquipmentState State;
	const FSPItemInstance Ward = MakeEquipmentItem(TEXT("ArchiveWard"));
	const FSPEquipmentRuntimeSpec Spec =
		MakeSpec(Ward, ESPEquipmentSlot::Protection, 80);
	const FSPEquipmentMutationRequest StaleRequest = MakeRequest(
		10, State.Revision + 1, ESPEquipmentOperation::Equip,
		ESPEquipmentSlot::Protection, Ward);

	FSPEquipmentMutationOutcome Outcome =
		State.AuthorityEquip(StaleRequest, Ward, Spec);
	TestEqual(TEXT("Forged future revision is rejected"),
		Outcome.Result, ESPEquipmentMutationResult::StaleRevision);
	TestEqual(TEXT("Stale request cannot advance revision"), State.Revision, 1);

	Outcome = State.AuthorityEquip(StaleRequest, Ward, Spec);
	TestEqual(TEXT("Exact failed request replay preserves result"),
		Outcome.Result, ESPEquipmentMutationResult::StaleRevision);
	TestTrue(TEXT("Failed request replay is identified"), Outcome.bReplay);

	FSPEquipmentMutationRequest ChangedReplay = StaleRequest;
	ChangedReplay.ExpectedRevision = State.Revision;
	Outcome = State.AuthorityEquip(ChangedReplay, Ward, Spec);
	TestEqual(TEXT("Same request id with changed payload conflicts"),
		Outcome.Result, ESPEquipmentMutationResult::RequestConflict);
	TestEqual(TEXT("Conflict cannot mutate state"), State.Revision, 1);

	const FSPEquipmentMutationRequest EquipRequest = MakeRequest(
		11, State.Revision, ESPEquipmentOperation::Equip,
		ESPEquipmentSlot::Protection, Ward);
	const FSPEquipmentMutationOutcome FirstSuccess =
		State.AuthorityEquip(EquipRequest, Ward, Spec);
	TestEqual(TEXT("Fresh request equips"), FirstSuccess.Result,
		ESPEquipmentMutationResult::Success);
	TestEqual(TEXT("Successful equip advances revision once"), State.Revision, 2);

	const FSPEquipmentMutationOutcome ReplayedSuccess =
		State.AuthorityEquip(EquipRequest, Ward, Spec);
	TestEqual(TEXT("Exact success replay stays successful"),
		ReplayedSuccess.Result, ESPEquipmentMutationResult::Success);
	TestTrue(TEXT("Success replay is identified"), ReplayedSuccess.bReplay);
	TestFalse(TEXT("Replay reports no new mutation"),
		ReplayedSuccess.bStateChanged);
	TestEqual(TEXT("Success replay cannot advance revision twice"),
		State.Revision, 2);

	const FSPEquipmentRuntimeSpec ChangedServerSpec =
		MakeSpec(Ward, ESPEquipmentSlot::Protection, 999);
	Outcome = State.AuthorityEquip(EquipRequest, Ward, ChangedServerSpec);
	TestEqual(TEXT("Same request id cannot change trusted server parameters"),
		Outcome.Result, ESPEquipmentMutationResult::RequestConflict);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPEquipmentDamageRepairTest,
	"ScrollPeddler.Equipment.Runtime.DurabilityBreakAndRepair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPEquipmentDamageRepairTest::RunTest(const FString& Parameters)
{
	FSPEquipmentState State;
	const FSPItemInstance Meter = MakeEquipmentItem(TEXT("NoiseMeter"));
	const FSPEquipmentRuntimeSpec Spec =
		MakeSpec(Meter, ESPEquipmentSlot::Utility, 50);
	TestTrue(TEXT("Utility equips"), State.AuthorityEquip(
		MakeRequest(20, State.Revision, ESPEquipmentOperation::Equip,
			ESPEquipmentSlot::Utility, Meter),
		Meter,
		Spec).IsSuccess());

	const FSPEquipmentMutationRequest DamageRequest = MakeRequest(
		21, State.Revision, ESPEquipmentOperation::Damage,
		ESPEquipmentSlot::Utility, Meter);
	FSPEquipmentMutationOutcome Outcome =
		State.AuthorityApplyDamage(DamageRequest, 500);
	TestEqual(TEXT("Overkill damage safely clamps to zero"),
		Outcome.CurrentDurability, 0);
	TestEqual(TEXT("Zero durability is broken"),
		Outcome.Condition, ESPEquipmentCondition::Broken);
	TestEqual(TEXT("Damage advances revision exactly once"), State.Revision, 3);

	Outcome = State.AuthorityApplyDamage(DamageRequest, 500);
	TestTrue(TEXT("Damage retry is recognized"), Outcome.bReplay);
	TestEqual(TEXT("Damage retry cannot advance revision"), State.Revision, 3);

	const FSPEquipmentMutationRequest ChangedDamageReplay = MakeRequest(
		21, DamageRequest.ExpectedRevision, ESPEquipmentOperation::Damage,
		ESPEquipmentSlot::Utility, Meter);
	Outcome = State.AuthorityApplyDamage(ChangedDamageReplay, 1);
	TestEqual(TEXT("Same damage id with a changed server amount conflicts"),
		Outcome.Result, ESPEquipmentMutationResult::RequestConflict);

	const FSPEquipmentMutationRequest RepairRequest = MakeRequest(
		22, State.Revision, ESPEquipmentOperation::Repair,
		ESPEquipmentSlot::Utility, Meter);
	Outcome = State.AuthorityApplyRepair(RepairRequest, 15);
	TestEqual(TEXT("Broken equipment can be partially repaired"),
		Outcome.CurrentDurability, 15);
	TestEqual(TEXT("Partial repair reports worn"),
		Outcome.Condition, ESPEquipmentCondition::Worn);

	Outcome = State.AuthorityApplyRepair(
		MakeRequest(23, State.Revision, ESPEquipmentOperation::Repair,
			ESPEquipmentSlot::Utility, Meter),
		1000);
	TestEqual(TEXT("Repair safely clamps at maximum"),
		Outcome.CurrentDurability, 50);
	TestEqual(TEXT("Maximum durability restores good condition"),
		Outcome.Condition, ESPEquipmentCondition::Good);

	const int32 RevisionBeforeNoOp = State.Revision;
	Outcome = State.AuthorityApplyRepair(
		MakeRequest(24, State.Revision, ESPEquipmentOperation::Repair,
			ESPEquipmentSlot::Utility, Meter),
		1);
	TestEqual(TEXT("Fully repaired equipment rejects no-op repair"),
		Outcome.Result, ESPEquipmentMutationResult::AlreadyFullyRepaired);
	TestEqual(TEXT("No-op repair does not advance revision"),
		State.Revision, RevisionBeforeNoOp);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPEquipmentForgeryAndUnequipTest,
	"ScrollPeddler.Equipment.Runtime.ReferenceForgeryAndUnequip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPEquipmentForgeryAndUnequipTest::RunTest(const FString& Parameters)
{
	FSPEquipmentState State;
	const FSPItemInstance Lantern = MakeEquipmentItem(TEXT("Lantern"));
	const FSPEquipmentRuntimeSpec ToolSpec =
		MakeSpec(Lantern, ESPEquipmentSlot::Tool, 100);

	FSPEquipmentMutationRequest ForgedInstance = MakeRequest(
		30, State.Revision, ESPEquipmentOperation::Equip,
		ESPEquipmentSlot::Tool, Lantern);
	ForgedInstance.Item.InstanceId = FGuid::NewGuid();
	FSPEquipmentMutationOutcome Outcome =
		State.AuthorityEquip(ForgedInstance, Lantern, ToolSpec);
	TestEqual(TEXT("Forged instance identity is rejected"),
		Outcome.Result, ESPEquipmentMutationResult::ItemReferenceMismatch);

	FSPEquipmentMutationRequest WrongSlot = MakeRequest(
		31, State.Revision, ESPEquipmentOperation::Equip,
		ESPEquipmentSlot::Protection, Lantern);
	Outcome = State.AuthorityEquip(WrongSlot, Lantern, ToolSpec);
	TestEqual(TEXT("Client cannot forge definition slot compatibility"),
		Outcome.Result, ESPEquipmentMutationResult::ItemReferenceMismatch);

	FSPItemInstance NonEquipment = Lantern;
	NonEquipment.Kind = ESPItemKind::Material;
	Outcome = State.AuthorityEquip(
		MakeRequest(32, State.Revision, ESPEquipmentOperation::Equip,
			ESPEquipmentSlot::Tool, NonEquipment),
		NonEquipment,
		ToolSpec);
	TestEqual(TEXT("Non-equipment inventory item is rejected"),
		Outcome.Result, ESPEquipmentMutationResult::InvalidItem);

	TestTrue(TEXT("Valid equipment still equips after denied requests"),
		State.AuthorityEquip(
			MakeRequest(33, State.Revision, ESPEquipmentOperation::Equip,
				ESPEquipmentSlot::Tool, Lantern),
			Lantern,
			ToolSpec).IsSuccess());

	FSPItemInstance OtherLantern = Lantern;
	OtherLantern.InstanceId = FGuid::NewGuid();
	Outcome = State.AuthorityUnequip(
		MakeRequest(34, State.Revision, ESPEquipmentOperation::Unequip,
			ESPEquipmentSlot::Tool, OtherLantern));
	TestEqual(TEXT("Different instance cannot unequip the occupied slot"),
		Outcome.Result, ESPEquipmentMutationResult::ItemReferenceMismatch);

	const int32 RevisionBeforeUnequip = State.Revision;
	const FSPEquipmentMutationRequest UnequipRequest = MakeRequest(
		35, State.Revision, ESPEquipmentOperation::Unequip,
		ESPEquipmentSlot::Tool, Lantern);
	Outcome = State.AuthorityUnequip(UnequipRequest);
	TestEqual(TEXT("Exact instance unequips"), Outcome.Result,
		ESPEquipmentMutationResult::Success);
	TestEqual(TEXT("Unequip returns final durability"), Outcome.CurrentDurability, 100);
	TestEqual(TEXT("Unequip advances exactly one revision"),
		State.Revision, RevisionBeforeUnequip + 1);
	TestFalse(TEXT("Slot becomes empty"),
		State.GetSlot(ESPEquipmentSlot::Tool)->bOccupied);

	const FSPEquipmentMutationOutcome Replay =
		State.AuthorityUnequip(UnequipRequest);
	TestTrue(TEXT("Unequip retry is idempotent"), Replay.bReplay);
	TestEqual(TEXT("Unequip retry returns original durability"),
		Replay.CurrentDurability, 100);
	TestEqual(TEXT("Unequip retry cannot advance revision"),
		State.Revision, RevisionBeforeUnequip + 1);
	TestTrue(TEXT("Final state remains structurally valid"),
		State.IsStructurallyValid());
	return true;
}

#endif
