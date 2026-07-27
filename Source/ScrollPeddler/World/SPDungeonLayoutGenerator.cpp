#include "World/SPDungeonLayoutGenerator.h"

#include "Data/SPVerticalSliceDefinitions.h"

namespace
{
	constexpr float DoorSocketDistance = 1000.0f;
	constexpr int32 RoomRoleCount = 8;

	class FSPStableRandom
	{
	public:
		explicit FSPStableRandom(const int32 Seed)
			: State(static_cast<uint32>(Seed) ^ 0xA511E9B3u)
		{
			if (State == 0)
			{
				State = 0x6D2B79F5u;
			}
		}

		uint32 Next()
		{
			State = State * 747796405u + 2891336453u;
			const uint32 Shift = (State >> 28u) + 4u;
			uint32 Word = ((State >> Shift) ^ State) * 277803737u;
			Word = (Word >> 22u) ^ Word;
			return Word;
		}

		int32 NextIndex(const int32 Count)
		{
			check(Count > 0);
			return static_cast<int32>(Next() % static_cast<uint32>(Count));
		}

	private:
		uint32 State;
	};

	template<typename ElementType>
	void StableShuffle(TArray<ElementType>& Values, FSPStableRandom& Random)
	{
		for (int32 Index = Values.Num() - 1; Index > 0; --Index)
		{
			Values.Swap(Index, Random.NextIndex(Index + 1));
		}
	}

	FSPDungeonLayoutValidationReport MakeReport(
		const ESPDungeonLayoutValidationResult Result,
		const TCHAR* Message)
	{
		FSPDungeonLayoutValidationReport Report;
		Report.Result = Result;
		Report.Message = FText::FromString(Message);
		return Report;
	}

	bool IsValidRole(const ESPRoomRole Role)
	{
		const uint8 Value = static_cast<uint8>(Role);
		return Value < RoomRoleCount;
	}

	bool IsValidDirection(const ESPDungeonCardinalDirection Direction)
	{
		return static_cast<uint8>(Direction)
			<= static_cast<uint8>(ESPDungeonCardinalDirection::West);
	}

	bool IsValidMarkerKind(const ESPDungeonSpawnMarkerKind Kind)
	{
		return static_cast<uint8>(Kind)
			<= static_cast<uint8>(
				ESPDungeonSpawnMarkerKind::ContractObjective);
	}

	ESPDungeonCardinalDirection RotateDirection(
		const ESPDungeonCardinalDirection Direction,
		const int32 QuarterTurnsClockwise)
	{
		const int32 Value =
			(static_cast<int32>(Direction) + QuarterTurnsClockwise + 4) % 4;
		return static_cast<ESPDungeonCardinalDirection>(Value);
	}

	FName DirectionSocketId(
		const ESPDungeonCardinalDirection Direction)
	{
		switch (Direction)
		{
		case ESPDungeonCardinalDirection::North:
			return TEXT("Door_North");
		case ESPDungeonCardinalDirection::East:
			return TEXT("Door_East");
		case ESPDungeonCardinalDirection::South:
			return TEXT("Door_South");
		case ESPDungeonCardinalDirection::West:
		default:
			return TEXT("Door_West");
		}
	}

	float DirectionYaw(const ESPDungeonCardinalDirection Direction)
	{
		switch (Direction)
		{
		case ESPDungeonCardinalDirection::North:
			return 90.0f;
		case ESPDungeonCardinalDirection::East:
			return 0.0f;
		case ESPDungeonCardinalDirection::South:
			return -90.0f;
		case ESPDungeonCardinalDirection::West:
		default:
			return 180.0f;
		}
	}

	FSPDungeonDoorSocket MakeDoor(
		const int32 ConnectedRoomIndex,
		const ESPDungeonCardinalDirection Direction,
		const bool bPrimaryPath)
	{
		const FIntPoint Step = SPDungeonDirectionToGridStep(Direction);
		FSPDungeonDoorSocket Door;
		Door.SocketId = DirectionSocketId(Direction);
		Door.ConnectedRoomIndex = ConnectedRoomIndex;
		Door.Direction = Direction;
		Door.LocalPosition = FVector(
			static_cast<float>(Step.X) * DoorSocketDistance,
			static_cast<float>(Step.Y) * DoorSocketDistance,
			0.0f);
		Door.LocalRotation = FRotator(
			0.0f,
			DirectionYaw(Direction),
			0.0f);
		Door.bPrimaryPath = bPrimaryPath;
		return Door;
	}

