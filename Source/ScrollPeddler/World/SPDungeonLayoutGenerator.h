#pragma once

#include "CoreMinimal.h"
#include "World/SPDungeonLayoutTypes.h"

class USPDungeonSeedDefinition;

/**
 * Pure deterministic layout builder and validator.
 *
 * Generated layouts are rooted trees: a six-room primary path from
 * EntryRecords to SealedStorage plus two one-room branches.
 */
struct SCROLLPEDDLER_API FSPDungeonLayoutGenerator
{
	static constexpr int32 RequiredRoomCount = 8;
	static constexpr int32 PrimaryPathRoomCount = 6;
	static constexpr int32 MinimumObjectiveDistance = 4;
	static constexpr float DefaultRoomSpacing = 2400.0f;

	static bool Generate(
		const USPDungeonSeedDefinition* Definition,
		FSPDungeonLayout& OutLayout,
		FSPDungeonLayoutValidationReport& OutReport);

	static FSPDungeonLayoutValidationReport ValidateSeedDefinition(
		const USPDungeonSeedDefinition* Definition);

	static FSPDungeonLayoutValidationReport ValidateLayout(
		const FSPDungeonLayout& Layout,
		bool bVerifyChecksum = true);

	static int32 CalculateGraphDistance(
		const FSPDungeonLayout& Layout,
		int32 StartRoomIndex,
		int32 EndRoomIndex);

	static int64 CalculateChecksum(const FSPDungeonLayout& Layout);
};
