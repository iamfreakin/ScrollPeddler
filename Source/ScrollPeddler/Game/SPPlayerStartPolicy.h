#pragma once

#include "CoreMinimal.h"

namespace SPPlayerStartPolicy
{
inline constexpr int32 SlotCount = 4;

/** Returns the canonical PlayerStartTag, or NAME_None for an invalid slot. */
SCROLLPEDDLER_API FName MakeSlotTag(int32 SlotIndex);

/** Returns 0..3 only for one of the canonical expedition PlayerStartTags. */
SCROLLPEDDLER_API int32 GetSlotIndex(FName PlayerStartTag);

/** Selects the lowest configured slot that has not already been reserved. */
SCROLLPEDDLER_API int32 ChooseLowestAvailableSlot(
	const TSet<int32>& ConfiguredSlots,
	const TSet<int32>& ReservedSlots);
}