	void ConnectRooms(
		FSPDungeonLayout& Layout,
		const int32 FirstIndex,
		const int32 SecondIndex,
		const ESPDungeonCardinalDirection FirstToSecond,
		const bool bPrimaryPath)
	{
		Layout.Rooms[FirstIndex].DoorSockets.Add(
			MakeDoor(SecondIndex, FirstToSecond, bPrimaryPath));
		Layout.Rooms[SecondIndex].DoorSockets.Add(
			MakeDoor(
				FirstIndex,
				SPOppositeDungeonDirection(FirstToSecond),
				bPrimaryPath));
	}

	void GetMarkerCounts(
		const ESPRoomRole Role,
		int32& OutItemCount,
		int32& OutThreatCount)
	{
		switch (Role)
		{
		case ESPRoomRole::EntryRecords:
			OutItemCount = 1;
			OutThreatCount = 1;
			break;
		case ESPRoomRole::StandardStacks:
			OutItemCount = 3;
			OutThreatCount = 1;
			break;
		case ESPRoomRole::Office:
			OutItemCount = 2;
			OutThreatCount = 1;
			break;
		case ESPRoomRole::FloodedArchive:
			OutItemCount = 1;
			OutThreatCount = 2;
			break;
		case ESPRoomRole::Bindery:
			OutItemCount = 2;
			OutThreatCount = 1;
			break;
		case ESPRoomRole::Incinerator:
			OutItemCount = 1;
			OutThreatCount = 2;
			break;
		case ESPRoomRole::EchoHall:
			OutItemCount = 1;
			OutThreatCount = 3;
			break;
		case ESPRoomRole::SealedStorage:
		default:
			OutItemCount = 3;
			OutThreatCount = 2;
			break;
		}
	}

	FString MarkerKindLabel(const ESPDungeonSpawnMarkerKind Kind)
	{
		switch (Kind)
		{
		case ESPDungeonSpawnMarkerKind::Item:
			return TEXT("Item");
		case ESPDungeonSpawnMarkerKind::Threat:
			return TEXT("Threat");
		case ESPDungeonSpawnMarkerKind::ContractObjective:
		default:
			return TEXT("Objective");
		}
	}

	FSPDungeonSpawnMarker MakeMarker(
		const int32 RoomIndex,
		const ESPDungeonSpawnMarkerKind Kind,
		const int32 MarkerIndex,
		const FVector& LocalPosition,
		FSPStableRandom& Random)
	{
		FSPDungeonSpawnMarker Marker;
		Marker.MarkerId = FName(*FString::Printf(
			TEXT("Room_%02d_%s_%02d"),
			RoomIndex,
			*MarkerKindLabel(Kind),
			MarkerIndex));
		Marker.Kind = Kind;
		Marker.LocalPosition = LocalPosition;
		Marker.LocalRotation = FRotator(
			0.0f,
			static_cast<float>(Random.NextIndex(4)) * 90.0f,
			0.0f);
		Marker.SpawnWeight = 1.0f;
		return Marker;
	}

	void PopulateMarkers(
		FSPDungeonRoomLayout& Room,
		FSPStableRandom& Random)
	{
		static const TArray<FVector> ItemPositions = {
			FVector(-620.0f, -420.0f, 20.0f),
			FVector(580.0f, -380.0f, 20.0f),
			FVector(-520.0f, 460.0f, 20.0f),
			FVector(560.0f, 440.0f, 20.0f)
		};
		static const TArray<FVector> ThreatPositions = {
			FVector(-760.0f, 120.0f, 90.0f),
			FVector(740.0f, 160.0f, 90.0f),
			FVector(0.0f, -720.0f, 90.0f),
			FVector(0.0f, 700.0f, 90.0f)
		};

		int32 ItemCount = 0;
		int32 ThreatCount = 0;
		GetMarkerCounts(Room.Role, ItemCount, ThreatCount);

		const int32 ItemOffset = Random.NextIndex(ItemPositions.Num());
		for (int32 Index = 0; Index < ItemCount; ++Index)
		{
			Room.SpawnMarkers.Add(MakeMarker(
				Room.RoomIndex,
				ESPDungeonSpawnMarkerKind::Item,
				Index,
				ItemPositions[(ItemOffset + Index) % ItemPositions.Num()],
				Random));
		}

		const int32 ThreatOffset = Random.NextIndex(ThreatPositions.Num());
		for (int32 Index = 0; Index < ThreatCount; ++Index)
		{
			Room.SpawnMarkers.Add(MakeMarker(
				Room.RoomIndex,
				ESPDungeonSpawnMarkerKind::Threat,
				Index,
				ThreatPositions[
					(ThreatOffset + Index) % ThreatPositions.Num()],
				Random));
		}

		if (Room.Role == ESPRoomRole::SealedStorage)
		{
			Room.SpawnMarkers.Add(MakeMarker(
				Room.RoomIndex,
				ESPDungeonSpawnMarkerKind::ContractObjective,
				0,
				FVector(0.0f, 0.0f, 40.0f),
				Random));
		}
	}

