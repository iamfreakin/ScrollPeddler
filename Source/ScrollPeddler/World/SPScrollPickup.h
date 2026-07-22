#pragma once

#include "CoreMinimal.h"
#include "Core/SPTypes.h"
#include "GameFramework/Actor.h"
#include "SPScrollPickup.generated.h"

class ASPCharacter;
class UBoxComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FStreamableHandle;

UCLASS()
class SCROLLPEDDLER_API ASPScrollPickup : public AActor
{
	GENERATED_BODY()

public:
	ASPScrollPickup();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Called by the authoritative game flow immediately after spawning the pickup. */
	void InitializeScroll(const FSPScrollInstance& InScrollInstance);

	const FSPScrollInstance& GetScrollInstance() const { return ScrollInstance; }
	bool IsAvailable() const;

	/** Server-side reservation used to make pickup transfer atomic on the game thread. */
	bool TryReserve(ASPCharacter* Claimant);
	void ReleaseReservation(ASPCharacter* Claimant);
	bool CommitClaim(ASPCharacter* Claimant);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void OnRep_ScrollInstance();

	UFUNCTION()
	void OnRep_Claimed();

	void RequestPickupVisual();
	void HandlePickupVisualLoaded(
		TSharedPtr<FStreamableHandle> CompletedHandle,
		uint32 RequestId,
		FGuid RequestedInstanceId,
		FPrimaryAssetId RequestedDefinitionId);
	void CancelPickupVisualLoad();
	void ApplyFallbackVisual();
	void LogVisualFallback(const TCHAR* Reason, const FPrimaryAssetId& DefinitionId) const;
	void ApplyClaimedPresentation();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> InteractionBounds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> PickupVisual;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> FallbackVisualMesh;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_ScrollInstance, Category = "Pickup", meta = (AllowPrivateAccess = "true"))
	FSPScrollInstance ScrollInstance;

	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing = OnRep_Claimed)
	bool bClaimed = false;

	TWeakObjectPtr<ASPCharacter> ReservedBy;
	TSharedPtr<FStreamableHandle> PickupVisualLoadHandle;
	uint32 PickupVisualRequestId = 0;
};
