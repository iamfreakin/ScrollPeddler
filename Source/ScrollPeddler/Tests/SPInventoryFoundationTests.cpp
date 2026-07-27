#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/SPItemTypes.h"

namespace
{
FPrimaryAssetId MakeDefinitionId(const TCHAR* Name)
{
	return FPrimaryAssetId(FPrimaryAssetType(TEXT("SPItem")), Name);
}

FSPItemInstance MakeMaterial(
	const TCHAR* Name,
	const int32 Quantity,
	const ESPMaterialQuality Quality = ESPMaterialQuality::B,
	const ESPContaminationTier Contamination = ESPContaminationTier::Clean)
{
	FSPItemInstance Item;
	Item.InstanceId = FGuid::NewGuid();
	Item.DefinitionId = MakeDefinitionId(Name);
	Item.Kind = ESPItemKind::Material;
	Item.Quantity = Quantity;
	Item.MaterialQuality = Quality;
	Item.MaterialContamination = Contamination;
	return Item;
}

FSPItemInstance MakeEquipment(const TCHAR* Name)
{
	FSPItemInstance Item;
	Item.InstanceId = FGuid::NewGuid();
	Item.DefinitionId = MakeDefinitionId(Name);
	Item.Kind = ESPItemKind::Equipment;
	Item.Quantity = 1;
	return Item;
}

FSPItemInstance MakeCargo(const TCHAR* Name)
{
	FSPItemInstance Item;
	Item.InstanceId = FGuid::NewGuid();
	Item.DefinitionId = MakeDefinitionId(Name);
	Item.Kind = ESPItemKind::LargeCargo;
	Item.Quantity = 1;
	return Item;
}

bool AddWithoutSwap(
	FSPInventoryState& State,
	const FSPItemInstance& Item,
	ESPInventoryMutationResult& OutResult)
{
	FSPItemInstance DisplacedItem;
	return State.TryAddItem(
		Item,
		State.Revision,
		false,
		DisplacedItem,
		OutResult);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPInventoryMaterialStackInvariantTest,
	"ScrollPeddler.Inventory.Foundation.MaterialStackInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPInventoryMaterialStackInvariantTest::RunTest(const FString& Parameters)
{
	FSPInventoryState State;
	ESPInventoryMutationResult Result = ESPInventoryMutationResult::InvalidItem;

	const FSPItemInstance HandEquipment = MakeEquipment(TEXT("Lantern"));
	TestTrue(TEXT("First item enters the empty hand"), AddWithoutSwap(State, HandEquipment, Result));

	const FSPItemInstance FirstPaper = MakeMaterial(TEXT("ArchivePaper"), 6);
	TestTrue(TEXT("First material stack enters bag slot zero"), AddWithoutSwap(State, FirstPaper, Result));
	TestEqual(TEXT("Bag zero keeps six units"), State.BagSlots[0].Item.Quantity, 6);

	const FSPItemInstance MatchingPaper = MakeMaterial(TEXT("ArchivePaper"), 4);
	TestTrue(TEXT("An exactly fitting material merges"), AddWithoutSwap(State, MatchingPaper, Result));
	TestEqual(TEXT("Matching stack stops at ten"), State.BagSlots[0].Item.Quantity, 10);
	TestTrue(TEXT("Merged source identity is retained for retry defense"),
		State.BagSlots[0].Item.MergedInstanceIds.Contains(MatchingPaper.InstanceId));

	const int32 RevisionBeforeDuplicate = State.Revision;
	TestFalse(TEXT("A retried merged source is rejected"),
		AddWithoutSwap(State, MatchingPaper, Result));
	TestEqual(TEXT("Retry is identified as a duplicate"), Result, ESPInventoryMutationResult::DuplicateInstance);
	TestEqual(TEXT("Rejected retry does not advance revision"), State.Revision, RevisionBeforeDuplicate);

	const FSPItemInstance OverflowPaper = MakeMaterial(TEXT("ArchivePaper"), 11);
	const int32 RevisionBeforeInvalid = State.Revision;
	TestFalse(TEXT("A material stack above ten is invalid"),
		AddWithoutSwap(State, OverflowPaper, Result));
	TestEqual(TEXT("Over-limit stack reports invalid item"), Result, ESPInventoryMutationResult::InvalidItem);
	TestEqual(TEXT("Invalid stack does not advance revision"), State.Revision, RevisionBeforeInvalid);

	const FSPItemInstance FullStackOverflow = MakeMaterial(TEXT("ArchivePaper"), 1);
	TestTrue(TEXT("A matching item never overfills a stack and uses the next slot"),
		AddWithoutSwap(State, FullStackOverflow, Result));
	TestEqual(TEXT("Full stack remains capped at ten"), State.BagSlots[0].Item.Quantity, 10);
	TestEqual(TEXT("Overflow source uses deterministic bag slot one"),
		State.BagSlots[1].Item.InstanceId, FullStackOverflow.InstanceId);

	const FSPItemInstance DifferentQualityPaper =
		MakeMaterial(TEXT("ArchivePaper"), 2, ESPMaterialQuality::A);
	TestTrue(TEXT("Different material quality occupies another slot"),
		AddWithoutSwap(State, DifferentQualityPaper, Result));
	TestEqual(TEXT("Different quality uses deterministic bag slot two"),
		State.BagSlots[2].Item.InstanceId, DifferentQualityPaper.InstanceId);

	const FSPItemInstance DifferentContaminationPaper =
		MakeMaterial(TEXT("ArchivePaper"), 2, ESPMaterialQuality::B, ESPContaminationTier::Trace);
	TestTrue(TEXT("Different contamination occupies another slot"),
		AddWithoutSwap(State, DifferentContaminationPaper, Result));
	TestEqual(TEXT("Different contamination uses deterministic bag slot three"),
		State.BagSlots[3].Item.InstanceId, DifferentContaminationPaper.InstanceId);
	TestTrue(TEXT("Resulting inventory remains structurally valid"), State.IsStructurallyValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPInventoryDeterministicRouteTest,
	"ScrollPeddler.Inventory.Foundation.DeterministicPickupRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPInventoryDeterministicRouteTest::RunTest(const FString& Parameters)
{
	FSPInventoryState State;
	const FSPItemInstance First = MakeEquipment(TEXT("Lantern"));
	FSPInventoryRoute Route = State.CalculatePickupRoute(First);
	TestEqual(TEXT("An empty inventory routes to hand"), Route.Kind, ESPInventoryRouteKind::EmptyHand);
	TestEqual(TEXT("Hand route has no bag index"), Route.Destination.BagIndex, INDEX_NONE);

	ESPInventoryMutationResult Result = ESPInventoryMutationResult::InvalidItem;
	TestTrue(TEXT("Hand route commits"), AddWithoutSwap(State, First, Result));

	const FSPItemInstance Second = MakeEquipment(TEXT("NoiseMeter"));
	Route = State.CalculatePickupRoute(Second);
	TestEqual(TEXT("Next unique item routes to the first bag slot"),
		Route.Kind, ESPInventoryRouteKind::EmptyBag);
	TestEqual(TEXT("First empty bag slot is deterministic"), Route.Destination.BagIndex, 0);

	const FSPItemInstance Cargo = MakeCargo(TEXT("ContractRelic"));
	Route = State.CalculatePickupRoute(Cargo, false);
	TestFalse(TEXT("Large cargo cannot route into an empty bag"), Route.IsSuccess());
	TestEqual(TEXT("Large cargo reports its hand restriction"),
		Route.Failure, ESPInventoryMutationResult::SlotRestricted);
	Route = State.CalculatePickupRoute(Cargo, true);
	TestEqual(TEXT("Explicit pickup swap routes large cargo to hand"),
		Route.Kind, ESPInventoryRouteKind::SwapHand);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPInventorySwapAndRevisionTest,
	"ScrollPeddler.Inventory.Foundation.AtomicSwapAndRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPInventorySwapAndRevisionTest::RunTest(const FString& Parameters)
{
	FSPInventoryState State;
	ESPInventoryMutationResult Result = ESPInventoryMutationResult::InvalidItem;
	const FSPItemInstance HandItem = MakeEquipment(TEXT("Lantern"));
	const FSPItemInstance BagItem = MakeEquipment(TEXT("Chalk"));
	TestTrue(TEXT("Hand item commits"), AddWithoutSwap(State, HandItem, Result));
	TestTrue(TEXT("Bag item commits"), AddWithoutSwap(State, BagItem, Result));

	const int32 CurrentRevision = State.Revision;
	TestFalse(TEXT("A stale swap is rejected"),
		State.TrySwapHandWithBag(0, CurrentRevision - 1, Result));
	TestEqual(TEXT("A stale swap reports its cause"), Result, ESPInventoryMutationResult::StaleRevision);
	TestEqual(TEXT("A stale swap does not advance revision"), State.Revision, CurrentRevision);
	TestEqual(TEXT("A stale swap leaves hand unchanged"), State.HandSlot.Item.InstanceId, HandItem.InstanceId);

	TestTrue(TEXT("Current revision swaps atomically"),
		State.TrySwapHandWithBag(0, CurrentRevision, Result));
	TestEqual(TEXT("Bag item moves to hand"), State.HandSlot.Item.InstanceId, BagItem.InstanceId);
	TestEqual(TEXT("Hand item moves to bag"), State.BagSlots[0].Item.InstanceId, HandItem.InstanceId);
	TestEqual(TEXT("One committed swap advances revision once"), State.Revision, CurrentRevision + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPInventoryExactInstanceMutationTest,
	"ScrollPeddler.Inventory.Foundation.ExactInstanceMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPInventoryExactInstanceMutationTest::RunTest(const FString& Parameters)
{
	FSPInventoryState State;
	ESPInventoryMutationResult Result = ESPInventoryMutationResult::InvalidItem;
	const FSPItemInstance First = MakeEquipment(TEXT("Lantern"));
	const FSPItemInstance Second = MakeEquipment(TEXT("NoiseMeter"));
	TestTrue(TEXT("First item commits"), AddWithoutSwap(State, First, Result));
	TestTrue(TEXT("Second item commits"), AddWithoutSwap(State, Second, Result));

	const int32 RevisionBeforeDuplicate = State.Revision;
	TestFalse(TEXT("Duplicate exact instance is rejected"), AddWithoutSwap(State, First, Result));
	TestEqual(TEXT("Duplicate result is explicit"), Result, ESPInventoryMutationResult::DuplicateInstance);
	TestEqual(TEXT("Duplicate does not advance revision"), State.Revision, RevisionBeforeDuplicate);

	FSPItemInstance RemovedItem;
	TestTrue(TEXT("Exact bag instance removes"),
		State.TryRemoveItemByInstanceId(Second.InstanceId, State.Revision, RemovedItem, Result));
	TestEqual(TEXT("Removed snapshot is exact"), RemovedItem.InstanceId, Second.InstanceId);
	TestFalse(TEXT("Only the targeted bag slot is now empty"), State.BagSlots[0].bOccupied);
	TestEqual(TEXT("Unrelated hand item remains"), State.HandSlot.Item.InstanceId, First.InstanceId);

	const int32 RevisionBeforeMissing = State.Revision;
	TestFalse(TEXT("Unknown instance cannot remove"),
		State.TryRemoveItemByInstanceId(FGuid::NewGuid(), State.Revision, RemovedItem, Result));
	TestEqual(TEXT("Unknown instance reports not found"), Result, ESPInventoryMutationResult::NotFound);
	TestEqual(TEXT("Missing removal does not advance revision"), State.Revision, RevisionBeforeMissing);
	return true;
}

#endif