	void HashValue(uint32& Hash, const uint32 Value)
	{
		for (int32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
		{
			Hash ^= (Value >> (ByteIndex * 8)) & 0xFFu;
			Hash *= 16777619u;
		}
	}

	void HashName(uint32& Hash, const FName Name)
	{
		const FString Value = Name.ToString();
		const FTCHARToUTF8 Utf8(*Value);
		HashValue(Hash, static_cast<uint32>(Utf8.Length()));
		for (int32 Index = 0; Index < Utf8.Length(); ++Index)
		{
			Hash ^= static_cast<uint8>(Utf8.Get()[Index]);
			Hash *= 16777619u;
		}
	}

	uint32 QuantizeFloat(const float Value)
	{
		return static_cast<uint32>(FMath::RoundToInt(Value * 10.0f));
	}

	bool HasReciprocalDoor(
		const FSPDungeonLayout& Layout,
		const int32 RoomIndex,
		const FSPDungeonDoorSocket& Door)
	{
		if (!Layout.Rooms.IsValidIndex(Door.ConnectedRoomIndex))
		{
			return false;
		}

		const ESPDungeonCardinalDirection Opposite =
			SPOppositeDungeonDirection(Door.Direction);
		return Layout.Rooms[Door.ConnectedRoomIndex].DoorSockets.ContainsByPredicate(
			[RoomIndex, Opposite, &Door](
				const FSPDungeonDoorSocket& Candidate)
			{
				return Candidate.ConnectedRoomIndex == RoomIndex
					&& Candidate.Direction == Opposite
					&& Candidate.bPrimaryPath == Door.bPrimaryPath;
			});
	}
}

bool FSPDungeonLayoutGenerator::Generate(
	const USPDungeonSeedDefinition* Definition,
	FSPDungeonLayout& OutLayout,
	FSPDungeonLayoutValidationReport& OutReport)
{
	OutLayout = FSPDungeonLayout();
	OutReport = ValidateSeedDefinition(Definition);
	if (!OutReport.IsValid())
	{
		return false;
	}

	FSPStableRandom Random(Definition->Seed);
	TArray<ESPRoomRole> FlexibleRoles;
	for (const ESPRoomRole Role : Definition->RoomOrder)
	{
		if (Role != ESPRoomRole::EntryRecords
			&& Role != ESPRoomRole::SealedStorage)
		{
			FlexibleRoles.Add(Role);
		}
	}
	FlexibleRoles.Sort(
		[](const ESPRoomRole First, const ESPRoomRole Second)
		{
			return static_cast<uint8>(First)
				< static_cast<uint8>(Second);
		});
	StableShuffle(FlexibleRoles, Random);

	OutLayout.FormatVersion = FSPDungeonLayout::CurrentFormatVersion;
	OutLayout.SeedStableId = Definition->StableId;
	OutLayout.Seed = Definition->Seed;
	OutLayout.RoomSpacing = DefaultRoomSpacing;
	OutLayout.EntryRoomIndex = 0;
	OutLayout.ObjectiveRoomIndex = PrimaryPathRoomCount - 1;
	OutLayout.Rooms.SetNum(RequiredRoomCount);

	const ESPDungeonCardinalDirection PrimaryDirection =
		static_cast<ESPDungeonCardinalDirection>(Random.NextIndex(4));
	const FIntPoint PrimaryStep =
		SPDungeonDirectionToGridStep(PrimaryDirection);

	TArray<ESPRoomRole> PrimaryRoles;
	PrimaryRoles.Add(ESPRoomRole::EntryRecords);
	for (int32 Index = 0; Index < PrimaryPathRoomCount - 2; ++Index)
	{
		PrimaryRoles.Add(FlexibleRoles[Index]);
	}
	PrimaryRoles.Add(ESPRoomRole::SealedStorage);

	for (int32 Index = 0; Index < PrimaryPathRoomCount; ++Index)
	{
		FSPDungeonRoomLayout& Room = OutLayout.Rooms[Index];
		Room.RoomIndex = Index;
		Room.Role = PrimaryRoles[Index];
		Room.GridCoordinate = PrimaryStep * Index;
		Room.WorldLocation = FVector(
			static_cast<float>(Room.GridCoordinate.X) * OutLayout.RoomSpacing,
			static_cast<float>(Room.GridCoordinate.Y) * OutLayout.RoomSpacing,
			0.0f);
		Room.ParentRoomIndex = Index == 0 ? INDEX_NONE : Index - 1;
		Room.DepthFromEntry = Index;
		Room.bPrimaryPath = true;
		if (Index > 0)
		{
			ConnectRooms(
				OutLayout,
				Index - 1,
				Index,
				PrimaryDirection,
				true);
		}
	}

	TArray<int32> BranchParents = { 1, 2, 3, 4 };
	StableShuffle(BranchParents, Random);
	for (int32 BranchIndex = 0; BranchIndex < 2; ++BranchIndex)
	{
		const int32 RoomIndex = PrimaryPathRoomCount + BranchIndex;
		const int32 ParentIndex = BranchParents[BranchIndex];
		const int32 SideTurns = Random.NextIndex(2) == 0 ? -1 : 1;
		const ESPDungeonCardinalDirection BranchDirection =
			RotateDirection(PrimaryDirection, SideTurns);
		const FIntPoint BranchStep =
			SPDungeonDirectionToGridStep(BranchDirection);

		FSPDungeonRoomLayout& Room = OutLayout.Rooms[RoomIndex];
		Room.RoomIndex = RoomIndex;
		Room.Role =
			FlexibleRoles[PrimaryPathRoomCount - 2 + BranchIndex];
		Room.GridCoordinate =
			OutLayout.Rooms[ParentIndex].GridCoordinate + BranchStep;
		Room.WorldLocation = FVector(
			static_cast<float>(Room.GridCoordinate.X) * OutLayout.RoomSpacing,
			static_cast<float>(Room.GridCoordinate.Y) * OutLayout.RoomSpacing,
			0.0f);
		Room.ParentRoomIndex = ParentIndex;
		Room.DepthFromEntry =
			OutLayout.Rooms[ParentIndex].DepthFromEntry + 1;
		Room.bPrimaryPath = false;
		ConnectRooms(
			OutLayout,
			ParentIndex,
			RoomIndex,
			BranchDirection,
			false);
	}

	for (FSPDungeonRoomLayout& Room : OutLayout.Rooms)
	{
		Room.DoorSockets.Sort(
			[](const FSPDungeonDoorSocket& First,
				const FSPDungeonDoorSocket& Second)
			{
				return static_cast<uint8>(First.Direction)
					< static_cast<uint8>(Second.Direction);
			});
		PopulateMarkers(Room, Random);
	}

	OutLayout.LayoutChecksum = CalculateChecksum(OutLayout);
	OutReport = ValidateLayout(OutLayout);
	if (!OutReport.IsValid())
	{
		OutLayout = FSPDungeonLayout();
		return false;
	}
	return true;
}

FSPDungeonLayoutValidationReport
FSPDungeonLayoutGenerator::ValidateSeedDefinition(
	const USPDungeonSeedDefinition* Definition)
{
	if (!Definition || Definition->StableId.IsNone())
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::MissingStableId,
			TEXT("Dungeon seed definition requires a StableId."));
	}
	if (Definition->Seed == 0)
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::InvalidSeed,
			TEXT("Dungeon seed must be non-zero."));
	}
	if (Definition->RoomOrder.Num() != RequiredRoomCount)
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::InvalidRoomCount,
			TEXT("Dungeon seed must contain exactly eight room roles."));
	}

	bool SeenRoles[RoomRoleCount] = {};
	for (const ESPRoomRole Role : Definition->RoomOrder)
	{
		if (!IsValidRole(Role))
		{
			return MakeReport(
				ESPDungeonLayoutValidationResult::InvalidRoomRole,
				TEXT("Dungeon seed contains an unknown room role."));
		}
		const uint8 RoleIndex = static_cast<uint8>(Role);
		if (SeenRoles[RoleIndex])
		{
			return MakeReport(
				ESPDungeonLayoutValidationResult::DuplicateRoomRole,
				TEXT("Dungeon seed contains a duplicate room role."));
		}
		SeenRoles[RoleIndex] = true;
	}

	return MakeReport(
		ESPDungeonLayoutValidationResult::Valid,
		TEXT("Dungeon seed definition is valid."));
}

