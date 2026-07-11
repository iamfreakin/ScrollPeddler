#pragma once

#include "CoreMinimal.h"
#include "Core/SPTypes.h"
#include "GameFramework/PlayerState.h"
#include "SPPlayerState.generated.h"

UCLASS()
class SCROLLPEDDLER_API ASPPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ASPPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server-only accounting. Returns true only when this instance is newly recorded. */
	bool RecordScrollPickedUp(const FSPScrollInstance& Item);

	/** Server-only accounting. Returns true only when this known carried instance is newly consumed. */
	bool RecordScrollConsumed(const FSPScrollInstance& Item, int32 DeliveryValue);

	/** Server-only compensation used if a reserved world pickup cannot finish its claim. */
	bool RollbackScrollPickedUp(const FSPScrollInstance& Item);

	/** Server-only and idempotent. Freezes this player's extraction totals. */
	void MarkExtracted();

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	bool IsExtracted() const { return bExtracted; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	int32 GetPickedUpCount() const { return PickedUpCount; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	int32 GetConsumedScrollCount() const { return ConsumedScrollCount; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	int32 GetExtractedScrollCount() const { return ExtractedScrollCount; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	int32 GetCarriedDeliveryValue() const { return CarriedDeliveryValue; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Session")
	int32 GetGoldDelta() const { return GoldDelta; }

private:
	UFUNCTION()
	void OnRep_Extracted();

	UPROPERTY(Replicated)
	int32 PickedUpCount = 0;

	UPROPERTY(Replicated)
	int32 ConsumedScrollCount = 0;

	UPROPERTY(Replicated)
	int32 ExtractedScrollCount = 0;

	UPROPERTY(Replicated)
	int32 CarriedDeliveryValue = 0;

	UPROPERTY(Replicated)
	int32 GoldDelta = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Extracted)
	bool bExtracted = false;

	/** Authority-only guards against duplicated RPCs and overlap callbacks. */
	TSet<FGuid> PickedUpInstanceIds;
	TSet<FGuid> ConsumedInstanceIds;
	TMap<FGuid, int32> PickedUpDeliveryValues;
};
