#pragma once

#include "CoreMinimal.h"
#include "Core/SPInteractionTypes.h"
#include "UObject/Interface.h"
#include "SPInteractable.generated.h"

class APawn;

UINTERFACE(BlueprintType)
class SCROLLPEDDLER_API USPInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Presentation-neutral interaction boundary.
 *
 * Implementations must still perform their authoritative distance, line of
 * sight, ownership, state, capacity, and replay checks before mutating state.
 */
class SCROLLPEDDLER_API ISPInteractable
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Scroll Peddler|Interaction")
	FText GetInteractionPrompt(const APawn* Viewer) const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Scroll Peddler|Interaction")
	FVector GetInteractionLocation() const;

	virtual ESPInteractionResultCode ValidateInteraction(
		const APawn* RequestingPawn,
		const FSPInteractionRequest& Request) const = 0;
};