FSPDungeonLayoutValidationReport FSPDungeonLayoutGenerator::ValidateLayout(
	const FSPDungeonLayout& Layout,
	const bool bVerifyChecksum)
{
	if (Layout.SeedStableId.IsNone())
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::MissingStableId,
			TEXT("Layout requires the source seed StableId."));
	}
	if (Layout.FormatVersion != FSPDungeonLayout::CurrentFormatVersion)
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::UnsupportedFormat,
			TEXT("Layout format version is not supported."));
	}
	if (Layout.Seed == 0 || Layout.RoomSpacing <= 0.0f)
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::InvalidSeed,
			TEXT("Layout seed and room spacing must be valid."));
	}
	if (Layout.Rooms.Num() != RequiredRoomCount)
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::InvalidRoomCount,
			TEXT("Layout must contain exactly eight rooms."));
	}

	bool SeenRoles[RoomRoleCount] = {};
	TSet<FIntPoint> SeenCoordinates;
	TSet<FName> SeenMarkerIds;
	for (int32 Index = 0; Index < Layout.Rooms.Num(); ++Index)
	{
		const FSPDungeonRoomLayout& Room = Layout.Rooms[Index];
		if (Room.RoomIndex != Index)
		{
			return MakeReport(
				ESPDungeonLayoutValidationResult::InvalidRoomIndex,
				TEXT("Room indexes must be contiguous and canonical."));
		}
		if (!IsValidRole(Room.Role))
		{
			return MakeReport(
				ESPDungeonLayoutValidationResult::InvalidRoomRole,
				TEXT("Layout contains an unknown room role."));
		}
		const uint8 RoleIndex = static_cast<uint8>(Room.Role);
		if (SeenRoles[RoleIndex])
		{
			return MakeReport(
				ESPDungeonLayoutValidationResult::DuplicateRoomRole,
				TEXT("Layout contains a duplicate room role."));
		}
		SeenRoles[RoleIndex] = true;

		if (SeenCoordinates.Contains(Room.GridCoordinate))
		{
			return MakeReport(
				ESPDungeonLayoutValidationResult::DuplicateCoordinate,
				TEXT("Two rooms occupy the same grid coordinate."));
		}
		SeenCoordinates.Add(Room.GridCoordinate);

		const FVector ExpectedWorldLocation(
			static_cast<float>(Room.GridCoordinate.X) * Layout.RoomSpacing,
			static_cast<float>(Room.GridCoordinate.Y) * Layout.RoomSpacing,
			0.0f);
		if (Room.WorldLocation.ContainsNaN()
			|| !Room.WorldLocation.Equals(ExpectedWorldLocation, 0.1f))
		{
			return MakeReport(
				ESPDungeonLayoutValidationResult::InvalidRoomIndex,
				TEXT("Room world location does not match its grid coordinate."));
		}

		bool bHasItemMarker = false;
		bool bHasThreatMarker = false;
		for (const FSPDungeonSpawnMarker& Marker : Room.SpawnMarkers)
		{
			if (Marker.MarkerId.IsNone()
				|| SeenMarkerIds.Contains(Marker.MarkerId)
				|| !IsValidMarkerKind(Marker.Kind)
				|| Marker.LocalPosition.ContainsNaN()
				|| Marker.LocalRotation.ContainsNaN()
				|| Marker.SpawnWeight <= 0.0f)
			{
				return MakeReport(
					ESPDungeonLayoutValidationResult::InvalidMarker,
					TEXT("Layout contains an invalid or duplicate spawn marker."));
			}
			SeenMarkerIds.Add(Marker.MarkerId);
			bHasItemMarker |=
				Marker.Kind == ESPDungeonSpawnMarkerKind::Item;
			bHasThreatMarker |=
				Marker.Kind == ESPDungeonSpawnMarkerKind::Threat;
		}
		if (!bHasItemMarker || !bHasThreatMarker)
		{
			return MakeReport(
				ESPDungeonLayoutValidationResult::InvalidMarker,
				TEXT("Every room requires item and threat spawn markers."));
		}
	}

	if (!Layout.Rooms.IsValidIndex(Layout.EntryRoomIndex)
		|| Layout.EntryRoomIndex != 0
		|| Layout.Rooms[Layout.EntryRoomIndex].Role
			!= ESPRoomRole::EntryRecords
		|| Layout.Rooms[Layout.EntryRoomIndex].GridCoordinate
			!= FIntPoint::ZeroValue
		|| Layout.Rooms[Layout.EntryRoomIndex].ParentRoomIndex != INDEX_NONE
		|| Layout.Rooms[Layout.EntryRoomIndex].DepthFromEntry != 0
		|| !Layout.Rooms[Layout.EntryRoomIndex].bPrimaryPath)
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::InvalidEntryRoom,
			TEXT("EntryRecords must be room zero at the layout origin."));
	}
	if (!Layout.Rooms.IsValidIndex(Layout.ObjectiveRoomIndex)
		|| Layout.Rooms[Layout.ObjectiveRoomIndex].Role
			!= ESPRoomRole::SealedStorage
		|| !Layout.Rooms[Layout.ObjectiveRoomIndex].bPrimaryPath)
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::InvalidObjectiveRoom,
			TEXT("SealedStorage must be the primary-path objective room."));
	}

	int32 UndirectedEdgeCount = 0;
	TArray<TArray<int32>> Adjacency;
	Adjacency.SetNum(Layout.Rooms.Num());
	for (const FSPDungeonRoomLayout& Room : Layout.Rooms)
	{
		if (Room.DoorSockets.Num() > 3)
		{
			return MakeReport(
				ESPDungeonLayoutValidationResult::InvalidDoor,
				TEXT("A path-and-branch room may expose at most three doors."));
		}
		TSet<int32> ConnectedIndexes;
		TSet<uint8> UsedDirections;
		for (const FSPDungeonDoorSocket& Door : Room.DoorSockets)
		{
			if (!Layout.Rooms.IsValidIndex(Door.ConnectedRoomIndex)
				|| Door.ConnectedRoomIndex == Room.RoomIndex
				|| Door.SocketId.IsNone()
				|| !IsValidDirection(Door.Direction)
				|| Door.LocalPosition.ContainsNaN()
				|| Door.LocalRotation.ContainsNaN())
			{
				return MakeReport(
					ESPDungeonLayoutValidationResult::InvalidDoor,
					TEXT("Layout contains an invalid door socket."));
			}
			const uint8 DirectionValue = static_cast<uint8>(Door.Direction);
			const FIntPoint DoorStep =
				SPDungeonDirectionToGridStep(Door.Direction);
			const FVector ExpectedLocalPosition(
				static_cast<float>(DoorStep.X) * DoorSocketDistance,
				static_cast<float>(DoorStep.Y) * DoorSocketDistance,
				0.0f);
			const FRotator ExpectedLocalRotation(
				0.0f,
				DirectionYaw(Door.Direction),
				0.0f);
			if (Door.SocketId != DirectionSocketId(Door.Direction)
				|| !Door.LocalPosition.Equals(
					ExpectedLocalPosition,
					0.1f)
				|| !Door.LocalRotation.Equals(
					ExpectedLocalRotation,
					0.1f))
			{
				return MakeReport(
					ESPDungeonLayoutValidationResult::InvalidDoor,
					TEXT("Door socket transform must match its direction."));
			}
			if (ConnectedIndexes.Contains(Door.ConnectedRoomIndex)
				|| UsedDirections.Contains(DirectionValue))
			{
				return MakeReport(
					ESPDungeonLayoutValidationResult::DuplicateDoor,
					TEXT("A room has duplicate door connections or directions."));
			}
			ConnectedIndexes.Add(Door.ConnectedRoomIndex);
			UsedDirections.Add(DirectionValue);

			const FIntPoint ExpectedCoordinate =
				Room.GridCoordinate + DoorStep;
			if (Layout.Rooms[Door.ConnectedRoomIndex].GridCoordinate
					!= ExpectedCoordinate
				|| !HasReciprocalDoor(Layout, Room.RoomIndex, Door))
			{
				return MakeReport(
					ESPDungeonLayoutValidationResult::InvalidDoor,
					TEXT("Door sockets must be adjacent and reciprocal."));
			}
			Adjacency[Room.RoomIndex].Add(Door.ConnectedRoomIndex);
			if (Room.RoomIndex < Door.ConnectedRoomIndex)
			{
				++UndirectedEdgeCount;
			}
		}
	}

	TArray<int32> Distances;
	Distances.Init(INDEX_NONE, Layout.Rooms.Num());
	TArray<int32> Queue;
	Queue.Add(Layout.EntryRoomIndex);
	Distances[Layout.EntryRoomIndex] = 0;
	for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
	{
		const int32 RoomIndex = Queue[QueueIndex];
		for (const int32 NeighborIndex : Adjacency[RoomIndex])
		{
			if (Distances[NeighborIndex] == INDEX_NONE)
			{
				Distances[NeighborIndex] = Distances[RoomIndex] + 1;
				Queue.Add(NeighborIndex);
			}
		}
	}
	if (Queue.Num() != Layout.Rooms.Num())
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::Disconnected,
			TEXT("Every dungeon room must be connected to EntryRecords."));
	}
	if (UndirectedEdgeCount != Layout.Rooms.Num() - 1)
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::Cyclic,
			TEXT("Dungeon connections must form one acyclic path-and-branch tree."));
	}
	if (Layout.Rooms[Layout.EntryRoomIndex].DoorSockets.Num() != 1
		|| Layout.Rooms[Layout.ObjectiveRoomIndex].DoorSockets.Num() != 1)
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::InvalidDoor,
			TEXT("EntryRecords and SealedStorage must be tree endpoints."));
	}

	for (const FSPDungeonRoomLayout& Room : Layout.Rooms)
	{
		if (Room.RoomIndex == Layout.EntryRoomIndex)
		{
			continue;
		}
		if (!Layout.Rooms.IsValidIndex(Room.ParentRoomIndex)
			|| !Adjacency[Room.RoomIndex].Contains(Room.ParentRoomIndex)
			|| Distances[Room.RoomIndex] != Room.DepthFromEntry
			|| Distances[Room.ParentRoomIndex] + 1
				!= Room.DepthFromEntry)
		{
			return MakeReport(
				ESPDungeonLayoutValidationResult::Disconnected,
				TEXT("Room parent and depth metadata must match the rooted tree."));
		}
	}

	if (Distances[Layout.ObjectiveRoomIndex]
		< MinimumObjectiveDistance)
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::ObjectiveTooClose,
			TEXT("SealedStorage must be at least four doors from EntryRecords."));
	}

	TSet<int32> ObjectivePath;
	int32 PathRoomIndex = Layout.ObjectiveRoomIndex;
	while (PathRoomIndex != INDEX_NONE)
	{
		if (ObjectivePath.Contains(PathRoomIndex))
		{
			return MakeReport(
				ESPDungeonLayoutValidationResult::Cyclic,
				TEXT("Primary path parent metadata contains a cycle."));
		}
		ObjectivePath.Add(PathRoomIndex);
		PathRoomIndex = Layout.Rooms[PathRoomIndex].ParentRoomIndex;
	}
	if (!ObjectivePath.Contains(Layout.EntryRoomIndex))
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::Disconnected,
			TEXT("Objective primary path does not reach EntryRecords."));
	}
	for (const FSPDungeonRoomLayout& Room : Layout.Rooms)
	{
		if (Room.bPrimaryPath != ObjectivePath.Contains(Room.RoomIndex))
		{
			return MakeReport(
				ESPDungeonLayoutValidationResult::InvalidObjectiveRoom,
				TEXT("Primary-path flags must identify the entry-to-objective path."));
		}
		for (const FSPDungeonDoorSocket& Door : Room.DoorSockets)
		{
			const bool bExpectedPrimary =
				Room.bPrimaryPath
				&& Layout.Rooms[Door.ConnectedRoomIndex].bPrimaryPath;
			if (Door.bPrimaryPath != bExpectedPrimary)
			{
				return MakeReport(
					ESPDungeonLayoutValidationResult::InvalidDoor,
					TEXT("Door primary-path metadata is inconsistent."));
			}
		}
	}

	int32 ObjectiveMarkerCount = 0;
	for (const FSPDungeonSpawnMarker& Marker
		: Layout.Rooms[Layout.ObjectiveRoomIndex].SpawnMarkers)
	{
		ObjectiveMarkerCount +=
			Marker.Kind == ESPDungeonSpawnMarkerKind::ContractObjective
			? 1
			: 0;
	}
	if (ObjectiveMarkerCount != 1)
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::InvalidMarker,
			TEXT("SealedStorage requires exactly one contract objective marker."));
	}
	for (const FSPDungeonRoomLayout& Room : Layout.Rooms)
	{
		if (Room.RoomIndex == Layout.ObjectiveRoomIndex)
		{
			continue;
		}
		if (Room.SpawnMarkers.ContainsByPredicate(
			[](const FSPDungeonSpawnMarker& Marker)
			{
				return Marker.Kind
					== ESPDungeonSpawnMarkerKind::ContractObjective;
			}))
		{
			return MakeReport(
				ESPDungeonLayoutValidationResult::InvalidMarker,
				TEXT("Only the objective room may contain an objective marker."));
		}
	}

	if (bVerifyChecksum
		&& (Layout.LayoutChecksum == 0
			|| Layout.LayoutChecksum != CalculateChecksum(Layout)))
	{
		return MakeReport(
			ESPDungeonLayoutValidationResult::ChecksumMismatch,
			TEXT("Layout checksum does not match its deterministic contents."));
	}

	return MakeReport(
		ESPDungeonLayoutValidationResult::Valid,
		TEXT("Dungeon layout is valid."));
}

