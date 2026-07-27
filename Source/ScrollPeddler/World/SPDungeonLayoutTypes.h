#pragma once

#include "CoreMinimal.h"
#include "Data/SPVerticalSliceDefinitions.h"
#include "SPDungeonLayoutTypes.generated.h"

UENUM(BlueprintType)
enum class ESPDungeonCardinalDirection : uint8
{
	North,
	East,
	South,
	West
};

UENUM(BlueprintType)
enum class ESPDungeonSpawnMarkerKind : uint8
{
	Item,
	Threat,
	ContractObjective
};

UENUM(BlueprintType)
enum class ESPDungeonLayoutValidationResult : uint8
{
	Valid,
	NotAuthority,
	MissingStableId,
	InvalidSeed,
	InvalidRoomCount,
	UnsupportedFormat,
	InvalidRoomRole,
	DuplicateRoomRole,
	InvalidRoomIndex,
	DuplicateCoordinate,
	InvalidEntryRoom,
	InvalidObjectiveRoom,
	ObjectiveTooClose,
	InvalidDoor,
	DuplicateDoor,
	Disconnected,
	Cyclic,
	InvalidMarker,
	ChecksumMismatch
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPDungeonLayoutValidationReport
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPDungeonLayoutValidationResult Result =
		ESPDungeonLayoutValidationResult::Valid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FText Message;

	bool IsValid() const
	{
		return Result == ESPDungeonLayoutValidationResult::Valid;
	}
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPDungeonDoorSocket
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName SocketId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 ConnectedRoomIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPDungeonCardinalDirection Direction =
		ESPDungeonCardinalDirection::North;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector LocalPosition = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FRotator LocalRotation = FRotator::ZeroRotator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bPrimaryPath = false;

	bool operator==(const FSPDungeonDoorSocket& Other) const;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPDungeonSpawnMarker
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName MarkerId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPDungeonSpawnMarkerKind Kind =
		ESPDungeonSpawnMarkerKind::Item;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector LocalPosition = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FRotator LocalRotation = FRotator::ZeroRotator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float SpawnWeight = 1.0f;

	bool operator==(const FSPDungeonSpawnMarker& Other) const;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPDungeonRoomLayout
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 RoomIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPRoomRole Role = ESPRoomRole::EntryRecords;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint GridCoordinate = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 ParentRoomIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 DepthFromEntry = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bPrimaryPath = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FSPDungeonDoorSocket> DoorSockets;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FSPDungeonSpawnMarker> SpawnMarkers;

	bool operator==(const FSPDungeonRoomLayout& Other) const;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPDungeonLayout
{
	GENERATED_BODY()

	static constexpr int32 CurrentFormatVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 FormatVersion = CurrentFormatVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName SeedStableId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 Seed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float RoomSpacing = 2400.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 EntryRoomIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 ObjectiveRoomIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FSPDungeonRoomLayout> Rooms;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 LayoutChecksum = 0;

	const FSPDungeonRoomLayout* FindRoom(int32 RoomIndex) const;
	bool IsEmpty() const { return Rooms.IsEmpty(); }
	bool operator==(const FSPDungeonLayout& Other) const;
};

SCROLLPEDDLER_API FIntPoint SPDungeonDirectionToGridStep(
	ESPDungeonCardinalDirection Direction);

SCROLLPEDDLER_API ESPDungeonCardinalDirection SPOppositeDungeonDirection(
	ESPDungeonCardinalDirection Direction);
