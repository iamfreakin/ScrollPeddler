#include "Game/SPPlayerStartPolicy.h"

namespace
{
const FName SPCanonicalPlayerStartTags[
	SPPlayerStartPolicy::SlotCount] =
{
	TEXT("SP_PlayerStart_0"),
	TEXT("SP_PlayerStart_1"),
	TEXT("SP_PlayerStart_2"),
	TEXT("SP_PlayerStart_3")
};
}

FName SPPlayerStartPolicy::MakeSlotTag(const int32 SlotIndex)
{
	return SlotIndex >= 0 && SlotIndex < SlotCount
		? SPCanonicalPlayerStartTags[SlotIndex]
		: NAME_None;
}

int32 SPPlayerStartPolicy::GetSlotIndex(const FName PlayerStartTag)
{
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		if (PlayerStartTag == SPCanonicalPlayerStartTags[SlotIndex])
		{
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

int32 SPPlayerStartPolicy::ChooseLowestAvailableSlot(
	const TSet<int32>& ConfiguredSlots,
	const TSet<int32>& ReservedSlots)
{
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		if (ConfiguredSlots.Contains(SlotIndex)
			&& !ReservedSlots.Contains(SlotIndex))
		{
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}
