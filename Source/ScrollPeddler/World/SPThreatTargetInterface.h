#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "World/SPThreatRuntimeTypes.h"
#include "SPThreatTargetInterface.generated.h"

UINTERFACE(BlueprintType)
class SCROLLPEDDLER_API USPThreatItemTarget : public UInterface
{
	GENERATED_BODY()
};

/**
 * Server-authoritative boundary for Paper Eater targets. Implementations own
 * item revision, claim, contamination, and destruction validation.
 */
class SCROLLPEDDLER_API ISPThreatItemTarget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Scroll Peddler|Threat")
	bool IsAvailableToPaperEater() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Scroll Peddler|Threat")
	ESPThreatItemTargetKind GetThreatItemTargetKind() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Scroll Peddler|Threat")
	FVector GetThreatItemTargetLocation() const;

	/**
	 * Returns true only when the authoritative owner accepted the mutation.
	 * Retried/rejected intents must not contaminate or damage an item twice.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Scroll Peddler|Threat")
	bool RequestPaperEaterCorruption(const FSPPaperCorruptionIntent& Intent);
};