int32 FSPDungeonLayoutGenerator::CalculateGraphDistance(
	const FSPDungeonLayout& Layout,
	const int32 StartRoomIndex,
	const int32 EndRoomIndex)
{
	if (!Layout.Rooms.IsValidIndex(StartRoomIndex)
		|| !Layout.Rooms.IsValidIndex(EndRoomIndex))
	{
		return INDEX_NONE;
	}
	if (StartRoomIndex == EndRoomIndex)
	{
		return 0;
	}

	TArray<int32> Distances;
	Distances.Init(INDEX_NONE, Layout.Rooms.Num());
	TArray<int32> Queue;
	Queue.Add(StartRoomIndex);
	Distances[StartRoomIndex] = 0;
	for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
	{
		const int32 RoomIndex = Queue[QueueIndex];
		for (const FSPDungeonDoorSocket& Door
			: Layout.Rooms[RoomIndex].DoorSockets)
		{
			if (!Layout.Rooms.IsValidIndex(Door.ConnectedRoomIndex)
				|| Distances[Door.ConnectedRoomIndex] != INDEX_NONE)
			{
				continue;
			}
			Distances[Door.ConnectedRoomIndex] =
				Distances[RoomIndex] + 1;
			if (Door.ConnectedRoomIndex == EndRoomIndex)
			{
				return Distances[Door.ConnectedRoomIndex];
			}
			Queue.Add(Door.ConnectedRoomIndex);
		}
	}
	return INDEX_NONE;
}

