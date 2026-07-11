#pragma once

#include "CoreMinimal.h"
#include "Core/SPTypes.h"
#include "GameFramework/Actor.h"
#include "SPScrollPickup.generated.h"

class ASPCharacter;
class UStaticMeshComponent;

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

private:
	UFUNCTION()
	void OnRep_Claimed();

	void ApplyClaimedPresentation();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> PickupMesh;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "Pickup", meta = (AllowPrivateAccess = "true"))
	FSPScrollInstance ScrollInstance;

	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing = OnRep_Claimed)
	bool bClaimed = false;

	TWeakObjectPtr<ASPCharacter> ReservedBy;
};
