#include "World/SPDungeonLayoutTypes.h"

bool FSPDungeonDoorSocket::operator==(
	const FSPDungeonDoorSocket& Other) const
{
	return SocketId == Other.SocketId
		&& ConnectedRoomIndex == Other.ConnectedRoomIndex
		&& Direction == Other.Direction
		&& LocalPosition.Equals(Other.LocalPosition)
		&& LocalRotation.Equals(Other.LocalRotation)
		&& bPrimaryPath == Other.bPrimaryPath;
}

bool FSPDungeonSpawnMarker::operator==(
	const FSPDungeonSpawnMarker& Other) const
{
	return MarkerId == Other.MarkerId
		&& Kind == Other.Kind
		&& LocalPosition.Equals(Other.LocalPosition)
		&& LocalRotation.Equals(Other.LocalRotation)
		&& FMath::IsNearlyEqual(SpawnWeight, Other.SpawnWeight);
}

bool FSPDungeonRoomLayout::operator==(
	const FSPDungeonRoomLayout& Other) const
{
	return RoomIndex == Other.RoomIndex
		&& Role == Other.Role
		&& GridCoordinate == Other.GridCoordinate
		&& WorldLocation.Equals(Other.WorldLocation)
		&& ParentRoomIndex == Other.ParentRoomIndex
		&& DepthFromEntry == Other.DepthFromEntry
		&& bPrimaryPath == Other.bPrimaryPath
		&& DoorSockets == Other.DoorSockets
		&& SpawnMarkers == Other.SpawnMarkers;
}

const FSPDungeonRoomLayout* FSPDungeonLayout::FindRoom(
	const int32 RoomIndex) const
{
	return Rooms.IsValidIndex(RoomIndex)
		&& Rooms[RoomIndex].RoomIndex == RoomIndex
		? &Rooms[RoomIndex]
		: nullptr;
}

bool FSPDungeonLayout::operator==(const FSPDungeonLayout& Other) const
{
	return FormatVersion == Other.FormatVersion
		&& SeedStableId == Other.SeedStableId
		&& Seed == Other.Seed
		&& FMath::IsNearlyEqual(RoomSpacing, Other.RoomSpacing)
		&& EntryRoomIndex == Other.EntryRoomIndex
		&& ObjectiveRoomIndex == Other.ObjectiveRoomIndex
		&& Rooms == Other.Rooms
		&& LayoutChecksum == Other.LayoutChecksum;
}

FIntPoint SPDungeonDirectionToGridStep(
	const ESPDungeonCardinalDirection Direction)
{
	switch (Direction)
	{
	case ESPDungeonCardinalDirection::North:
		return FIntPoint(0, 1);
	case ESPDungeonCardinalDirection::East:
		return FIntPoint(1, 0);
	case ESPDungeonCardinalDirection::South:
		return FIntPoint(0, -1);
	case ESPDungeonCardinalDirection::West:
		return FIntPoint(-1, 0);
	default:
		return FIntPoint::ZeroValue;
	}
}

ESPDungeonCardinalDirection SPOppositeDungeonDirection(
	const ESPDungeonCardinalDirection Direction)
{
	switch (Direction)
	{
	case ESPDungeonCardinalDirection::North:
		return ESPDungeonCardinalDirection::South;
	case ESPDungeonCardinalDirection::East:
		return ESPDungeonCardinalDirection::West;
	case ESPDungeonCardinalDirection::South:
		return ESPDungeonCardinalDirection::North;
	case ESPDungeonCardinalDirection::West:
	default:
		return ESPDungeonCardinalDirection::East;
	}
}
