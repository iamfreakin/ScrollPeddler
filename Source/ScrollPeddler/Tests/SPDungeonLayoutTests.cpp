#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Data/SPVerticalSliceDefinitions.h"
#include "World/SPDungeonLayoutActor.h"
#include "World/SPDungeonLayoutGenerator.h"

namespace
{
	USPDungeonSeedDefinition* MakeValidSeed(const int32 Seed = 17321)
	{
		USPDungeonSeedDefinition* Definition =
			NewObject<USPDungeonSeedDefinition>();
		Definition->StableId = TEXT("Dungeon.VerticalSlice.Test");
		Definition->Seed = Seed;
		Definition->RoomOrder = {
			ESPRoomRole::EntryRecords,
			ESPRoomRole::StandardStacks,
			ESPRoomRole::Office,
			ESPRoomRole::FloodedArchive,
			ESPRoomRole::Bindery,
			ESPRoomRole::Incinerator,
			ESPRoomRole::EchoHall,
			ESPRoomRole::SealedStorage
		};
		return Definition;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPDungeonLayoutDeterminismTest,
	"ScrollPeddler.Dungeon.Layout.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPDungeonLayoutDeterminismTest::RunTest(
	const FString& Parameters)
{
	const USPDungeonSeedDefinition* Definition = MakeValidSeed();
	FSPDungeonLayout First;
	FSPDungeonLayout Second;
	FSPDungeonLayoutValidationReport FirstReport;
	FSPDungeonLayoutValidationReport SecondReport;

	TestTrue(TEXT("First deterministic generation succeeds"),
		FSPDungeonLayoutGenerator::Generate(
			Definition,
			First,
			FirstReport));
	TestTrue(TEXT("Second deterministic generation succeeds"),
		FSPDungeonLayoutGenerator::Generate(
			Definition,
			Second,
			SecondReport));
	TestTrue(TEXT("Same seed produces the exact same snapshot"),
		First == Second);
	TestEqual(TEXT("Same seed produces the same checksum"),
		First.LayoutChecksum,
		Second.LayoutChecksum);
	USPDungeonSeedDefinition* ReorderedDefinition = MakeValidSeed();
	ReorderedDefinition->RoomOrder.Swap(1, 6);
	FSPDungeonLayout Reordered;
	FSPDungeonLayoutValidationReport ReorderedReport;
	TestTrue(TEXT("Reordered valid role set still generates"),
		FSPDungeonLayoutGenerator::Generate(
			ReorderedDefinition,
			Reordered,
			ReorderedReport));
	TestTrue(TEXT("Seed, not source array order, determines layout"),
		First == Reordered);
	TestEqual(TEXT("Layout contains every room"),
		First.Rooms.Num(),
		FSPDungeonLayoutGenerator::RequiredRoomCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPDungeonLayoutTopologyTest,
	"ScrollPeddler.Dungeon.Layout.TopologyAndMarkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPDungeonLayoutTopologyTest::RunTest(
	const FString& Parameters)
{
	FSPDungeonLayout Layout;
	FSPDungeonLayoutValidationReport Report;
	TestTrue(TEXT("Valid seed generates a layout"),
		FSPDungeonLayoutGenerator::Generate(
			MakeValidSeed(88231),
			Layout,
			Report));
	TestEqual(TEXT("Generated layout validates"),
		Report.Result,
		ESPDungeonLayoutValidationResult::Valid);
	TestEqual(TEXT("EntryRecords is the start room"),
		Layout.Rooms[Layout.EntryRoomIndex].Role,
		ESPRoomRole::EntryRecords);
	TestEqual(TEXT("SealedStorage is the objective room"),
		Layout.Rooms[Layout.ObjectiveRoomIndex].Role,
		ESPRoomRole::SealedStorage);

	const int32 ObjectiveDistance =
		FSPDungeonLayoutGenerator::CalculateGraphDistance(
			Layout,
			Layout.EntryRoomIndex,
			Layout.ObjectiveRoomIndex);
	TestTrue(TEXT("Objective is sufficiently far from the start"),
		ObjectiveDistance
			>= FSPDungeonLayoutGenerator::MinimumObjectiveDistance);

	int32 UndirectedEdges = 0;
	int32 BranchRooms = 0;
	for (const FSPDungeonRoomLayout& Room : Layout.Rooms)
	{
		UndirectedEdges += Room.DoorSockets.Num();
		BranchRooms += Room.bPrimaryPath ? 0 : 1;
		TestTrue(TEXT("Every room has a door socket"),
			!Room.DoorSockets.IsEmpty());
		TestTrue(TEXT("Every room has an item marker"),
			Room.SpawnMarkers.ContainsByPredicate(
				[](const FSPDungeonSpawnMarker& Marker)
				{
					return Marker.Kind
						== ESPDungeonSpawnMarkerKind::Item;
				}));
		TestTrue(TEXT("Every room has a threat marker"),
			Room.SpawnMarkers.ContainsByPredicate(
				[](const FSPDungeonSpawnMarker& Marker)
				{
					return Marker.Kind
						== ESPDungeonSpawnMarkerKind::Threat;
				}));
	}
	TestEqual(TEXT("Tree has exactly seven undirected connections"),
		UndirectedEdges / 2,
		Layout.Rooms.Num() - 1);
	TestEqual(TEXT("Generator creates two branch rooms"),
		BranchRooms,
		2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPDungeonLayoutInputRejectionTest,
	"ScrollPeddler.Dungeon.Layout.RejectsInvalidRoomSets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPDungeonLayoutInputRejectionTest::RunTest(
	const FString& Parameters)
{
	USPDungeonSeedDefinition* DuplicateDefinition = MakeValidSeed();
	DuplicateDefinition->RoomOrder.Last() =
		ESPRoomRole::EntryRecords;
	FSPDungeonLayout Layout;
	FSPDungeonLayoutValidationReport Report;
	TestFalse(TEXT("Duplicate role set is rejected"),
		FSPDungeonLayoutGenerator::Generate(
			DuplicateDefinition,
			Layout,
			Report));
	TestEqual(TEXT("Duplicate role has an explicit result"),
		Report.Result,
		ESPDungeonLayoutValidationResult::DuplicateRoomRole);
	TestTrue(TEXT("Failed generation leaves no partial layout"),
		Layout.IsEmpty());

	USPDungeonSeedDefinition* ShortDefinition = MakeValidSeed();
	ShortDefinition->RoomOrder.Pop();
	TestFalse(TEXT("Incomplete room set is rejected"),
		FSPDungeonLayoutGenerator::Generate(
			ShortDefinition,
			Layout,
			Report));
	TestEqual(TEXT("Incomplete set reports its count"),
		Report.Result,
		ESPDungeonLayoutValidationResult::InvalidRoomCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPDungeonLayoutMutationRejectionTest,
	"ScrollPeddler.Dungeon.Layout.RejectsInvalidTopology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPDungeonLayoutMutationRejectionTest::RunTest(
	const FString& Parameters)
{
	FSPDungeonLayout ValidLayout;
	FSPDungeonLayoutValidationReport Report;
	TestTrue(TEXT("Test fixture generation succeeds"),
		FSPDungeonLayoutGenerator::Generate(
			MakeValidSeed(99217),
			ValidLayout,
			Report));

	FSPDungeonLayout DuplicateRole = ValidLayout;
	DuplicateRole.Rooms[1].Role = DuplicateRole.Rooms[0].Role;
	TestEqual(TEXT("Duplicate generated role is rejected"),
		FSPDungeonLayoutGenerator::ValidateLayout(DuplicateRole).Result,
		ESPDungeonLayoutValidationResult::DuplicateRoomRole);

	FSPDungeonLayout DuplicateCoordinate = ValidLayout;
	DuplicateCoordinate.Rooms[1].GridCoordinate =
		DuplicateCoordinate.Rooms[0].GridCoordinate;
	DuplicateCoordinate.Rooms[1].WorldLocation =
		DuplicateCoordinate.Rooms[0].WorldLocation;
	TestEqual(TEXT("Duplicate grid coordinate is rejected"),
		FSPDungeonLayoutGenerator::ValidateLayout(
			DuplicateCoordinate).Result,
		ESPDungeonLayoutValidationResult::DuplicateCoordinate);

	FSPDungeonLayout Disconnected = ValidLayout;
	const int32 LeafIndex = Disconnected.Rooms.Num() - 1;
	const int32 ParentIndex =
		Disconnected.Rooms[LeafIndex].ParentRoomIndex;
	Disconnected.Rooms[LeafIndex].DoorSockets.Reset();
	Disconnected.Rooms[ParentIndex].DoorSockets.RemoveAll(
		[LeafIndex](const FSPDungeonDoorSocket& Door)
		{
			return Door.ConnectedRoomIndex == LeafIndex;
		});
	TestEqual(TEXT("Disconnected room is rejected"),
		FSPDungeonLayoutGenerator::ValidateLayout(Disconnected).Result,
		ESPDungeonLayoutValidationResult::Disconnected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPDungeonLayoutActorContractTest,
	"ScrollPeddler.Dungeon.Layout.ReplicatedActorContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPDungeonLayoutActorContractTest::RunTest(
	const FString& Parameters)
{
	const ASPDungeonLayoutActor* Actor =
		GetDefault<ASPDungeonLayoutActor>();
	TestTrue(TEXT("Layout actor replicates"), Actor->GetIsReplicated());
	TestFalse(TEXT("Layout actor has no replicated movement"),
		Actor->IsReplicatingMovement());
	TestFalse(TEXT("Spawner must generate the layout on the server"),
		Actor->HasGeneratedLayout());
	return true;
}

#endif
