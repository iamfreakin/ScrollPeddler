#pragma once

#include "CoreMinimal.h"
#include "Core/SPScrollUseTypes.h"

/**
 * Pure server-authority scroll-use policy. It does not mutate inventory, actors
 * or the world; callers atomically consume the instance and apply returned
 * commands after an Accepted result.
 */
class SCROLLPEDDLER_API FSPScrollUseResolver final
{
public:
	static FSPScrollUseResult Resolve(
		const FSPScrollUseRequest& Request,
		const FSPAuthoritativeScrollUseState& AuthorityState,
		const FSPScrollFamilyTuning& Family,
		const FSPScrollEngravingUseTuning& Engraving);

private:
	static uint32 BuildDeterministicHash(
		const FSPAuthoritativeScrollUseState& AuthorityState,
		const TCHAR* Salt);
};