int64 FSPDungeonLayoutGenerator::CalculateChecksum(
	const FSPDungeonLayout& Layout)
{
	uint32 Hash = 2166136261u;
	HashValue(Hash, static_cast<uint32>(Layout.FormatVersion));
	HashName(Hash, Layout.SeedStableId);
	HashValue(Hash, static_cast<uint32>(Layout.Seed));
	HashValue(Hash, QuantizeFloat(Layout.RoomSpacing));
	HashValue(Hash, static_cast<uint32>(Layout.EntryRoomIndex));
	HashValue(Hash, static_cast<uint32>(Layout.ObjectiveRoomIndex));
	HashValue(Hash, static_cast<uint32>(Layout.Rooms.Num()));

	for (const FSPDungeonRoomLayout& Room : Layout.Rooms)
	{
		HashValue(Hash, static_cast<uint32>(Room.RoomIndex));
		HashValue(Hash, static_cast<uint32>(Room.Role));
		HashValue(Hash, static_cast<uint32>(Room.GridCoordinate.X));
		HashValue(Hash, static_cast<uint32>(Room.GridCoordinate.Y));
		HashValue(Hash, static_cast<uint32>(Room.ParentRoomIndex));
		HashValue(Hash, static_cast<uint32>(Room.DepthFromEntry));
		HashValue(Hash, Room.bPrimaryPath ? 1u : 0u);
		HashValue(Hash, static_cast<uint32>(Room.DoorSockets.Num()));
		for (const FSPDungeonDoorSocket& Door : Room.DoorSockets)
		{
			HashName(Hash, Door.SocketId);
			HashValue(Hash, static_cast<uint32>(Door.ConnectedRoomIndex));
			HashValue(Hash, static_cast<uint32>(Door.Direction));
			HashValue(Hash, QuantizeFloat(Door.LocalPosition.X));
			HashValue(Hash, QuantizeFloat(Door.LocalPosition.Y));
			HashValue(Hash, QuantizeFloat(Door.LocalPosition.Z));
			HashValue(Hash, QuantizeFloat(Door.LocalRotation.Pitch));
			HashValue(Hash, QuantizeFloat(Door.LocalRotation.Yaw));
			HashValue(Hash, QuantizeFloat(Door.LocalRotation.Roll));
			HashValue(Hash, Door.bPrimaryPath ? 1u : 0u);
		}
		HashValue(Hash, static_cast<uint32>(Room.SpawnMarkers.Num()));
		for (const FSPDungeonSpawnMarker& Marker : Room.SpawnMarkers)
		{
			HashName(Hash, Marker.MarkerId);
			HashValue(Hash, static_cast<uint32>(Marker.Kind));
			HashValue(Hash, QuantizeFloat(Marker.LocalPosition.X));
			HashValue(Hash, QuantizeFloat(Marker.LocalPosition.Y));
			HashValue(Hash, QuantizeFloat(Marker.LocalPosition.Z));
			HashValue(Hash, QuantizeFloat(Marker.LocalRotation.Pitch));
			HashValue(Hash, QuantizeFloat(Marker.LocalRotation.Yaw));
			HashValue(Hash, QuantizeFloat(Marker.LocalRotation.Roll));
			HashValue(Hash, QuantizeFloat(Marker.SpawnWeight));
		}
	}
	return static_cast<int64>(Hash);
}
