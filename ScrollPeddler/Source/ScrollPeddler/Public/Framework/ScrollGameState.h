#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "ScrollGameState.generated.h"

/**
 * Replicated state shared by every participant.
 * Effect timing uses GetServerWorldTimeSeconds() as the common clock; only the server decides expiry.
 */
UCLASS()
class SCROLLPEDDLER_API AScrollGameState : public AGameStateBase
{
	GENERATED_BODY()
};
